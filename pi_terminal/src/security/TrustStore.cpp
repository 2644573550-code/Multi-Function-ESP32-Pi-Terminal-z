#include "security/TrustStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
constexpr int kPublicKeyBytes = 65;   // 未压缩点：0x04 || X(32) || Y(32)
constexpr int kFingerprintBytes = 8;  // SHA-256 前 8 字节
constexpr char kSchema[] = "pbr-trust-v1";
constexpr auto kFileMode = QFile::ReadOwner | QFile::WriteOwner;

QString defaultPath()
{
    // config 位置而非 data：刷卡的用途是选用户，发生在登录之前。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/pbr_trust.json");
}

void setError(QString *error, const QString &text)
{
    if (error) *error = text;
}
} // namespace

TrustStore::TrustStore(QString path)
    : path_(path.isEmpty() ? defaultPath() : std::move(path))
{
}

TrustStore::~TrustStore() = default;

bool TrustStore::exists() const
{
    return QFile::exists(path_);
}

bool TrustStore::healthy() const
{
    // ensureLoaded 会把解析/校验失败记到 corrupted_，这里只做一次读取，
    // 不因为"文件不存在"就报不健康（新机器本来就没有这个文件）。
    ensureLoaded(nullptr);
    return !corrupted_;
}

bool TrustStore::isValidPublicKey(const QByteArray &publicKey)
{
    return publicKey.size() == kPublicKeyBytes && quint8(publicKey.at(0)) == 0x04;
}

bool TrustStore::isValidBridgeId(const QString &bridgeId)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return pattern.match(bridgeId).hasMatch();
}

QByteArray TrustStore::fingerprintOf(const QByteArray &publicKey)
{
    if (!isValidPublicKey(publicKey)) return {};
    return QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256).left(kFingerprintBytes);
}

quint8 TrustStore::keyIdOf(const QByteArray &publicKey)
{
    const QByteArray fingerprint = fingerprintOf(publicKey);
    return fingerprint.isEmpty() ? 0 : quint8(fingerprint.at(0));
}

QString TrustStore::keyIdText(quint8 keyId)
{
    return QStringLiteral("%1").arg(keyId, 2, 16, QLatin1Char('0'));
}

bool TrustStore::ensureLoaded(QString *error) const
{
    if (loaded_) return true;
    if (corrupted_) {
        // 库文件损坏时**失败关闭**：不当作空库，否则一次损坏就等于清空信任。
        setError(error, QStringLiteral("trust store corrupted"));
        return false;
    }

    records_.clear();
    QFile file(path_);
    if (!file.exists()) {
        loaded_ = true;
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return false;
    }

    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        corrupted_ = true;
        setError(error, QStringLiteral("trust store is not valid JSON"));
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String(kSchema)) {
        corrupted_ = true;
        setError(error, QStringLiteral("unsupported trust store schema"));
        return false;
    }

    // `records` 存在但不是数组 → 类型损坏。**不能**当成空库：那等于
    // 一次篡改就静默清空信任。`records` 缺失视为空库（新文件即如此写法）。
    const QJsonValue recordsValue = root.value(QStringLiteral("records"));
    if (!recordsValue.isUndefined() && !recordsValue.isArray()) {
        corrupted_ = true;
        setError(error, QStringLiteral("trust store records field is not an array"));
        return false;
    }
    const QJsonArray array = recordsValue.toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Record record;
        record.bridgeId = object.value(QStringLiteral("bridge_id")).toString();
        record.publicKey = QByteArray::fromHex(object.value(QStringLiteral("public_key")).toString().toLatin1());
        record.enrolledAt = qint64(object.value(QStringLiteral("enrolled_at")).toDouble());
        record.revokedAt = qint64(object.value(QStringLiteral("revoked_at")).toDouble());
        // 只信任自己能算出来的指纹；文件里的 fingerprint 字段仅作交叉校验。
        if (!isValidPublicKey(record.publicKey)) {
            corrupted_ = true;
            setError(error, QStringLiteral("trust store holds an invalid public key"));
            return false;
        }
        record.fingerprint = fingerprintOf(record.publicKey);
        const QByteArray stored = QByteArray::fromHex(object.value(QStringLiteral("fingerprint")).toString().toLatin1());
        if (!stored.isEmpty() && stored != record.fingerprint) {
            corrupted_ = true;
            setError(error, QStringLiteral("trust store fingerprint mismatch"));
            return false;
        }
        records_.append(record);
    }
    loaded_ = true;
    return true;
}

