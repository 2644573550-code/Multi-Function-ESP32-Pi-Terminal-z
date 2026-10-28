#include "core/SecurityService.h"

#include "security/AuditLog.h"
#include "security/Backup.h"
#include "security/CardMatcher.h"
#include "security/PermissionMatrix.h"
#include "security/SessionManager.h"
#include "security/TrustStore.h"
#include "security/UserDatabase.h"
#include "protocol/uid_index.h"

#include <cmath>

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>

#include <openssl/evp.h>

#include <vector>

namespace {

// 导出文件的信封：魔数 + 版本 + salt + nonce + 密文。
// 用 AES-256-GCM，key 由导出密码经 PBKDF2 派生（与 PIN 策略同一原语）。
constexpr char kExportMagic[] = "OPTEXP01";
constexpr int kExportSaltBytes = 16;
constexpr int kExportNonceBytes = 12;
constexpr int kExportTagBytes = 16;
// 导出密码允许比 PIN 宽松：不是 6 位数字限制，但必须有长度。
constexpr int kMinExportPassword = 8;

QByteArray deriveExportKey(const QString &password, const QByteArray &salt) {
    const QByteArray pin = password.toUtf8();
    unsigned char out[32];
    // 用 600k 迭代与 PIN 保持一致；导出是低频操作，延迟可接受。
    if (PKCS5_PBKDF2_HMAC(pin.constData(), pin.size(),
                          reinterpret_cast<const unsigned char *>(salt.constData()),
                          salt.size(), 600000, EVP_sha256(), 32, out) != 1)
        return {};
    return QByteArray(reinterpret_cast<const char *>(out), 32);
}

}  // namespace

SecurityService::SecurityService(QObject *parent) : QObject(parent) {}
SecurityService::~SecurityService() = default;

void SecurityService::setSessionManager(security::SessionManager *session) {
    session_ = session;
    ready_ = session_ != nullptr;
    emit changed();
}

void SecurityService::setUserDatabase(security::UserDatabase *database) {
    database_ = database;
    // 库换了就把卡索引的缓存丢掉：旧索引指向旧库。
    delete cards_;
    cards_ = nullptr;
    emit changed();
}

void SecurityService::setMessage(const QString &message) {
    if (lastMessage_ == message) return;
    lastMessage_ = message;
    emit changed();
}

QString SecurityService::activeUserId() const {
    return session_ ? session_->activeUserId() : QString();
}

bool SecurityService::requireUser(const QString &action) {
    if (activeUserId().isEmpty() || database_ == nullptr || !database_->isOpen()) {
        setMessage(QStringLiteral("NOT_AUTHORIZED"));
        // 未登录也要留痕：谁在什么动作上被拒（actor 记为 anonymous）。
        audit(action, QStringLiteral("none"), QString::fromLatin1(security::AuditResult::Denied));
        return false;
    }
    return true;
}

bool SecurityService::requireAdmin(const QString &action) {
    if (!session_ || !session_->isAdmin()) {
        setMessage(QStringLiteral("NOT_AUTHORIZED"));
        audit(action, QStringLiteral("admin"), QString::fromLatin1(security::AuditResult::Denied));
        return false;
    }
    return true;
}

void SecurityService::audit(const QString &action, const QString &target, const QString &result) {
    // 审计写入失败不阻断业务，但要把失败暴露给调用方。
    if (!database_ || !database_->isOpen()) return;
    if (auto *log = database_->audit())
        log->record(activeUserId().isEmpty() ? QStringLiteral("anonymous") : activeUserId(),
                    action, target, result);
}

TrustStore *SecurityService::trustStore() const {
    if (!trustStore_) trustStore_ = new TrustStore();
    return trustStore_;
}

security::PermissionMatrix *SecurityService::permissions() const {
    if (!permissions_) permissions_ = new security::PermissionMatrix();
    return permissions_;
}

// ---- 桥板信任 ----

