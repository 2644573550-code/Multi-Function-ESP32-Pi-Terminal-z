#include "security/HostIdentity.h"
#include "security/TrustStore.h"

#include <QCryptographicHash>
#include <QSet>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

// 需要 mbedtls；本测试目标只在 CMake 找到 mbedtls 时才生成。

static int g_checks = 0;

static void require(bool ok, const char *message)
{
    ++g_checks;
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

static QString tempIdentity(QTemporaryDir &dir)
{
    return dir.filePath(QStringLiteral("host_identity.pem"));
}

static void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write fixture");
    file.write(content);
    file.close();
}

// ---------------------------------------------------------------------------
// 1. 空对象行为（未生成/未载入）
// ---------------------------------------------------------------------------
static void emptyObject()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    HostIdentity identity(tempIdentity(dir));

    require(!identity.exists(), "file absent");
    require(!identity.hasKey(), "no key before generate/load");
    require(identity.publicKey().isEmpty(), "no public key before generate");
    require(identity.fingerprint().isEmpty(), "no fingerprint before generate");
    require(identity.keyId() == 0, "no key_id before generate");

    QByteArray signature;
    QString error;
    require(!identity.sign(sha256("x"), &signature, &error), "sign without key fails");
    require(signature.isEmpty(), "failed sign leaves signature empty");
    require(!error.isEmpty(), "failed sign explains why");

    require(!identity.load(&error), "load of missing file fails");
    require(error.contains(QStringLiteral("not found")), "missing file reported");
}

// ---------------------------------------------------------------------------
// 2. 生成、派生一致性、权限
// ---------------------------------------------------------------------------
static void generateBasics()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = tempIdentity(dir);

    HostIdentity identity(path);
    QString error;
    require(identity.generate(&error), qPrintable(QStringLiteral("generate: ") + error));
    require(identity.exists(), "file created");
    require(identity.hasKey(), "key loaded after generate");

    const QByteArray pub = identity.publicKey();
    require(pub.size() == HostIdentity::kPublicKeyBytes, "public key is 65 bytes");
    require(quint8(pub.at(0)) == 0x04, "public key is uncompressed point");
    require(identity.fingerprint().size() == 8, "fingerprint is 8 bytes");
    require(identity.fingerprint() == TrustStore::fingerprintOf(pub), "fingerprint agrees with TrustStore");
    require(identity.keyId() == TrustStore::keyIdOf(pub), "key_id agrees with TrustStore");
    require(identity.keyId() == quint8(identity.fingerprint().at(0)), "key_id is fingerprint[0]");

    const QFile::Permissions perms = QFile::permissions(path);
    require(perms.testFlag(QFile::ReadOwner) && perms.testFlag(QFile::WriteOwner), "owner rw");
    require(!perms.testFlag(QFile::ReadGroup) && !perms.testFlag(QFile::ReadOther), "no group/other read");
    require(!perms.testFlag(QFile::WriteGroup) && !perms.testFlag(QFile::WriteOther), "no group/other write");

    // 文件内容不应当是明文标量的 hex（只应是 PEM 包装）。
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read identity file");
    const QByteArray content = file.readAll();
    require(content.startsWith("-----BEGIN PBR HOST IDENTITY-----"), "PEM header present");
    require(content.contains("-----END PBR HOST IDENTITY-----"), "PEM footer present");
}

