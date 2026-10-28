#include "security/UserService.h"
#include "security/RecoveryCode.h"

#include "security/KeyWrap.h"
#include "security/PinPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

namespace security {


namespace {

QString defaultRegistryPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/users.json");
}

QString newUserId() {
    QByteArray raw(16, Qt::Uninitialized);
    for (int i = 0; i < raw.size(); ++i) raw[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    // 不透明 ID：不含用户名，也不表达创建顺序。
    return QString::fromLatin1(raw.toHex());
}

QByteArray newDbKey() {
    QByteArray key(KeyWrap::kKeyBytes, Qt::Uninitialized);
    for (int i = 0; i < key.size(); ++i) key[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return key;
}

QJsonObject toJson(const UserRecord &record) {
    return {{"user_id", record.userId},
            {"display_name", record.displayName},
            {"admin", record.admin},
            {"disabled", record.disabled},
            {"salt", QString::fromLatin1(record.salt.toBase64())},
            {"verifier", QString::fromLatin1(record.verifier.toBase64())},
            {"wrapped_db_key", QString::fromLatin1(record.wrappedDbKey.toBase64())},
            {"db_key_nonce", QString::fromLatin1(record.dbKeyNonce.toBase64())},
            {"iterations", record.iterations},
            // 恢复路径：只存 REK 封装后的密文，永不存恢复码本身。
            {"recovery_salt", QString::fromLatin1(record.recoverySalt.toBase64())},
            {"wrapped_db_key_by_code",
             QString::fromLatin1(record.wrappedDbKeyByCode.toBase64())},
            {"db_key_by_code_nonce", QString::fromLatin1(record.dbKeyByCodeNonce.toBase64())},
            {"has_recovery_code", record.hasRecoveryCode}};
}

UserRecord fromJson(const QJsonObject &object) {
    UserRecord record;
    record.userId = object.value("user_id").toString();
    record.displayName = object.value("display_name").toString();
    record.admin = object.value("admin").toBool();
    record.disabled = object.value("disabled").toBool();
    record.salt = QByteArray::fromBase64(object.value("salt").toString().toLatin1());
    record.verifier = QByteArray::fromBase64(object.value("verifier").toString().toLatin1());
    record.wrappedDbKey = QByteArray::fromBase64(object.value("wrapped_db_key").toString().toLatin1());
    record.dbKeyNonce = QByteArray::fromBase64(object.value("db_key_nonce").toString().toLatin1());
    record.iterations = object.value("iterations").toInt();
    record.recoverySalt = QByteArray::fromBase64(object.value("recovery_salt").toString().toLatin1());
    record.wrappedDbKeyByCode = QByteArray::fromBase64(
        object.value("wrapped_db_key_by_code").toString().toLatin1());
    record.dbKeyByCodeNonce = QByteArray::fromBase64(
        object.value("db_key_by_code_nonce").toString().toLatin1());
    record.hasRecoveryCode = object.value("has_recovery_code").toBool();
    return record;
}

}  // namespace

UserService::UserService(QString registryPath)
    : registryPath_(registryPath.isEmpty() ? defaultRegistryPath() : std::move(registryPath)) {}

QString UserService::path() const { return registryPath_; }

QList<UserRecord> UserService::load() const {
    QFile file(registryPath_);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QList<UserRecord> records;
    for (const QJsonValue &value : document.object().value("users").toArray()) {
        UserRecord record = fromJson(value.toObject());
        // 缺字段的记录直接丢弃，不做猜测修复。
        if (record.userId.isEmpty() || record.salt.size() != PinPolicy::kSaltBytes
            || record.verifier.isEmpty() || record.iterations < 1)
            continue;
        records.append(record);
    }
    return records;
}

bool UserService::save(const QList<UserRecord> &records) const {
    QJsonArray users;
    for (const UserRecord &record : records) users.append(toJson(record));
    const QJsonDocument document(QJsonObject{{"schema", "p0-users-v1"}, {"users", users}});

    QDir().mkpath(QFileInfo(registryPath_).absolutePath());
    QSaveFile file(registryPath_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return false;
    if (file.write(document.toJson(QJsonDocument::Compact)) < 0) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) return false;
    QFile::setPermissions(registryPath_, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool UserService::hasAnyUser() const { return !load().isEmpty(); }

QString UserService::createInitialAdmin(const QString &displayName, const QString &pin,
                                        const QString &recoveryCode) {
    // 已有任何用户就拒绝，且不覆盖 —— 否则删文件即可重建管理员。
    if (hasAnyUser()) return {};
    return createUser({}, displayName, pin, true, recoveryCode);
}

QString UserService::createUser(const QString &actorUserId, const QString &displayName,
                                const QString &pin, bool admin, const QString &recoveryCode) {
    QList<UserRecord> records = load();
    // actorUserId 为空只允许"首次初始化"路径；其余必须由管理员发起。
    if (!records.isEmpty()) {
        if (actorUserId.isEmpty()) return {};
        bool actorIsAdmin = false;
        for (const UserRecord &record : records) {
            if (record.userId == actorUserId) actorIsAdmin = record.admin && !record.disabled;
        }
        if (!actorIsAdmin) return {};
    }
    if (displayName.trimmed().isEmpty() || !PinPolicy::validPin(pin)) return {};

    UserRecord record;
    record.userId = newUserId();
    record.displayName = displayName.trimmed();
    record.admin = admin;
    record.iterations = PinPolicy::kProductionIterations;
    record.salt = PinPolicy::salt16();
    const QByteArray kek = PinPolicy::deriveKek(pin, record.salt, record.iterations);
    if (kek.isEmpty()) return {};
    record.verifier = PinPolicy::verifyValue(kek);
    const QByteArray dbKey = newDbKey();
    record.wrappedDbKey = KeyWrap::seal(kek, dbKey, &record.dbKeyNonce);
    if (record.wrappedDbKey.isEmpty()) return {};

    // 恢复路径：与 PIN 完全独立的第二层封装。传空则明确标记"无恢复路径"，
    // 由界面提示用户"丢 PIN 即丢数据"，不假装已可恢复。
    if (!recoveryCode.isEmpty()) {
        if (!RecoveryCode::isValidFormat(recoveryCode)) return {};
        record.recoverySalt = RecoveryCode::randomSalt();
        const QByteArray rek = RecoveryCode::deriveKey(recoveryCode, record.recoverySalt);
        if (rek.size() != 32) return {};
        record.wrappedDbKeyByCode = RecoveryCode::wrap(dbKey, rek, &record.dbKeyByCodeNonce);
        if (record.wrappedDbKeyByCode.isEmpty()) return {};
        record.hasRecoveryCode = true;
    }

    records.append(record);
    if (!save(records)) return {};
    return record.userId;
}

bool UserService::openWithRecoveryCode(const QString &userId, const QString &recoveryCode,
                                        QByteArray *dbKeyOut) const {
    if (!dbKeyOut) return false;
    UserRecord record;
    if (!lookup(userId, &record) || record.disabled) return false;
    if (!record.hasRecoveryCode || record.recoverySalt.isEmpty()) return false;
    if (!RecoveryCode::isValidFormat(recoveryCode)) return false;

    const QByteArray rek = RecoveryCode::deriveKey(recoveryCode, record.recoverySalt);
    if (rek.size() != 32) return false;
    const QByteArray dbKey = RecoveryCode::unwrap(record.wrappedDbKeyByCode, rek,
                                                  record.dbKeyByCodeNonce);
    if (dbKey.size() != 32) return false;   // 恢复码错或密文损坏
    *dbKeyOut = dbKey;
    return true;
}

bool UserService::setRecoveryCode(const QString &userId, const QByteArray &dbKey,
                                  const QString &recoveryCode) {
    if (dbKey.size() != 32 || !RecoveryCode::isValidFormat(recoveryCode)) return false;
    QList<UserRecord> records = load();
    for (UserRecord &record : records) {
        if (record.userId != userId) continue;
        record.recoverySalt = RecoveryCode::randomSalt();
        const QByteArray rek = RecoveryCode::deriveKey(recoveryCode, record.recoverySalt);
        if (rek.size() != 32) return false;
        record.wrappedDbKeyByCode = RecoveryCode::wrap(dbKey, rek, &record.dbKeyByCodeNonce);
        if (record.wrappedDbKeyByCode.isEmpty()) return false;
        record.hasRecoveryCode = true;
        return save(records);
    }
    return false;
}

bool UserService::hasRecoveryCode(const QString &userId) const {
    UserRecord record;
    return lookup(userId, &record) && record.hasRecoveryCode;
}

bool UserService::disableUser(const QString &actorUserId, const QString &targetUserId) {
    QList<UserRecord> records = load();
    bool actorIsAdmin = false;
    for (const UserRecord &record : records) {
        if (record.userId == actorUserId) actorIsAdmin = record.admin && !record.disabled;
    }
    if (!actorIsAdmin) return false;
    for (UserRecord &record : records) {
        if (record.userId != targetUserId) continue;
        if (record.userId == actorUserId) return false;  // 不得停用自己
        record.disabled = true;
        return save(records);
    }
    return false;
}

QList<UserService::PublicUser> UserService::listUsers() const {
    QList<PublicUser> users;
    for (const UserRecord &record : load()) {
        users.append({record.userId, record.displayName, record.admin, record.disabled});
    }
    return users;
}

bool UserService::lookup(const QString &userId, UserRecord *out) const {
    if (!out) return false;
    for (const UserRecord &record : load()) {
        if (record.userId != userId) continue;
        *out = record;
        return true;
    }
    return false;
}

bool UserService::replaceRecord(const UserRecord &record) {
    QList<UserRecord> records = load();
    for (UserRecord &existing : records) {
        if (existing.userId != record.userId) continue;
        existing = record;
        return save(records);
    }
    return false;
}

}  // namespace security
