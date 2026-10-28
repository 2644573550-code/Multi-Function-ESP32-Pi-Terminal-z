#pragma once

#include <QByteArray>
#include <QString>

// PBR secure v1 握手密钥派生（§4）。
//
// 只做纯计算，不做 IO：X25519 共享密钥、HKDF-SHA256 派生 96 字节、
// 切片成三个方向密钥。握手状态机（消息顺序、超时、重传）不在本文件。
namespace PbrHandshake {

constexpr int kKeyBytes = 32;
constexpr int kNoncePrefixBytes = 4;
constexpr int kSessionIdBytes = 16;

struct Keys {
    QByteArray handshake;     // ks[0..32)
    QByteArray piToBridge;    // ks[32..64)
    QByteArray bridgeToPi;    // ks[64..96)
    QByteArray sessionId;     // transcript_hash[0..16)
    QByteArray noncePrefix;   // transcript_hash[16..20)
};

enum class Result {
    Ok,
    BadKeyLength,
    SharedIsZero,      // X25519 输出全零 = 低阶点，断连且不回错误细节
    BadTranscript,
};

// X25519(priv32, peerPub32)，返回 32 字节。失败返回空。
QByteArray sharedSecret(const QByteArray &privateKey, const QByteArray &peerPublicKey);

// 由 X25519 私钥推导公钥（用于握手前发送自己的临时公钥）。
QByteArray publicKeyOf(const QByteArray &privateKey);

// §4：salt = SHA-256(T3)，ikm = shared，info = "pbr-secure-v1"，L = 96。
Keys deriveKeys(const QByteArray &shared32, const QByteArray &transcriptT3);

// 常量时间比较，用于签名/公钥逐字节比对（§6.7「比完整公钥」）。
bool constantTimeEquals(const QByteArray &a, const QByteArray &b);

}  // namespace PbrHandshake
