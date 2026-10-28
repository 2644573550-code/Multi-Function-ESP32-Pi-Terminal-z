#include "pbr_aead.h"

#include <QtEndian>

#include <openssl/evp.h>

#include <vector>

namespace PbrAead {

namespace {

void setResult(PbrAead::Result *out, Result value) {
    if (out) *out = value;
}

QByteArray nonceOf(const QByteArray &payload) {
    return payload.mid(1 + kKeyIdBytes, kNonceBytes);
}

}  // namespace

QByteArray buildAad(const Aad &aad) {
    QByteArray out;
    out.reserve(10);
    out.append(char(aad.envVer));
    out.append(char(aad.keyId));
    out.append(char(aad.kind));
    out.append(char(aad.dir));
    // auth_seq 只取低 32 位：高 32 位已由 nonce 覆盖，AAD 保持定长。
    QByteArray seq(4, '\0');
    qToLittleEndian<quint32>(quint32(aad.authSeq & 0xffffffffu),
                             reinterpret_cast<uchar *>(seq.data()));
    out.append(seq);
    QByteArray len(2, '\0');
    qToLittleEndian<quint16>(aad.plaintextLen, reinterpret_cast<uchar *>(len.data()));
    out.append(len);
    return out;
}

QByteArray buildNonce(const QByteArray &noncePrefix4, quint64 authSeq) {
    if (noncePrefix4.size() != 4) return {};
    QByteArray nonce(12, '\0');
    nonce.replace(0, 4, noncePrefix4);
    qToLittleEndian<quint64>(authSeq, reinterpret_cast<uchar *>(nonce.data()) + 4);
    return nonce;
}

QByteArray seal(const QByteArray &plaintext, const QByteArray &key32,
                const QByteArray &noncePrefix4, quint64 authSeq,
                quint8 keyId, quint8 kind, quint8 dir, Result *result) {
    if (plaintext.size() > kMaxPlaintext) { setResult(result, Result::TooLarge); return {}; }
    if (key32.size() != 32 || noncePrefix4.size() != 4) {
        setResult(result, Result::Malformed);
        return {};
    }
    // 禁止环绕：调用方必须在耗尽前重新握手。
    if (authSeq == 0 || authSeq == 0xffffffffffffffffULL) {
        setResult(result, Result::Malformed);
        return {};
    }

    const QByteArray nonce = buildNonce(noncePrefix4, authSeq);
    const QByteArray aad = buildAad(Aad{kEnvVersion, keyId, kind, dir, authSeq,
                                        quint16(plaintext.size())});

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { setResult(result, Result::Malformed); return {}; }

    std::vector<unsigned char> cipher(size_t(plaintext.size()) + kTagBytes);
    int len = 0;
    int written = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, kNonceBytes, nullptr) == 1
        && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(key32.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) == 1
        && EVP_EncryptUpdate(ctx, nullptr, &len,
                             reinterpret_cast<const unsigned char *>(aad.constData()),
                             aad.size()) == 1
        && (len == 0 || true);
    if (ok && !plaintext.isEmpty()) {
        ok = EVP_EncryptUpdate(ctx, cipher.data(), &written,
                               reinterpret_cast<const unsigned char *>(plaintext.constData()),
                               plaintext.size()) == 1;
    }
    int total = written;
    if (ok) {
        int finalLen = 0;
        ok = EVP_EncryptFinal_ex(ctx, cipher.data() + total, &finalLen) == 1;
        total += finalLen;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, kTagBytes,
                                 cipher.data() + total) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) { setResult(result, Result::Malformed); return {}; }

    QByteArray payload;
    payload.reserve(kOverhead + plaintext.size());
    payload.append(char(kEnvVersion));
    payload.append(char(keyId));
    payload.append(nonce);
    payload.append(reinterpret_cast<const char *>(cipher.data()), total + kTagBytes);
    if (payload.size() > kMaxFramePayload) {
        setResult(result, Result::TooLarge);
        return {};
    }
    setResult(result, Result::Ok);
    return payload;
}

bool parseEnvelope(const QByteArray &payload, Envelope *out, quint8 *envVersion,
                   Aad *aad, Result *result) {
    if (payload.size() < kOverhead) { setResult(result, Result::Malformed); return false; }
    if (payload.size() > kMaxFramePayload) { setResult(result, Result::TooLarge); return false; }

    const quint8 version = quint8(payload.at(0));
    if (envVersion) *envVersion = version;
    if (version != kEnvVersion) { setResult(result, Result::UnknownVersion); return false; }

    const quint8 keyId = quint8(payload.at(1));
    const QByteArray nonce = nonceOf(payload);
    const quint64 authSeq = qFromLittleEndian<quint64>(
        reinterpret_cast<const uchar *>(nonce.constData()) + 4);

    if (out) {
        out->keyId = keyId;
        out->authSeq = authSeq;
        out->ciphertext = payload.mid(kHeaderBytes);
    }
    if (aad) {
        // plaintext 长度 = 密文长度 - tag。
        const int plainLen = payload.size() - kHeaderBytes - kTagBytes;
        aad->envVer = version;
        aad->keyId = keyId;
        aad->authSeq = authSeq;
        aad->plaintextLen = quint16(plainLen);
    }
    setResult(result, Result::Ok);
    return true;
}

QByteArray open(const QByteArray &payload, const QByteArray &key32, Aad aad,
                Result *result) {
    Envelope envelope;
    if (!parseEnvelope(payload, &envelope, nullptr, nullptr, result)) return {};
    if (key32.size() != 32) { setResult(result, Result::Malformed); return {}; }

    const int cipherLen = envelope.ciphertext.size() - kTagBytes;
    if (cipherLen < 0) { setResult(result, Result::Malformed); return {}; }

    // AAD 的 auth_seq/key_id 必须与信封一致：调用方即使传错也必须验不过。
    if (aad.authSeq != envelope.authSeq || aad.keyId != envelope.keyId) {
        setResult(result, Result::AuthFailed);
        return {};
    }
    aad.plaintextLen = quint16(cipherLen);
    const QByteArray aadBytes = buildAad(aad);
    const QByteArray nonce = nonceOf(payload);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { setResult(result, Result::Malformed); return {}; }

    std::vector<unsigned char> plain(size_t(cipherLen) + kTagBytes + 1);
    int len = 0;
    int written = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, kNonceBytes, nullptr) == 1
        && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(key32.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) == 1
        && EVP_DecryptUpdate(ctx, nullptr, &len,
                             reinterpret_cast<const unsigned char *>(aadBytes.constData()),
                             aadBytes.size()) == 1;
    if (ok && cipherLen > 0) {
        ok = EVP_DecryptUpdate(ctx, plain.data(), &written,
                               reinterpret_cast<const unsigned char *>(envelope.ciphertext.constData()),
                               cipherLen) == 1;
    }
    if (ok) {
        // tag 必须作为最后一个参数传入，否则 set_tag 失败 → 验签必然失败。
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, kTagBytes,
                                 const_cast<char *>(envelope.ciphertext.constData()) + cipherLen) == 1;
    }
    int finalLen = 0;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, plain.data() + written, &finalLen) == 1;
    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        setResult(result, Result::AuthFailed);
        return {};
    }
    setResult(result, Result::Ok);
    return QByteArray(reinterpret_cast<const char *>(plain.data()), written + finalLen);
}

bool sharedIsUsable(const QByteArray &shared32) {
    if (shared32.size() != 32) return false;
    // 全零表示对方公钥是低阶点（RFC 7748 §6.1）。
    for (const char c : shared32)
        if (c != 0) return true;
    return false;
}

}  // namespace PbrAead
