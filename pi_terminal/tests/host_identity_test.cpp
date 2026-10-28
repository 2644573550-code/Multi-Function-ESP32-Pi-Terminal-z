#include "security/HostIdentity.h"
#include "security/TrustStore.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

// 需要 mbedtls；本测试目标只在找到 mbedtls 时才由 CMake 生成。

static void require(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

static void generateAndSign()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = dir.filePath(QStringLiteral("host_identity.pem"));

    HostIdentity identity(path);
    require(!identity.exists(), "identity absent before generate");
    require(!identity.hasKey(), "no key before generate");

    QString error;
    require(identity.generate(&error), qPrintable(QStringLiteral("generate: ") + error));
    require(identity.exists(), "identity file created");
    require(identity.hasKey(), "key loaded after generate");

    const QByteArray pub = identity.publicKey();
    require(pub.size() == HostIdentity::kPublicKeyBytes, "public key is 65 bytes");
    require(quint8(pub.at(0)) == 0x04, "public key is an uncompressed point");
    require(identity.fingerprint() == TrustStore::fingerprintOf(pub), "fingerprint agrees with TrustStore");
    require(identity.keyId() == TrustStore::keyIdOf(pub), "key_id agrees with TrustStore");

    const QByteArray hash = sha256("pbr-secure-v1 test message");
    QByteArray signature;
    require(identity.sign(hash, &signature, &error), qPrintable(QStringLiteral("sign: ") + error));
    require(signature.size() == HostIdentity::kSignatureBytes, "signature is r||s 64 bytes");
    require(HostIdentity::verify(pub, hash, signature), "self-verify succeeds");

    // 篡改摘要 / 篡改签名 / 换公钥，都必须失败。
    QByteArray otherHash = hash;
    otherHash[0] = char(quint8(otherHash.at(0)) ^ 0x01);
    require(!HostIdentity::verify(pub, otherHash, signature), "tampered digest rejected");

    QByteArray badSignature = signature;
    badSignature[63] = char(quint8(badSignature.at(63)) ^ 0x01);
    require(!HostIdentity::verify(pub, hash, badSignature), "tampered signature rejected");

    QByteArray shortSignature = signature.left(63);
    require(!HostIdentity::verify(pub, hash, shortSignature), "truncated signature rejected");

    // 0600
    const QFile::Permissions perms = QFile::permissions(path);
    require(perms.testFlag(QFile::ReadOwner) && perms.testFlag(QFile::WriteOwner), "owner rw");
    require(!perms.testFlag(QFile::ReadGroup) && !perms.testFlag(QFile::ReadOther), "no group/other read");

    // 重新加载：公钥一致，且旧签名仍能验过。
    HostIdentity reloaded(path);
    require(reloaded.load(&error), qPrintable(QStringLiteral("load: ") + error));
    require(reloaded.publicKey() == pub, "same public key after reload");
    require(HostIdentity::verify(reloaded.publicKey(), hash, signature), "old signature verifies after reload");

    // 不覆盖：默认拒绝。
    require(!identity.generate(&error), "generate refuses to overwrite existing identity");
    require(error.contains(QStringLiteral("already exists")), "overwrite refusal explains why");

    // 显式 overwrite 才换密钥，且公钥必须变。
    require(identity.generate(&error, true), qPrintable(QStringLiteral("overwrite: ") + error));
    require(identity.publicKey() != pub, "overwrite produces a new key");
}

static void derConversion()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    HostIdentity identity(dir.filePath(QStringLiteral("host_identity.pem")));
    require(identity.generate(), "generate for DER roundtrip");

    const QByteArray hash = sha256("der roundtrip");
    // 反复签直到 r 的最高位为 1（约各 50%），专门覆盖 DER 补 0x00 的分支。
    QByteArray highBitSignature;
    for (int attempt = 0; attempt < 200 && highBitSignature.isEmpty(); ++attempt) {
        QByteArray signature;
        require(identity.sign(hash, &signature), "sign");
        if (quint8(signature.at(0)) & 0x80) highBitSignature = signature;
    }
    require(!highBitSignature.isEmpty(), "found a signature whose r has the high bit set");

    QByteArray der;
    require(HostIdentity::rawToDer(highBitSignature, &der), "raw -> DER");
    require(quint8(der.at(0)) == 0x30, "DER is a SEQUENCE");
    QByteArray back;
    require(HostIdentity::derToRaw(der, &back), "DER -> raw");
    require(back == highBitSignature, "r||s survives a DER roundtrip when r has the high bit set");
    require(HostIdentity::verify(identity.publicKey(), hash, back), "roundtripped signature verifies");
}

int main()
{
    generateAndSign();
    derConversion();
    std::cout << "host identity tests passed\n";
    return 0;
}
