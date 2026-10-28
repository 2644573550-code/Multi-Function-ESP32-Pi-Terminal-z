#include "security/TrustStore.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <map>

// 与 protocol_test.cpp 同风格：纯 main + require，不引入 QtTest/moc。
// 运行失败即非零退出，ctest 判定失败。

static void require(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// 65 字节未压缩点的形状（0x04 前缀），内容不必是曲线上真正的点：
// TrustStore 只做长度/前缀校验与哈希，验签在 T-07 里。
static QByteArray shapedKey(quint32 seed)
{
    QByteArray key(65, '\0');
    key[0] = char(0x04);
    quint32 state = seed * 2654435761u + 1u;
    for (int i = 1; i < key.size(); ++i) {
        state = state * 1664525u + 1013904223u;
        key[i] = char((state >> 16) & 0xff);
    }
    return key;
}

// 构造一对 key_id 相同、公钥不同的密钥。1 字节空间只有 256 个桶，
// 平均几十次采样就会撞上，这里用确定性序列保证可复现。
static void findCollidingPair(QByteArray *first, QByteArray *second)
{
    std::map<int, QByteArray> seen;
    for (quint32 seed = 0; seed < 200000; ++seed) {
        const QByteArray key = shapedKey(seed);
        const int id = TrustStore::keyIdOf(key);
        auto it = seen.find(id);
        if (it != seen.end() && it->second != key) {
            *first = it->second;
            *second = key;
            return;
        }
        seen.emplace(id, key);
    }
}

static void identityAndValidation()
{
    const QByteArray key = shapedKey(7);

    require(TrustStore::isValidPublicKey(key), "65-byte 0x04 key is valid");
    require(!TrustStore::isValidPublicKey(QByteArray(65, '\0')), "missing 0x04 prefix rejected");
    require(!TrustStore::isValidPublicKey(key.left(64)), "64-byte key rejected");
    require(!TrustStore::isValidPublicKey(key + '\0'), "66-byte key rejected");

    require(TrustStore::isValidBridgeId(QStringLiteral("bridge-1")), "bridge-1 ok");
    require(TrustStore::isValidBridgeId(QString(64, QLatin1Char('a'))), "64 chars ok");
    require(!TrustStore::isValidBridgeId(QString(65, QLatin1Char('a'))), "65 chars rejected");
    require(!TrustStore::isValidBridgeId(QStringLiteral("has space")), "space rejected");
    require(!TrustStore::isValidBridgeId(QStringLiteral("")), "empty rejected");

    require(TrustStore::fingerprintOf(key).size() == 8, "fingerprint is 8 bytes");
    require(TrustStore::keyIdOf(key) == quint8(TrustStore::fingerprintOf(key).at(0)), "key_id is fingerprint[0]");
    require(TrustStore::keyIdOf(QByteArray()) == 0, "invalid key has no key_id");

    const QString text = TrustStore::keyIdText(0x0a);
    require(text == QStringLiteral("0a"), "key_id text is 2 lowercase hex");
}

static void enrollAndPersist()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = dir.filePath(QStringLiteral("pbr_trust.json"));

    const QByteArray key = shapedKey(11);
    {
        TrustStore store(path);
        require(!store.hasActiveIdentity(), "empty store has no identity");
        require(store.lookupIdentity(TrustStore::keyIdOf(key)).isEmpty(), "no candidates before enroll");

        QString error;
        require(store.enrollIdentity(QStringLiteral("bridge-1"), key, &error), "enroll ok");
        require(store.hasActiveIdentity(), "identity active after enroll");
        require(store.findActive(key).has_value(), "findActive by full key");

        // §6.7：已有记录不覆盖。
        require(!store.enrollIdentity(QStringLiteral("bridge-1"), key, &error), "duplicate enroll rejected");
        require(error.contains(QStringLiteral("already enrolled")), "duplicate enroll reports reason");
    }

    // 0600：只有 owner 可读写。
    const QFile::Permissions perms = QFile::permissions(path);
    require(perms.testFlag(QFile::ReadOwner) && perms.testFlag(QFile::WriteOwner), "owner rw");
    require(!perms.testFlag(QFile::ReadGroup) && !perms.testFlag(QFile::ReadOther), "no group/other read");
    require(!perms.testFlag(QFile::WriteGroup) && !perms.testFlag(QFile::WriteOther), "no group/other write");

    // 重新打开即从磁盘读回（验证持久化而非内存态）。
    TrustStore reopened(path);
    require(reopened.hasActiveIdentity(), "reload sees enrolled identity");
    auto record = reopened.findActive(key);
    require(record.has_value() && record->bridgeId == QStringLiteral("bridge-1"), "bridge_id survived reload");
    require(record->fingerprint == TrustStore::fingerprintOf(key), "fingerprint recomputed identically");
}