// ---------------------------------------------------------------------------
// 3. 签名 / 验签：批量 + 篡改 + 跨身份
// ---------------------------------------------------------------------------
static void signVerifySuite()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    HostIdentity identity(tempIdentity(dir));
    require(identity.generate(), "generate for signing");
    const QByteArray pub = identity.publicKey();

    // 50 条不同消息，逐条签并验。
    QSet<QByteArray> signatures;
    for (int i = 0; i < 50; ++i) {
        const QByteArray message = QByteArray("pbr message #") + QByteArray::number(i);
        const QByteArray hash = sha256(message);
        QByteArray signature;
        require(identity.sign(hash, &signature), "sign batch message");
        require(signature.size() == HostIdentity::kSignatureBytes, "signature is 64 bytes");
        require(HostIdentity::verify(pub, hash, signature), "verify batch message");
        require(!signatures.contains(signature), "ECDSA signatures are randomized per call");
        signatures.insert(signature);
    }
    require(signatures.size() == 50, "50 distinct signatures");

    const QByteArray hash = sha256("tamper target");
    QByteArray signature;
    require(identity.sign(hash, &signature), "sign for tamper tests");
    require(HostIdentity::verify(pub, hash, signature), "baseline verifies");

    QByteArray otherHash = hash;
    otherHash[0] = char(quint8(otherHash.at(0)) ^ 0x01);
    require(!HostIdentity::verify(pub, otherHash, signature), "tampered digest rejected");

    QByteArray badSignature = signature;
    badSignature[0] = char(quint8(badSignature.at(0)) ^ 0x01);
    require(!HostIdentity::verify(pub, hash, badSignature), "tampered r rejected");
    badSignature = signature;
    badSignature[63] = char(quint8(badSignature.at(63)) ^ 0x01);
    require(!HostIdentity::verify(pub, hash, badSignature), "tampered s rejected");

    require(!HostIdentity::verify(pub, hash, signature.left(63)), "truncated signature rejected");
    require(!HostIdentity::verify(pub, hash, signature + '\0'), "over-long signature rejected");
    require(!HostIdentity::verify(pub.left(64), hash, signature), "short public key rejected");
    require(!HostIdentity::verify(pub, hash.left(31), signature), "short digest rejected");
    require(!HostIdentity::verify(QByteArray(65, '\x04'), hash, signature), "off-curve public key rejected");

    // 跨身份：另一把密钥的公钥不能验过本密钥的签名。
    QTemporaryDir other;
    require(other.isValid(), "second temp dir");
    HostIdentity stranger(other.filePath(QStringLiteral("stranger.pem")));
    require(stranger.generate(), "generate stranger");
    require(!HostIdentity::verify(stranger.publicKey(), hash, signature),
            "signature does not verify under another identity");
    require(stranger.publicKey() != pub, "two identities have different public keys");
    require(stranger.keyId() != 0, "stranger has a key_id");

    // 自己签的消息，另一把密钥也无法伪造出通过本公钥验证的签名。
    QByteArray strangerSignature;
    require(stranger.sign(hash, &strangerSignature), "stranger signs");
    require(!HostIdentity::verify(pub, hash, strangerSignature), "cross-identity signature rejected");
}

// ---------------------------------------------------------------------------
// 4. 持久化、重载、覆盖
// ---------------------------------------------------------------------------
static void reloadAndOverwrite()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = tempIdentity(dir);

    HostIdentity identity(path);
    require(identity.generate(), "initial generate");
    const QByteArray pub = identity.publicKey();
    const QByteArray hash = sha256("persisted");
    QByteArray signature;
    require(identity.sign(hash, &signature), "sign before reload");

    HostIdentity reloaded(path);
    QString error;
    require(reloaded.load(&error), qPrintable(QStringLiteral("load: ") + error));
    require(reloaded.publicKey() == pub, "same public key after reload");
    require(HostIdentity::verify(reloaded.publicKey(), hash, signature), "old signature verifies after reload");

    // 不覆盖：默认拒绝。
    require(!reloaded.generate(&error), "generate refuses to overwrite existing identity");
    require(error.contains(QStringLiteral("already exists")), "overwrite refusal explains");
    require(reloaded.publicKey() == pub, "refused overwrite kept the old key");

    // 显式 overwrite 才换密钥，且旧签名在新公钥下必须失效。
    require(reloaded.generate(&error, true), qPrintable(QStringLiteral("overwrite: ") + error));
    const QByteArray newPub = reloaded.publicKey();
    require(newPub != pub, "overwrite produces a new key");
    require(!HostIdentity::verify(newPub, hash, signature), "old signature invalid under new key");
    require(HostIdentity::verify(newPub, hash, [&] {
        QByteArray fresh;
        reloaded.sign(hash, &fresh);
        return fresh;
    }()), "new key signs and verifies");

    // 覆盖后文件仍可用：第三个对象重新载入得到新公钥。
    HostIdentity third(path);
    require(third.load(), "load after overwrite");
    require(third.publicKey() == newPub, "overwritten key persisted");
}

