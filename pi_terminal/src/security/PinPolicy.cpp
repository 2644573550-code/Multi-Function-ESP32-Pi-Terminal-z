#include "security/PinPolicy.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <openssl/evp.h>

#include <vector>

namespace security {


namespace PinPolicy {

bool validPin(const QString &pin) {
    if (pin.size() < kMinDigits) return false;
    for (const QChar c : pin) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    }
    return true;
}

QByteArray salt16() {
    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    for (int i = 0; i < kSaltBytes; ++i) salt[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return salt;
}

QByteArray deriveKek(const QString &pin, const QByteArray &salt, int iterations) {
    // shortcut: PBKDF2 而非内存硬 KDF，Pi 实测延迟有余量时升级 Argon2id
    if (!validPin(pin) || salt.size() != kSaltBytes) return {};
    if (iterations < 1) return {};

    // 用 OpenSSL 而非 QPasswordDigestor：同参数下本机实测 630ms vs 96ms，
    // 输出逐字节相同（RFC 6070 向量仍由测试覆盖）。Pi 3B 上 600k 迭代会
    // 从数秒级降到亚秒级，这是 §0.2 的"≤2 秒"能否成立的关键。
    const QByteArray pinBytes = pin.toUtf8();
    std::vector<unsigned char> out(kKekBytes);
    if (PKCS5_PBKDF2_HMAC(pinBytes.constData(), pinBytes.size(),
                          reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
                          iterations, EVP_sha256(), kKekBytes, out.data()) != 1) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(out.data()), int(out.size()));
}

QByteArray verifyValue(const QByteArray &kek) {
    // 域分隔：与 KEK 任何其他用途不得共用同一输入。
    return QCryptographicHash::hash(QByteArrayLiteral("p0-pin-verify-v1") + kek,
                                    QCryptographicHash::Sha256);
}

bool verifyMatches(const QByteArray &stored, const QByteArray &kek) {
    const QByteArray candidate = verifyValue(kek);
    if (stored.size() != candidate.size() || stored.isEmpty()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < stored.size(); ++i) {
        diff |= static_cast<unsigned char>(stored[i] ^ candidate[i]);
    }
    return diff == 0;
}

}  // namespace PinPolicy

}  // namespace security
