#include "security/Backup.h"

#include "security/UserDatabase.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>

#include <openssl/evp.h>

#include <vector>

namespace security {
namespace Backup {

namespace {

QByteArray randomBytes(int n) {
    QByteArray out(n, Qt::Uninitialized);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return out;
}

QByteArray deriveKey(const QString &password, const QByteArray &salt) {
    const QByteArray raw = password.toUtf8();
    unsigned char out[32];
    if (PKCS5_PBKDF2_HMAC(raw.constData(), raw.size(),
                          reinterpret_cast<const unsigned char *>(salt.constData()),
                          salt.size(), 600000, EVP_sha256(), 32, out) != 1)
        return {};
    return QByteArray(reinterpret_cast<const char *>(out), 32);
}

// 文件布局：magic(8) || version(2 le) || salt(16) || nonce(12) || ciphertext || tag(16)
// 明文部分是 JSON：schema / user_id / exported_at / history[]
QByteArray sealPayload(const QByteArray &plaintext, const QByteArray &key,
                       const QByteArray &nonce) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    std::vector<unsigned char> cipher(size_t(plaintext.size()) + kTagBytes);
    int len = 0, written = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) == 1
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
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagBytes, cipher.data() + total) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};
    return QByteArray(reinterpret_cast<const char *>(cipher.data()), total + kTagBytes);
}

QByteArray openPayload(const QByteArray &body, const QByteArray &key, const QByteArray &nonce) {
    if (body.size() < kTagBytes) return {};
    const int cipherLen = body.size() - kTagBytes;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    std::vector<unsigned char> plain(size_t(cipherLen) + 1);
    int len = 0, written = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) == 1
        && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) == 1;
    if (ok && cipherLen > 0)
        ok = EVP_DecryptUpdate(ctx, plain.data(), &written,
                               reinterpret_cast<const unsigned char *>(body.constData()),
                               cipherLen) == 1;
    if (ok)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagBytes,
                                 const_cast<char *>(body.constData()) + cipherLen) == 1;
    int finalLen = 0;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, plain.data() + written, &finalLen) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};
    return QByteArray(reinterpret_cast<const char *>(plain.data()), written + finalLen);
}

}  // namespace

QString describe(Result result) {
    switch (result) {
    case Result::Ok: return QStringLiteral("ok");
    case Result::BadPassword: return QStringLiteral("bad password or corrupted file");
    case Result::CorruptFile: return QStringLiteral("backup file corrupted");
    case Result::WrongUser: return QStringLiteral("backup belongs to another user");
    case Result::StorageUnavailable: return QStringLiteral("storage unavailable");
    case Result::InvalidArgument: return QStringLiteral("invalid argument");
    }
    return QStringLiteral("unknown");
}

Result create(const UserDatabase &database, const QString &path, const QString &password) {
    if (!database.isOpen()) return Result::StorageUnavailable;
    if (path.isEmpty() || password.size() < kMinPassword) return Result::InvalidArgument;

    // 一致性快照：只读查询，不锁库；导出的是已提交的聚合数据。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto series = database.historySeries(now, 365LL * 24 * 60 * 60 * 1000, 60000);
    QJsonArray rows;
    for (const auto &bucket : series) {
        rows.append(QJsonObject{{QStringLiteral("t"), double(bucket.bucketMs)},
                                {QStringLiteral("sum"), bucket.sum},
                                {QStringLiteral("low"), bucket.low},
                                {QStringLiteral("high"), bucket.high},
                                {QStringLiteral("count"), bucket.count}});
    }

    // 写入归属用户：恢复时逐字节比对，防止把 A 的备份恢复进 B 的库。
    const QJsonObject body{
        {QStringLiteral("schema"), QStringLiteral("orange-pi-backup-v1")},
        {QStringLiteral("user_id"), database.openUserId()},
        {QStringLiteral("exported_at"), double(now)},
        {QStringLiteral("history"), rows}};
    const QByteArray plaintext = QJsonDocument(body).toJson(QJsonDocument::Compact);

    const QByteArray salt = randomBytes(kSaltBytes);
    const QByteArray key = deriveKey(password, salt);
    if (key.size() != 32) return Result::StorageUnavailable;
    const QByteArray nonce = randomBytes(kNonceBytes);
    const QByteArray sealed = sealPayload(plaintext, key, nonce);
    if (sealed.isEmpty()) return Result::StorageUnavailable;

    QByteArray file;
    file.append(kFileMagic, kMagicLen);
    file.append(char(kFormatVersion & 0xff));
    file.append(char((kFormatVersion >> 8) & 0xff));
    file.append(salt);
    file.append(nonce);
    file.append(sealed);

    // 失败不覆盖：QSaveFile 原子替换。
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) return Result::StorageUnavailable;
    if (out.write(file) != file.size()) { out.cancelWriting(); return Result::StorageUnavailable; }
    if (!out.commit()) return Result::StorageUnavailable;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return Result::Ok;
}