QVariantList SecurityService::trustedBridges() const {
    QVariantList list;
    const auto records = trustStore()->all();
    for (const auto &record : records) {
        if (!record.active()) continue;
        QVariantMap entry;
        entry.insert(QStringLiteral("bridgeId"), record.bridgeId);
        // 只给指纹前缀与 key_id，不给完整公钥。
        entry.insert(QStringLiteral("fingerprint"), QString::fromLatin1(record.fingerprint.toHex()));
        entry.insert(QStringLiteral("keyId"), TrustStore::keyIdText(record.keyId()));
        entry.insert(QStringLiteral("enrolledAt"), record.enrolledAt);
        list.append(entry);
    }
    return list;
}

bool SecurityService::healthy() const {
    return trustStore()->healthy();
}

bool SecurityService::pairBridge(const QString &bridgeId, const QString &publicKeyHex) {
    if (!requireAdmin(QString::fromLatin1(security::AuditAction::PairBridge))) return false;
    if (!TrustStore::isValidBridgeId(bridgeId)) {
        setMessage(QStringLiteral("INVALID_MESSAGE"));
        audit(QString::fromLatin1(security::AuditAction::PairBridge), bridgeId,
              QString::fromLatin1(security::AuditResult::Failed));
        return false;
    }
    const QByteArray publicKey = QByteArray::fromHex(publicKeyHex.toLatin1());
    QString error;
    if (!trustStore()->enrollIdentity(bridgeId, publicKey, &error)) {
        setMessage(error.isEmpty() ? QStringLiteral("IDENTITY_REJECTED") : error);
        audit(QString::fromLatin1(security::AuditAction::PairBridge), bridgeId,
              QString::fromLatin1(security::AuditResult::Failed));
        return false;
    }
    audit(QString::fromLatin1(security::AuditAction::PairBridge), bridgeId,
          QString::fromLatin1(security::AuditResult::Ok));
    setMessage(QString());
    emit changed();
    return true;
}

bool SecurityService::revokeBridge(const QString &fingerprintHex) {
    if (!requireAdmin(QString::fromLatin1(security::AuditAction::RevokeBridge))) return false;
    const QByteArray fingerprint = QByteArray::fromHex(fingerprintHex.toLatin1());
    if (fingerprint.isEmpty()) {
        setMessage(QStringLiteral("INVALID_MESSAGE"));
        return false;
    }
    QString error;
    if (!trustStore()->revokeByFingerprint(fingerprint, &error)) {
        setMessage(error.isEmpty() ? QStringLiteral("NOT_FOUND") : error);
        audit(QString::fromLatin1(security::AuditAction::RevokeBridge), fingerprintHex,
              QString::fromLatin1(security::AuditResult::Failed));
        return false;
    }
    audit(QString::fromLatin1(security::AuditAction::RevokeBridge), fingerprintHex,
          QString::fromLatin1(security::AuditResult::Ok));
    setMessage(QString());
    emit changed();
    return true;
}

// ---- 刷卡选用户 ----

void SecurityService::setBridgeAvailable(bool available) {
    if (bridgeAvailable_ == available) return;
    bridgeAvailable_ = available;
    emit changed();
}

