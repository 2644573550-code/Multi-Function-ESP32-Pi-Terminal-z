#include "security/KeyWrap.h"

#include <openssl/evp.h>

#include <QRandomGenerator>

namespace KeyWrap {

QByteArray seal(const QByteArray &kek, const QByteArray &dbKey, QByteArray *nonceOut) {
    if (kek.size() != kKeyBytes || dbKey.size() != kKeyBytes || !nonceOut) return {};

    QByteArray nonce(kNonceBytes, Qt::Uninitialized);
    for (int i = 0; i < kNonceBytes; ++i) nonce[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    QByteArray sealed(dbKey.size() + kTagBytes, Qt::Uninitialized);
    int len = 0, total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) == 1
        && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(kek.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) == 1
        && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(sealed.data()), &len,
                             reinterpret_cast<const unsigned char *>(dbKey.constData()), dbKey.size()) == 1;
    if (ok) {
        total = len;
        ok = EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(sealed.data()) + total, &len) == 1;
        if (ok) total += len;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagBytes,
                                 reinterpret_cast<unsigned char *>(sealed.data()) + total) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok || total != dbKey.size()) return {};
    *nonceOut = nonce;
    return sealed;
}

QByteArray open(const QByteArray &kek, const QByteArray &sealed, const QByteArray &nonce) {
    if (kek.size() != kKeyBytes || nonce.size() != kNonceBytes) return {};
    if (sealed.size() != kKeyBytes + kTagBytes) return {};

    const int cipherBytes = sealed.size() - kTagBytes;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    QByteArray out(kKeyBytes, Qt::Uninitialized);
    int len = 0, total = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) == 1
        && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(kek.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) == 1
        && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &len,
                             reinterpret_cast<const unsigned char *>(sealed.constData()), cipherBytes) == 1;
    if (ok) {
        total = len;
        // tag 必须在 Update 之后设置。
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagBytes,
                                 const_cast<char *>(sealed.constData()) + cipherBytes) == 1;
    }
    if (ok) {
        // 认证失败在此返回 0：调用方拿不到任何 key 字节。
        ok = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + total, &len) == 1;
        if (ok) total += len;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok || total != kKeyBytes) return {};
    return out;
}

}  // namespace KeyWrap
