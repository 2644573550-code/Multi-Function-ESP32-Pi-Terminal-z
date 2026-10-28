#pragma once

#include <QByteArray>
#include <QString>

// PBR secure v1 的 AEAD 信封（§5）。
//
// 帧头不动，认证的是 payload：
//   payload = env_ver(1) || key_id(1) || nonce(12) || ciphertext(N) || tag(16)
//   nonce   = nonce_prefix(4) || auth_seq_u64le(8)
//   AAD     = env_ver || key_id || kind || dir || auth_seq_u32le || plaintext_len_u16le
//
// 载荷是**二进制**，不是文本；实现里全部按长度处理，不做字符串假设。
namespace PbrAead {

constexpr int kEnvVersion = 1;
constexpr int kKeyIdBytes = 1;
constexpr int kNonceBytes = 12;
constexpr int kTagBytes = 16;
constexpr int kHeaderBytes = 1 + kKeyIdBytes + kNonceBytes;   // 14
constexpr int kOverhead = kHeaderBytes + kTagBytes;            // 30
constexpr int kMaxPlaintext = 2048 - kOverhead;                // 2018
constexpr int kMaxFramePayload = 2048;

enum class Result {
    Ok,
    TooLarge,          // 明文超过 2018，或信封超过 2048
    Malformed,         // 长度不足 / env_ver 不符 / tag 长度不对
    UnknownVersion,    // env_ver != 1
    AuthFailed,        // tag 校验失败（也可能是 AAD 不匹配）
};

struct Envelope {
    quint8 keyId = 0;
    quint64 authSeq = 0;
    QByteArray ciphertext;   // 含 tag
};

struct Aad {
    quint8 envVer = kEnvVersion;
    quint8 keyId = 0;
    quint8 kind = 0;
    quint8 dir = 0;
    quint64 authSeq = 0;
    quint16 plaintextLen = 0;
};

// 组 AAD：定长 10 字节。
QByteArray buildAad(const Aad &aad);

// 组 nonce：nonce_prefix(4) || auth_seq_u64le(8)。
QByteArray buildNonce(const QByteArray &noncePrefix4, quint64 authSeq);

// 加密。key 32 字节，noncePrefix 4 字节，authSeq 必须是本次发送的新序号。
// 成功返回完整信封（可直接作为 PBR1 payload）。
QByteArray seal(const QByteArray &plaintext, const QByteArray &key32,
                const QByteArray &noncePrefix4, quint64 authSeq,
                quint8 keyId, quint8 kind, quint8 dir, Result *result = nullptr);

// 拆信封但不验 tag：拿到 key_id / auth_seq / nonce，供调用方查钥与查重放。
// 只做结构校验，**不认证**任何字节。
bool parseEnvelope(const QByteArray &payload, Envelope *out, quint8 *envVersion,
                   Aad *aad, Result *result = nullptr);

// 验 tag 并解密。调用方必须**先** parseEnvelope 取 auth_seq 并做单调性检查
// 是在 tag 通过**之后**——见 §6.2：tag 必须先验，否则未认证的序号推进可 DoS。
QByteArray open(const QByteArray &payload, const QByteArray &key32, Aad aad,
                Result *result = nullptr);

// 全零 X25519 共享密钥检查（低阶点）。返回 true 表示可用。
bool sharedIsUsable(const QByteArray &shared32);

}  // namespace PbrAead