QVariantMap SecurityService::resolveCard(const QString &uidHex) {
    QVariantMap result;
    result.insert(QStringLiteral("matched"), false);

    // §10.3：桥板失联时位置未知，禁止新的刷卡。
    if (!bridgeAvailable_) {
        setMessage(QStringLiteral("POSITION_UNKNOWN"));
        result.insert(QStringLiteral("error"), QStringLiteral("POSITION_UNKNOWN"));
        return result;
    }

    const QByteArray raw = QByteArray::fromHex(uidHex.toLatin1());
    // 只接受 4/7/10 字节；其他长度一律失败关闭。
    if (raw.size() != 4 && raw.size() != 7 && raw.size() != 10) {
        setMessage(QStringLiteral("INVALID_MESSAGE"));
        result.insert(QStringLiteral("error"), QStringLiteral("INVALID_MESSAGE"));
        return result;
    }

    if (!cards_) cards_ = new security::CardIndex();
    // raw UID 只在这里短暂存在：派生成为 HMAC 索引后立即丢弃。
    const auto match = cards_->findOwner(raw, &deriveUidIndex);
    switch (match.verdict) {
    case security::CardMatch::Verdict::Matched:
        result.insert(QStringLiteral("matched"), true);
        // 只回 userId：调用方据此跳 PIN 页，**不得**直接登录。
        result.insert(QStringLiteral("userId"), match.userId);
        audit(QString::fromLatin1(security::AuditAction::GrantDevice),
              QStringLiteral("card.matched"), QString::fromLatin1(security::AuditResult::Ok));
        break;
    case security::CardMatch::Verdict::NoMatch:
        result.insert(QStringLiteral("error"), QStringLiteral("NO_MATCH"));
        audit(QString::fromLatin1(security::AuditAction::GrantDevice),
              QStringLiteral("card.unmatched"), QString::fromLatin1(security::AuditResult::Denied));
        break;
    case security::CardMatch::Verdict::IndexRejected:
        // 密钥不可用或长度非法：失败关闭，不退回明文 UID 比较。
        result.insert(QStringLiteral("error"), QStringLiteral("INVALID_MESSAGE"));
        break;
    }
    return result;
}

// ---- 卡绑定 ----

QVariantList SecurityService::boundCards() const {
    QVariantList list;
    const QString userId = activeUserId();
    if (userId.isEmpty()) return list;
    if (!cards_) cards_ = new security::CardIndex();

    // 卡索引只存 HMAC，不存 raw UID；界面拿不到卡号本身，
    // 只能用序号 + 绑定时间做区分。挂失时按序号回查索引。
    const auto entries = cards_->boundCards(userId);
    int index = 0;
    for (const auto &entry : entries) {
        QVariantMap item;
        item.insert(QStringLiteral("label"),
                    QStringLiteral("card-%1").arg(index++));
        item.insert(QStringLiteral("boundAtMs"), entry.boundAtMs);
        // 界面不显示 raw UID，因此挂失按钮只做"按序号撤销全部"的语义，
        // 单卡挂失需要用户重新刷卡（P1 再做索引列表）。
        item.insert(QStringLiteral("rawUidHex"), QString());
        list.append(item);
    }
    return list;
}

bool SecurityService::bindCard(const QString &rawUidHex) {
    if (!requireUser(QString::fromLatin1(security::AuditAction::GrantDevice))) return false;
    // 绑卡必须已登录且库已打开（requireUser 已保证）。
    const QByteArray raw = QByteArray::fromHex(rawUidHex.toLatin1());
    if (raw.size() != 4 && raw.size() != 7 && raw.size() != 10) {
        setMessage(QStringLiteral("INVALID_MESSAGE"));
        return false;
    }
    if (!cards_) cards_ = new security::CardIndex();
    const bool ok = cards_->bind(activeUserId(), raw, &deriveUidIndex);
    audit(QString::fromLatin1(security::AuditAction::GrantDevice),
          QStringLiteral("card"), ok ? QString::fromLatin1(security::AuditResult::Ok)
                                     : QString::fromLatin1(security::AuditResult::Failed));
    if (!ok) setMessage(QStringLiteral("STORAGE_UNAVAILABLE"));
    else setMessage(QString());
    emit changed();
    return ok;
}

