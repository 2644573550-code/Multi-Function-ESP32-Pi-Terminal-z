#include "pbr_handshake.h"

#include <QCryptographicHash>

#include <openssl/evp.h>
#include <openssl/kdf.h>

namespace PbrHandshake {

namespace {

// RFC 5869 HKDF-SHA256（extract + expand）。OpenSSL 3 用 EVP_KDF 接口。
QByteArray hkdfSha256(const QByteArray &ikm, const QByteArray &salt,
                      const QByteArray &info, int length) {
    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf) return {};
    EVP_KDF_CTX *ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) return {};

    OSSL_PARAM params[5];
    int index = 0;
    params[index++] = OSSL_PARAM_construct_utf8_string("digest", const_cast<char *>("SHA256"), 0);
    // HKDF 的 key 即 IKM，salt 单独传。
    params[index++] = OSSL_PARAM_construct_octet_string(
        "key", const_cast<char *>(ikm.constData()), size_t(ikm.size()));
    if (!salt.isEmpty())
        params[index++] = OSSL_PARAM_construct_octet_string(
            "salt", const_cast<char *>(salt.constData()), size_t(salt.size()));
    if (!info.isEmpty())
        params[index++] = OSSL_PARAM_construct_octet_string(
            "info", const_cast<char *>(info.constData()), size_t(info.size()));
    params[index] = OSSL_PARAM_construct_end();

    QByteArray out(length, '\0');
    const bool ok = EVP_KDF_derive(ctx, reinterpret_cast<unsigned char *>(out.data()),
                                   size_t(length), params) == 1;
    EVP_KDF_CTX_free(ctx);
    if (!ok) return {};
    return out;
}

}  // namespace

QByteArray sharedSecret(const QByteArray &privateKey, const QByteArray &peerPublicKey) {
    if (privateKey.size() != kKeyBytes || peerPublicKey.size() != kKeyBytes) return {};

    EVP_PKEY *self = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
        reinterpret_cast<const unsigned char *>(privateKey.constData()), size_t(kKeyBytes));
    EVP_PKEY *peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
        reinterpret_cast<const unsigned char *>(peerPublicKey.constData()), size_t(kKeyBytes));
    if (!self || !peer) {
        EVP_PKEY_free(self);
        EVP_PKEY_free(peer);
        return {};
    }

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(self, nullptr);
    QByteArray shared(kKeyBytes, '\0');
    size_t length = size_t(kKeyBytes);
    bool ok = ctx
        && EVP_PKEY_derive_init(ctx) == 1
        && EVP_PKEY_derive_set_peer(ctx, peer) == 1
        && EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char *>(shared.data()), &length) == 1;
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(self);
    EVP_PKEY_free(peer);

    if (!ok || length != size_t(kKeyBytes)) return {};
    // §4：全零表示对方公钥是低阶点，必须断连且不解释原因。
    bool allZero = true;
    for (const char c : shared) if (c != 0) { allZero = false; break; }
    if (allZero) return {};
    return shared;
}

QByteArray publicKeyOf(const QByteArray &privateKey) {
    if (privateKey.size() != kKeyBytes) return {};
    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
        reinterpret_cast<const unsigned char *>(privateKey.constData()), size_t(kKeyBytes));
    if (!key) return {};
    QByteArray out(kKeyBytes, '\0');
    size_t length = size_t(kKeyBytes);
    const bool ok = EVP_PKEY_get_raw_public_key(
        key, reinterpret_cast<unsigned char *>(out.data()), &length) == 1;
    EVP_PKEY_free(key);
    return ok ? out : QByteArray();
}

Keys deriveKeys(const QByteArray &shared32, const QByteArray &transcriptT3) {
    Keys keys;
    if (shared32.size() != kKeyBytes) return keys;

    const QByteArray transcriptHash = QCryptographicHash::hash(transcriptT3, QCryptographicHash::Sha256);
    const QByteArray ks = hkdfSha256(shared32, transcriptHash,
                                     QByteArrayLiteral("pbr-secure-v1"), 96);
    if (ks.size() != 96) return keys;

    keys.handshake = ks.mid(0, 32);
    keys.piToBridge = ks.mid(32, 32);
    keys.bridgeToPi = ks.mid(64, 32);
    // session_id 与 nonce_prefix 是两个字段，不能混用（修订 1 的错误）。
    keys.sessionId = transcriptHash.mid(0, kSessionIdBytes);
    keys.noncePrefix = transcriptHash.mid(kSessionIdBytes, kNoncePrefixBytes);
    return keys;
}

bool constantTimeEquals(const QByteArray &a, const QByteArray &b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

}  // namespace PbrHandshake
