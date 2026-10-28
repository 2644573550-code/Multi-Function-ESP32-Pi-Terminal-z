#include "security/RecoveryCode.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <openssl/evp.h>

#include <vector>

namespace security {
namespace RecoveryCode {

namespace {

// Base32 字母表：去掉 0/O/1/I 四个易混淆字符，共 28 个。
constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr int kAlphabetSize = 28;

}  // namespace

QString normalize(const QString &code) {
    QString out;
    out.reserve(code.size());
    for (const QChar c : code) {
        if (c == QLatin1Char(' ') || c == QLatin1Char('-')) continue;
        out.append(c.toUpper());
    }
    return out;
}

QString generate() {
    QString raw;
    raw.reserve(kChars);
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < kChars; ++i) raw.append(QLatin1Char(kAlphabet[rng->bounded(kAlphabetSize)]));

    // 分组输出，便于人工抄写与核对。
    QString formatted;
    for (int group = 0; group < kGroups; ++group) {
        if (group > 0) formatted.append(QLatin1Char('-'));
        formatted.append(raw.mid(group * kGroupSize, kGroupSize));
    }
    return formatted;
}

bool isValidFormat(const QString &code) {
    const QString normalized = normalize(code);
    if (normalized.size() != kChars) return false;
    for (const QChar c : normalized) {
        if (!QLatin1String(kAlphabet).contains(c)) return false;
    }
    return true;
}

QByteArray deriveKey(const QString &code, const QByteArray &salt) {
    if (!isValidFormat(code) || salt.size() != kSaltBytes) return {};
    const QByteArray normalized = normalize(code).toLatin1();
    unsigned char out[kKeyBytes];
    // 恢复码本身有约 120 bit 熵，迭代数可以比 6 位 PIN 低，但仍取同一档，
    // 避免有人误以为"恢复码可以弱一点"。
    if (PKCS5_PBKDF2_HMAC(normalized.constData(), normalized.size(),
                          reinterpret_cast<const unsigned char *>(salt.constData()),
                          salt.size(), 600000, EVP_sha256(), kKeyBytes, out) != 1)
        return {};
    return QByteArray(reinterpret_cast<const char *>(out), kKeyBytes);
}

QByteArray randomSalt() {
    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    for (int i = 0; i < salt.size(); ++i)
        salt[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return salt;
}

QByteArray wrap(const QByteArray &dbKey, const QByteArray &rek, QByteArray *nonce) {
    if (dbKey.size() != kKeyBytes || rek.size() != kKeyBytes || nonce == nullptr) return {};
    *nonce = QByteArray(kNonceBytes, Qt::Uninitialized);
    for (int i = 0; i < nonce->size(); ++i)
        (*nonce)[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    std::vector<unsigned char> cipher(size_t(dbKey.size()) + 16);
    int len = 0, written = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) == 1
        && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(rek.constData()),
                              reinterpret_cast<const unsigned char *>(nonce->constData())) == 1
        && EVP_EncryptUpdate(ctx, cipher.data(), &written,
                             reinterpret_cast<const unsigned char *>(dbKey.constData()),
                             dbKey.size()) == 1;
    int total = written;
    if (ok) {
        int finalLen = 0;
        ok = EVP_EncryptFinal_ex(ctx, cipher.data() + total, &finalLen) == 1;
        total += finalLen;
    }
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, cipher.data() + total) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) { nonce->clear(); return {}; }
    return QByteArray(reinterpret_cast<const char *>(cipher.data()), total + 16);
}

QByteArray unwrap(const QByteArray &wrapped, const QByteArray &rek, const QByteArray &nonce) {
    if (wrapped.size() < 16 || rek.size() != kKeyBytes || nonce.size() != kNonceBytes) return {};
    const int cipherLen = wrapped.size() - 16;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    std::vector<unsigned char> plain(size_t(cipherLen) + 1);
    int len = 0, written = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) == 1
        && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(rek.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) == 1
        && EVP_DecryptUpdate(ctx, plain.data(), &written,
                             reinterpret_cast<const unsigned char *>(wrapped.constData()),
                             cipherLen) == 1;
    if (ok)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                                 const_cast<char *>(wrapped.constData()) + cipherLen) == 1;
    int finalLen = 0;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, plain.data() + written, &finalLen) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};
    return QByteArray(reinterpret_cast<const char *>(plain.data()), written + finalLen);
}

}  // namespace RecoveryCode
}  // namespace security