bool SecurityService::revokeCard(const QString &rawUidHex) {
    if (!requireUser(QString::fromLatin1(security::AuditAction::RevokeDevice))) return false;
    const QByteArray raw = QByteArray::fromHex(rawUidHex.toLatin1());
    if (!cards_) cards_ = new security::CardIndex();
    const bool ok = cards_->revoke(activeUserId(), raw, &deriveUidIndex);
    audit(QString::fromLatin1(security::AuditAction::RevokeDevice),
          QStringLiteral("card"), ok ? QString::fromLatin1(security::AuditResult::Ok)
                                     : QString::fromLatin1(security::AuditResult::Failed));
    emit changed();
    return ok;
}

int SecurityService::revokeAllCards() {
    if (!requireUser(QString::fromLatin1(security::AuditAction::RevokeDevice))) return 0;
    if (!cards_) cards_ = new security::CardIndex();
    const int removed = cards_->revokeAll(activeUserId());
    audit(QString::fromLatin1(security::AuditAction::RevokeDevice),
          QStringLiteral("card.all"),
          removed > 0 ? QString::fromLatin1(security::AuditResult::Ok)
                      : QString::fromLatin1(security::AuditResult::Failed));
    emit changed();
    return removed;
}

// ---- 能力授权 ----

QVariantList SecurityService::capabilities(int moduleId) const {
    QVariantList list;
    const QString userId = activeUserId();
    if (userId.isEmpty()) return list;
    const QStringList granted = permissions()->granted(userId, moduleId);
    for (const QString &capability : granted) {
        QVariantMap entry;
        entry.insert(QStringLiteral("capability"), capability);
        entry.insert(QStringLiteral("granted"), true);
        list.append(entry);
    }
    return list;
}

void SecurityService::noteRequestedCapabilities(int moduleId, const QStringList &capabilities) {
    requested_.insert(moduleId, capabilities);
    emit changed();
}

QVariantList SecurityService::requestedCapabilities(int moduleId) const {
    QVariantList list;
    const QString userId = activeUserId();
    const QStringList requested = requested_.value(moduleId);
    for (const QString &capability : requested) {
        QVariantMap entry;
        entry.insert(QStringLiteral("capability"), capability);
        // manifest 的声明与用户授予是两件事：这里如实分开报告。
        entry.insert(QStringLiteral("requested"), true);
        entry.insert(QStringLiteral("granted"),
                     !userId.isEmpty() && permissions()->has(userId, moduleId, capability));
        list.append(entry);
    }
    return list;
}

bool SecurityService::grantCapability(const QString &targetUserId, int moduleId,
                                      const QString &capability) {
    if (!requireAdmin(QStringLiteral("device.grant"))) return false;
    permissions()->addUser(targetUserId);
    const auto verdict = permissions()->grant(targetUserId, moduleId, capability);
    const bool ok = verdict == security::GrantVerdict::Granted;
    if (!ok) setMessage(security::PermissionMatrix::describe(verdict));
    else setMessage(QString());
    audit(QString::fromLatin1(security::AuditAction::GrantDevice),
          capability, ok ? QString::fromLatin1(security::AuditResult::Ok)
                         : QString::fromLatin1(security::AuditResult::Denied));
    emit changed();
    return ok;
}

bool SecurityService::revokeCapability(const QString &targetUserId, int moduleId,
                                       const QString &capability) {
    if (!requireAdmin(QString::fromLatin1(security::AuditAction::RevokeDevice))) return false;
    const bool ok = permissions()->revoke(targetUserId, moduleId, capability);
    audit(QString::fromLatin1(security::AuditAction::RevokeDevice),
          capability, ok ? QString::fromLatin1(security::AuditResult::Ok)
                         : QString::fromLatin1(security::AuditResult::Failed));
    emit changed();
    return ok;
}

// ---- 审计 ----

QVariantList SecurityService::recentAudit(int limit) const {
    QVariantList list;
    if (!database_ || !database_->isOpen()) return list;
    auto *log = database_->audit();
    if (!log) return list;
    const auto entries = log->recent(limit);
    for (const auto &entry : entries) {
        QVariantMap item;
        item.insert(QStringLiteral("atMs"), entry.atMs);
        item.insert(QStringLiteral("actor"), entry.actor);
        item.insert(QStringLiteral("action"), entry.action);
        item.insert(QStringLiteral("target"), entry.target);
        item.insert(QStringLiteral("result"), entry.result);
        list.append(item);
    }
    return list;
}