bool TrustStore::reload(QString *error)
{
    loaded_ = false;
    corrupted_ = false;
    records_.clear();
    return ensureLoaded(error);
}

bool TrustStore::save(QString *error)
{
    const QFileInfo info(path_);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, QStringLiteral("cannot create trust store directory"));
        return false;
    }

    QJsonArray array;
    for (const Record &record : std::as_const(records_)) {
        QJsonObject object;
        object.insert(QStringLiteral("bridge_id"), record.bridgeId);
        object.insert(QStringLiteral("public_key"), QString::fromLatin1(record.publicKey.toHex()));
        object.insert(QStringLiteral("fingerprint"), QString::fromLatin1(record.fingerprint.toHex()));
        object.insert(QStringLiteral("enrolled_at"), double(record.enrolledAt));
        object.insert(QStringLiteral("revoked_at"), double(record.revokedAt));
        array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QLatin1String(kSchema));
    root.insert(QStringLiteral("records"), array);

    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    // 私钥不在这里，但身份公钥与配对关系仍属敏感元数据，一律 0600。
    QFile::setPermissions(path_, kFileMode);
    return true;
}

QVector<TrustStore::Record> TrustStore::lookupIdentity(quint8 keyId) const
{
    QVector<Record> matches;
    if (!ensureLoaded(nullptr)) return matches;
    for (const Record &record : records_) {
        if (record.active() && record.keyId() == keyId) matches.append(record);
    }
    return matches;
}

std::optional<TrustStore::Record> TrustStore::findActive(const QByteArray &publicKey) const
{
    if (!ensureLoaded(nullptr)) return std::nullopt;
    for (const Record &record : records_) {
        if (record.active() && record.publicKey == publicKey) return record;
    }
    return std::nullopt;
}

QVector<TrustStore::Record> TrustStore::all() const
{
    if (!ensureLoaded(nullptr)) return {};
    return records_;
}

bool TrustStore::hasActiveIdentity() const
{
    if (!ensureLoaded(nullptr)) return false;
    for (const Record &record : records_) {
        if (record.active()) return true;
    }
    return false;
}

bool TrustStore::enrollIdentity(const QString &bridgeId, const QByteArray &publicKey, QString *error)
{
    if (!isValidPublicKey(publicKey)) {
        setError(error, QStringLiteral("public key must be 65 bytes and start with 0x04"));
        return false;
    }
    if (!isValidBridgeId(bridgeId)) {
        setError(error, QStringLiteral("bridge_id must be 1..64 of [A-Za-z0-9_-]"));
        return false;
    }
    if (!ensureLoaded(error)) return false;

    const QByteArray fingerprint = fingerprintOf(publicKey);
    for (const Record &record : std::as_const(records_)) {
        if (record.fingerprint != fingerprint) continue;
        if (record.active()) {
            // §6.7：已有记录时不覆盖。换板必须先 revoke。
            setError(error, QStringLiteral("identity already enrolled; revoke before re-enrolling"));
            return false;
        }
    }

    Record record;
    record.bridgeId = bridgeId;
    record.publicKey = publicKey;
    record.fingerprint = fingerprint;
    record.enrolledAt = QDateTime::currentMSecsSinceEpoch();
    records_.append(record);
    return save(error);
}

bool TrustStore::revokeIdentity(const QByteArray &publicKey, QString *error)
{
    const QByteArray fingerprint = fingerprintOf(publicKey);
    if (fingerprint.isEmpty()) {
        setError(error, QStringLiteral("public key must be 65 bytes and start with 0x04"));
        return false;
    }
    return revokeByFingerprint(fingerprint, error);
}

bool TrustStore::revokeByFingerprint(const QByteArray &fingerprint, QString *error)
{
    if (fingerprint.size() != kFingerprintBytes) {
        setError(error, QStringLiteral("fingerprint must be 8 bytes"));
        return false;
    }
    if (!ensureLoaded(error)) return false;

    bool found = false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (Record &record : records_) {
        if (record.fingerprint == fingerprint && record.active()) {
            record.revokedAt = now;
            found = true;
        }
    }
    if (!found) {
        setError(error, QStringLiteral("identity not found or already revoked"));
        return false;
    }
    return save(error);
}