Result restore(UserDatabase *database, const QString &path, const QString &password,
               const QString &expectedUserId) {
    if (!database || !database->isOpen()) return Result::StorageUnavailable;
    if (path.isEmpty() || password.size() < kMinPassword) return Result::InvalidArgument;
    if (expectedUserId.isEmpty()) return Result::InvalidArgument;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Result::StorageUnavailable;
    const QByteArray raw = file.readAll();
    file.close();

    const int magicLen = kMagicLen;
    const int headerLen = magicLen + 2 + kSaltBytes + kNonceBytes;
    if (raw.size() < headerLen + kTagBytes) return Result::CorruptFile;
    if (raw.left(magicLen) != QByteArray(kFileMagic, magicLen)) return Result::CorruptFile;

    const int version = quint8(raw.at(magicLen)) | (quint8(raw.at(magicLen + 1)) << 8);
    if (version != kFormatVersion) return Result::CorruptFile;

    const QByteArray salt = raw.mid(magicLen + 2, kSaltBytes);
    const QByteArray nonce = raw.mid(magicLen + 2 + kSaltBytes, kNonceBytes);
    const QByteArray body = raw.mid(headerLen);

    const QByteArray key = deriveKey(password, salt);
    if (key.size() != 32) return Result::StorageUnavailable;
    const QByteArray plaintext = openPayload(body, key, nonce);
    // tag 校验失败与密码错误不可区分（GCM 的性质），统一报 BadPassword。
    if (plaintext.isEmpty()) return Result::BadPassword;

    const QJsonObject object = QJsonDocument::fromJson(plaintext).object();
    if (object.value(QStringLiteral("schema")).toString() != QLatin1String("orange-pi-backup-v1"))
        return Result::CorruptFile;

    // 归属校验：备份里的 user_id 必须与当前会话用户一致。
    // 不一致即拒绝，不做"尽力恢复"——那会把别人的数据写进你的库。
    const QString owner = object.value(QStringLiteral("user_id")).toString();
    if (owner != expectedUserId) return Result::WrongUser;

    const QJsonArray rows = object.value(QStringLiteral("history")).toArray();
    int restored = 0;
    for (const QJsonValue &value : rows) {
        const QJsonObject row = value.toObject();
        UserDatabase::HistoryBucket bucket;
        bucket.bucketMs = qint64(row.value(QStringLiteral("t")).toDouble());
        bucket.sum = row.value(QStringLiteral("sum")).toDouble();
        bucket.low = row.value(QStringLiteral("low")).toDouble();
        bucket.high = row.value(QStringLiteral("high")).toDouble();
        bucket.count = row.value(QStringLiteral("count")).toInt();
        if (bucket.bucketMs <= 0 || bucket.count <= 0) continue;
        // 保留原始聚合（sum/low/high/count），不重新采样。
        if (database->restoreHistoryBucket(bucket)) ++restored;
    }
    return restored > 0 || rows.isEmpty() ? Result::Ok : Result::CorruptFile;
}

}  // namespace Backup
}  // namespace security