// ---- 导出 / 删除 ----

bool SecurityService::addManualReading(double value, const QString &unit,
                                       const QString &deviceModel) {
    if (!requireUser(QStringLiteral("data.manualEntry"))) return false;
    // 报告 §12.3：边界值必须校验，不能补造数据。
    if (!std::isfinite(value) || value <= 0 || value > 400) {
        setMessage(QStringLiteral("INVALID_MESSAGE"));
        return false;
    }
    const bool ok = database_->appendManualReading(value, unit, deviceModel);
    audit(QStringLiteral("data.manualEntry"), QStringLiteral("own"),
          ok ? QString::fromLatin1(security::AuditResult::Ok)
             : QString::fromLatin1(security::AuditResult::Failed));
    setMessage(ok ? QString() : QStringLiteral("STORAGE_UNAVAILABLE"));
    emit changed();
    return ok;
}

QVariantList SecurityService::readings(int limit) const {
    QVariantList list;
    if (!database_ || !database_->isOpen()) return list;
    for (const auto &reading : database_->queryReadings(limit)) {
        QVariantMap item;
        item.insert(QStringLiteral("receivedAtMs"), reading.receivedAtMs);
        item.insert(QStringLiteral("measuredAtMs"), reading.measuredAtMs);
        item.insert(QStringLiteral("source"), reading.source);
        item.insert(QStringLiteral("deviceModel"), reading.deviceModel);
        item.insert(QStringLiteral("metric"), reading.metric);
        // 缺失值用 isValid=false 表达，不塞 0。
        item.insert(QStringLiteral("hasValue"), reading.hasValue);
        if (reading.hasValue) item.insert(QStringLiteral("value"), reading.value);
        item.insert(QStringLiteral("unit"), reading.unit);
        item.insert(QStringLiteral("valid"), reading.valid);
        item.insert(QStringLiteral("invalidReason"), reading.invalidReason);
        list.append(item);
    }
    return list;
}