// ---------------------------------------------------------------------------
// 5. 损坏文件必须失败
// ---------------------------------------------------------------------------
static void corruptFiles()
{
    const struct { const char *name; QByteArray content; } cases[] = {
        {"empty file", QByteArray()},
        {"not pem", QByteArray("hello world")},
        {"header only", QByteArray("-----BEGIN PBR HOST IDENTITY-----\n-----END PBR HOST IDENTITY-----\n")},
        {"invalid base64", QByteArray("-----BEGIN PBR HOST IDENTITY-----\n!!!!\n-----END PBR HOST IDENTITY-----\n")},
        {"truncated scalar", QByteArray("-----BEGIN PBR HOST IDENTITY-----\nAAAA\n-----END PBR HOST IDENTITY-----\n")},
        {"zero scalar", QByteArray("-----BEGIN PBR HOST IDENTITY-----\n") + QByteArray(32, '\0').toBase64()
                            + QByteArray("\n-----END PBR HOST IDENTITY-----\n")},
    };

    for (const auto &testCase : cases) {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir for corrupt case");
        const QString path = tempIdentity(dir);
        writeFile(path, testCase.content);

        HostIdentity identity(path);
        QString error;
        require(!identity.load(&error), testCase.name);
        require(!identity.hasKey(), "corrupt identity leaves no key");
        require(identity.publicKey().isEmpty(), "corrupt identity has no public key");
        QByteArray signature;
        require(!identity.sign(sha256("x"), &signature, &error), "sign after failed load fails");
    }
}

// ---------------------------------------------------------------------------
// 6. reset() 清空
// ---------------------------------------------------------------------------
static void resetClears()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    HostIdentity identity(tempIdentity(dir));
    require(identity.generate(), "generate for reset");
    require(identity.hasKey(), "key present before reset");
    identity.reset();
    require(!identity.hasKey(), "reset clears key");
    require(identity.publicKey().isEmpty(), "reset clears public key");
    require(identity.keyId() == 0, "reset clears key_id");
    QByteArray signature;
    require(!identity.sign(sha256("x"), &signature), "sign after reset fails");
    // 文件仍在，可以重新载入。
    require(identity.load(), "reload after reset");
    require(identity.hasKey(), "key back after reload");
}

// ---------------------------------------------------------------------------
// 7. DER ↔ r||s（含 r 最高位为 1 的分支）
// ---------------------------------------------------------------------------
static void derConversion()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    HostIdentity identity(tempIdentity(dir));
    require(identity.generate(), "generate for DER tests");

    const QByteArray hash = sha256("der roundtrip");
    int highBitCases = 0;
    for (int i = 0; i < 40; ++i) {
        QByteArray signature;
        require(identity.sign(hash, &signature), "sign for DER");
        QByteArray der;
        require(HostIdentity::rawToDer(signature, &der), "raw -> DER");
        require(quint8(der.at(0)) == 0x30, "DER is a SEQUENCE");
        QByteArray back;
        require(HostIdentity::derToRaw(der, &back), "DER -> raw");
        require(back == signature, "r||s survives a DER roundtrip");
        require(HostIdentity::verify(identity.publicKey(), hash, back), "roundtripped signature verifies");
        if (quint8(signature.at(0)) & 0x80) ++highBitCases;
    }
    // 40 次签名里 r 高位为 1 的概率极高；至少命中一次才说明该分支被覆盖过。
    require(highBitCases > 0, "at least one signature exercised the r-high-bit DER padding branch");

    // 入参非法必须拒绝。
    QByteArray der;
    require(!HostIdentity::rawToDer(QByteArray(63, '\x01'), &der), "rawToDer rejects short input");
    require(!HostIdentity::rawToDer(QByteArray(65, '\x01'), &der), "rawToDer rejects long input");
    QByteArray raw;
    require(!HostIdentity::derToRaw(QByteArray("garbage"), &raw), "derToRaw rejects garbage");
    require(!HostIdentity::derToRaw(QByteArray(), &raw), "derToRaw rejects empty");
    require(!HostIdentity::derToRaw(QByteArray("\x30\x00", 2), &raw), "derToRaw rejects empty sequence");
}

int main()
{
    emptyObject();
    generateBasics();
    signVerifySuite();
    reloadAndOverwrite();
    corruptFiles();
    resetClears();
    derConversion();
    std::cout << "host identity tests passed (" << g_checks << " assertions)\n";
    return 0;
}