// F 的 T-03 验收点：key_id 冲突时必须返回**多条**，调用方逐个验签。
static void keyIdCollisionReturnsAll()
{
    QByteArray first;
    QByteArray second;
    findCollidingPair(&first, &second);
    require(!first.isEmpty() && first != second, "found colliding pair");
    const quint8 id = TrustStore::keyIdOf(first);
    require(TrustStore::keyIdOf(second) == id, "pair shares key_id");
    require(TrustStore::fingerprintOf(first) != TrustStore::fingerprintOf(second), "fingerprints differ");

    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    TrustStore store(dir.filePath(QStringLiteral("pbr_trust.json")));

    QString error;
    require(store.enrollIdentity(QStringLiteral("bridge-a"), first, &error), "enroll first");
    require(store.enrollIdentity(QStringLiteral("bridge-b"), second, &error), "enroll second");

    const QVector<TrustStore::Record> candidates = store.lookupIdentity(id);
    require(candidates.size() == 2, "lookupIdentity returns BOTH candidates for a key_id collision");
    require(candidates.at(0).publicKey != candidates.at(1).publicKey, "candidates carry distinct full keys");

    // 取第一条即认身份是错的；精确查找必须只命中一条。
    require(store.findActive(first).has_value(), "findActive(first)");
    require(store.findActive(second).has_value(), "findActive(second)");
    require(store.findActive(first)->bridgeId == QStringLiteral("bridge-a"), "first maps to bridge-a");
    require(store.findActive(second)->bridgeId == QStringLiteral("bridge-b"), "second maps to bridge-b");
}

static void revokeSemantics()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = dir.filePath(QStringLiteral("pbr_trust.json"));
    TrustStore store(path);
    const QByteArray key = shapedKey(23);
    const quint8 id = TrustStore::keyIdOf(key);

    QString error;
    require(store.enrollIdentity(QStringLiteral("bridge-1"), key, &error), "enroll");
    require(store.revokeIdentity(key, &error), "revoke ok");
    require(!store.hasActiveIdentity(), "no active identity after revoke");
    require(store.lookupIdentity(id).isEmpty(), "revoked identity not offered as candidate");
    require(!store.findActive(key).has_value(), "revoked identity not found active");
    require(store.all().size() == 1, "revoked record is kept for audit, not deleted");
    require(store.all().at(0).revokedAt > 0, "revoked_at stamped");

    require(!store.revokeIdentity(key, &error), "double revoke rejected");

    // 撤销后可以重新登记（换板流程）。
    require(store.enrollIdentity(QStringLiteral("bridge-2"), key, &error), "re-enroll after revoke");
    require(store.findActive(key)->bridgeId == QStringLiteral("bridge-2"), "bridge_id updated on re-enroll");
}

static void corruptedStoreFailsClosed()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = dir.filePath(QStringLiteral("pbr_trust.json"));

    // 写一个不是 JSON 的文件：必须当损坏处理，**不能**当空库。
    {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "write corrupt store");
        file.write("{not json");
    }
    TrustStore store(path);
    QString error;
    require(!store.hasActiveIdentity(), "corrupt store yields no identity");
    require(store.lookupIdentity(1).isEmpty(), "corrupt store yields no candidates");
    require(!store.enrollIdentity(QStringLiteral("bridge-1"), shapedKey(31), &error), "corrupt store refuses enroll");
    require(error.contains(QStringLiteral("corrupt")), "corrupt store reports reason");

    // 指纹字段被改成别的值 → 视为损坏（防篡改交叉校验）。
    QTemporaryDir dir2;
    require(dir2.isValid(), "temp dir 2");
    const QString path2 = dir2.filePath(QStringLiteral("pbr_trust.json"));
    const QByteArray key = shapedKey(37);
    {
        TrustStore seed(path2);
        require(seed.enrollIdentity(QStringLiteral("bridge-1"), key, nullptr), "seed enroll");
    }
    QFile file(path2);
    require(file.open(QIODevice::ReadOnly), "read-back");
    QByteArray content = file.readAll();
    file.close();
    const QByteArray hexKey = key.toHex();
    const QByteArray hexFingerprint = TrustStore::fingerprintOf(key).toHex();
    // 把 fingerprint 字段替换成 8 个 00 的 hex。
    const int at = content.indexOf(hexFingerprint);
    require(at > 0, "fingerprint present in file");
    content.replace(at, hexFingerprint.size(), QByteArray("0000000000000000"));
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "rewrite tampered store");
    file.write(content);
    file.close();

    TrustStore tampered(path2);
    QString tamperedError;
    require(!tampered.hasActiveIdentity(), "tampered fingerprint fails closed");
    require(!tampered.enrollIdentity(QStringLiteral("bridge-1"), key, &tamperedError), "tampered store refuses enroll");
    Q_UNUSED(hexKey);
}

int main()
{
    identityAndValidation();
    enrollAndPersist();
    keyIdCollisionReturnsAll();
    revokeSemantics();
    corruptedStoreFailsClosed();
    std::cout << "trust store tests passed\n";
    return 0;
}