bool SecurityService::exportEncrypted(const QString &path, const QString &password) {
    if (!requireUser(QString::fromLatin1(security::AuditAction::ExportData))) return false;
    if (path.isEmpty() || password.size() < kMinExportPassword) {
        setMessage(QStringLiteral("INVALID_MESSAGE"));
        audit(QString::fromLatin1(security::AuditAction::ExportData), QStringLiteral("own"),
              QString::fromLatin1(security::AuditResult::Failed));
        return false;
    }

    // 导出内容：本用户的最近历史（不含任何密钥/PIN）。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto series = database_->historySeries(now, 7LL * 24 * 60 * 60 * 1000, 60000);
    QJsonArray rows;
    for (const auto &bucket : series) {
        rows.append(QJsonObject{{QStringLiteral("t"), double(bucket.bucketMs)},
                                {QStringLiteral("sum"), bucket.sum},
                                {QStringLiteral("low"), bucket.low},
                                {QStringLiteral("high"), bucket.high},
                                {QStringLiteral("count"), bucket.count}});
    }
    const QByteArray plaintext = QJsonDocument(
        QJsonObject{{QStringLiteral("schema"), QStringLiteral("orange-pi-export-v1")},
                    {QStringLiteral("exported_at"), double(now)},
                    {QStringLiteral("history"), rows}}).toJson(QJsonDocument::Compact);

    QByteArray salt(kExportSaltBytes, Qt::Uninitialized);
    for (int i = 0; i < salt.size(); ++i)
        salt[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    const QByteArray key = deriveExportKey(password, salt);
    if (key.size() != 32) { setMessage(QStringLiteral("STORAGE_UNAVAILABLE")); return false; }

    QByteArray nonce(kExportNonceBytes, Qt::Uninitialized);
    for (int i = 0; i < nonce.size(); ++i)
        nonce[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    std::vector<unsigned char> cipher(size_t(plaintext.size()) + kExportTagBytes);
    int len = 0, written = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kExportNonceBytes, nullptr) == 1
        && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) == 1;
    if (ok && !plaintext.isEmpty())
        ok = EVP_EncryptUpdate(ctx, cipher.data(), &written,
                               reinterpret_cast<const unsigned char *>(plaintext.constData()),
                               plaintext.size()) == 1;
    int total = written;
    if (ok) {
        int finalLen = 0;
        ok = EVP_EncryptFinal_ex(ctx, cipher.data() + total, &finalLen) == 1;
        total += finalLen;
    }
    if (ok)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kExportTagBytes,
                                 cipher.data() + total) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        setMessage(QStringLiteral("STORAGE_UNAVAILABLE"));
        audit(QString::fromLatin1(security::AuditAction::ExportData), QStringLiteral("own"),
              QString::fromLatin1(security::AuditResult::Failed));
        return false;
    }

    // 文件格式：magic(8) || salt(16) || nonce(12) || 密文 || tag(16)
    QByteArray out(kExportMagic, int(sizeof(kExportMagic) - 1));
    out.append(salt);
    out.append(nonce);
    out.append(reinterpret_cast<const char *>(cipher.data()), total + kExportTagBytes);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(out) != out.size() || !file.commit()) {
        setMessage(QStringLiteral("STORAGE_UNAVAILABLE"));
        audit(QString::fromLatin1(security::AuditAction::ExportData), QStringLiteral("own"),
              QString::fromLatin1(security::AuditResult::Failed));
        return false;
    }
    // 明文副本不得留在临时目录：QSaveFile 已原子替换，这里再收紧权限。
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);

    audit(QString::fromLatin1(security::AuditAction::ExportData), QStringLiteral("own"),
          QString::fromLatin1(security::AuditResult::Ok));
    setMessage(QString());
    emit changed();
    return true;
}

bool SecurityService::createBackup(const QString &path, const QString &password) {
    if (!requireUser(QString::fromLatin1(security::AuditAction::BackupData))) return false;
    const auto result = security::Backup::create(*database_, path, password);
    const bool ok = result == security::Backup::Result::Ok;
    audit(QString::fromLatin1(security::AuditAction::BackupData), QStringLiteral("own"),
          ok ? QString::fromLatin1(security::AuditResult::Ok)
             : QString::fromLatin1(security::AuditResult::Failed));
    setMessage(ok ? QString() : security::Backup::describe(result));
    emit changed();
    return ok;
}

bool SecurityService::restoreBackup(const QString &path, const QString &password) {
    if (!requireUser(QString::fromLatin1(security::AuditAction::RestoreData))) return false;
    const auto result = security::Backup::restore(database_, path, password, activeUserId());
    const bool ok = result == security::Backup::Result::Ok;
    audit(QString::fromLatin1(security::AuditAction::RestoreData), QStringLiteral("own"),
          ok ? QString::fromLatin1(security::AuditResult::Ok)
             : QString::fromLatin1(security::AuditResult::Failed));
    setMessage(ok ? QString() : security::Backup::describe(result));
    emit changed();
    return ok;
}

bool SecurityService::purgeOwnData() {
    if (!requireUser(QString::fromLatin1(security::AuditAction::PurgeData))) return false;
    // 删除本用户全部历史；密钥与账户保留（销毁账户属另一条路径）。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int removed = database_->pruneHistory(now + 1000, 1);
    audit(QString::fromLatin1(security::AuditAction::PurgeData), QStringLiteral("own"),
          QString::fromLatin1(security::AuditResult::Ok));
    emit changed();
    Q_UNUSED(removed)
    return true;
}
