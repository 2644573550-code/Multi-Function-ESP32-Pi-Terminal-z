#include "security/TrustStore.h"

#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <map>

// 与 protocol_test.cpp 同风格：纯 main + require，不引入 QtTest/moc。
// 任一条失败即非零退出，ctest 判定失败。

static int g_checks = 0;

static void require(bool ok, const char *message)
{
    ++g_checks;
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

static QString tempPath(const QTemporaryDir &dir, const char *name = "pbr_trust.json")
{
    return dir.filePath(QString::fromLatin1(name));
}

static void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write fixture file");
    file.write(content);
    file.close();
}

// ---------------------------------------------------------------------------
// 1. 静态校验与派生
// ---------------------------------------------------------------------------
static void identityAndValidation()
{
    const QByteArray key = shapedKey(7);

    require(TrustStore::isValidPublicKey(key), "65-byte 0x04 key is valid");
    require(!TrustStore::isValidPublicKey(QByteArray()), "empty key rejected");
    require(!TrustStore::isValidPublicKey(QByteArray(65, '\0')), "missing 0x04 prefix rejected");
    require(!TrustStore::isValidPublicKey(key.left(64)), "64-byte key rejected");
    require(!TrustStore::isValidPublicKey(key.left(1)), "1-byte key rejected");
    require(!TrustStore::isValidPublicKey(key + '\0'), "66-byte key rejected");

    require(TrustStore::isValidBridgeId(QStringLiteral("bridge-1")), "bridge-1 ok");
    require(TrustStore::isValidBridgeId(QStringLiteral("A_b-9")), "letters digits underscore hyphen ok");
    require(TrustStore::isValidBridgeId(QString(64, QLatin1Char('a'))), "64 chars ok");
    require(!TrustStore::isValidBridgeId(QString(65, QLatin1Char('a'))), "65 chars rejected");
    require(!TrustStore::isValidBridgeId(QStringLiteral("")), "empty rejected");
    require(!TrustStore::isValidBridgeId(QStringLiteral("has space")), "space rejected");
    require(!TrustStore::isValidBridgeId(QStringLiteral("has/slash")), "slash rejected");
    require(!TrustStore::isValidBridgeId(QStringLiteral("有中文")), "non-ascii rejected");

    require(TrustStore::fingerprintOf(key).size() == 8, "fingerprint is 8 bytes");
    require(TrustStore::fingerprintOf(QByteArray()) .isEmpty(), "invalid key has no fingerprint");
    require(TrustStore::keyIdOf(key) == quint8(TrustStore::fingerprintOf(key).at(0)), "key_id is fingerprint[0]");
    require(TrustStore::keyIdOf(QByteArray()) == 0, "invalid key has no key_id");

    require(TrustStore::keyIdText(0x00) == QStringLiteral("00"), "key_id text 00");
    require(TrustStore::keyIdText(0x0a) == QStringLiteral("0a"), "key_id text is 2 lowercase hex");
    require(TrustStore::keyIdText(0xff) == QStringLiteral("ff"), "key_id text ff");
}

// ---------------------------------------------------------------------------
// 2. 登记、持久化、权限、重载
// ---------------------------------------------------------------------------
static void enrollAndPersist()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = tempPath(dir);

    const QByteArray key = shapedKey(11);
    {
        TrustStore store(path);
        require(!store.hasActiveIdentity(), "empty store has no identity");
        require(store.all().isEmpty(), "empty store has no records");
        require(store.lookupIdentity(TrustStore::keyIdOf(key)).isEmpty(), "no candidates before enroll");
        require(!store.exists(), "store reports missing before write");

        QString error;
        require(store.enrollIdentity(QStringLiteral("bridge-1"), key, &error), "enroll ok");
        require(error.isEmpty(), "no error on successful enroll");
        require(store.hasActiveIdentity(), "identity active after enroll");
        require(store.all().size() == 1, "one record");
        require(store.findActive(key).has_value(), "findActive by full key");
        require(store.findActive(key)->bridgeId == QStringLiteral("bridge-1"), "bridge_id stored");
        require(store.findActive(key)->revokedAt == 0, "fresh record is active");
        require(store.findActive(key)->enrolledAt > 0, "enrolled_at stamped");

        // §6.7：已有记录不覆盖。
        require(!store.enrollIdentity(QStringLiteral("bridge-1"), key, &error), "duplicate enroll rejected");
        require(error.contains(QStringLiteral("already enrolled")), "duplicate enroll reports reason");
        require(store.all().size() == 1, "duplicate enroll does not add a record");

        // 同一公钥换 bridge_id 也不允许（仍是覆盖）。
        require(!store.enrollIdentity(QStringLiteral("bridge-9"), key, &error), "same key different id rejected");
    }

    require(QFile::exists(path), "store file written");
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

    // reload() 能在同一对象上反映外部改动。
    const QByteArray second = shapedKey(12);
    {
        TrustStore other(path);
        require(other.enrollIdentity(QStringLiteral("bridge-2"), second, nullptr), "second identity enrolled");
    }
    require(reopened.all().size() == 1, "reload() not yet called: still cached");
    require(reopened.reload(nullptr), "reload succeeds");
    require(reopened.all().size() == 2, "reload picks up external change");
}

// ---------------------------------------------------------------------------
// 3. 入参拒绝
// ---------------------------------------------------------------------------
static void enrollRejectsBadInput()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    TrustStore store(tempPath(dir));
    QString error;

    require(!store.enrollIdentity(QStringLiteral("bridge-1"), QByteArray(64, '\x04'), &error), "64-byte key rejected");
    require(!store.enrollIdentity(QStringLiteral("bridge-1"), QByteArray(66, '\x04'), &error), "66-byte key rejected");
    require(!store.enrollIdentity(QStringLiteral("bridge-1"), QByteArray(65, '\x00'), &error), "nonzero-prefix key rejected");
    require(!store.enrollIdentity(QStringLiteral("bad id"), shapedKey(21), &error), "bad bridge_id rejected");
    require(!store.enrollIdentity(QString(), shapedKey(21), &error), "empty bridge_id rejected");
    require(store.all().isEmpty(), "no record created by rejected enrolls");

    require(!store.revokeIdentity(QByteArray(65, '\x00'), &error), "revoke with invalid key rejected");
    require(!store.revokeByFingerprint(QByteArray(7, '\x01'), &error), "revoke with 7-byte fingerprint rejected");
    require(!store.revokeByFingerprint(QByteArray(8, '\x01'), &error), "revoke unknown fingerprint reports not found");
    require(error.contains(QStringLiteral("not found")), "unknown fingerprint explains");
}

// ---------------------------------------------------------------------------
// 4. key_id 冲突（F 指定的 T-03 验收点）
// ---------------------------------------------------------------------------
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
    TrustStore store(tempPath(dir));

    QString error;
    require(store.enrollIdentity(QStringLiteral("bridge-a"), first, &error), "enroll first");
    require(store.enrollIdentity(QStringLiteral("bridge-b"), second, &error), "enroll second");

    const QVector<TrustStore::Record> candidates = store.lookupIdentity(id);
    require(candidates.size() == 2, "lookupIdentity returns BOTH candidates for a key_id collision");
    require(candidates.at(0).publicKey != candidates.at(1).publicKey, "candidates carry distinct full keys");

    // 取第一条即认身份是错的；精确查找必须只命中一条。
    require(!store.findActive(first)->bridgeId.isEmpty(), "first has an id");
    require(store.findActive(first)->bridgeId == QStringLiteral("bridge-a"), "first maps to bridge-a");
    require(store.findActive(second)->bridgeId == QStringLiteral("bridge-b"), "second maps to bridge-b");

    // 撤销其中一条，候选只剩一条。
    require(store.revokeIdentity(first, &error), "revoke first");
    require(store.lookupIdentity(id).size() == 1, "only one candidate after revoking one");
    require(store.lookupIdentity(id).at(0).publicKey == second, "remaining candidate is the live one");
}

// ---------------------------------------------------------------------------
// 5. 多记录与撤销语义
// ---------------------------------------------------------------------------
static void manyRecordsAndRevoke()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = tempPath(dir);
    TrustStore store(path);

    constexpr int kCount = 50;
    for (int i = 0; i < kCount; ++i) {
        require(store.enrollIdentity(QStringLiteral("bridge-%1").arg(i), shapedKey(quint32(1000 + i)), nullptr),
                "enroll in bulk");
    }
    require(store.all().size() == kCount, "all bulk records present");

    // 每条都能被自己的 key_id 找到，且完整公钥可精确命中。
    int found = 0;
    for (int i = 0; i < kCount; ++i) {
        const QByteArray key = shapedKey(quint32(1000 + i));
        if (store.findActive(key).has_value()) ++found;
        const auto candidates = store.lookupIdentity(TrustStore::keyIdOf(key));
        require(!candidates.isEmpty(), "bulk candidate lookup non-empty");
    }
    require(found == kCount, "all bulk records findable by full key");

    // 撤销一半，只剩一半有效。
    for (int i = 0; i < kCount; i += 2) {
        require(store.revokeIdentity(shapedKey(quint32(1000 + i)), nullptr), "bulk revoke");
    }
    int active = 0;
    for (const auto &record : store.all()) if (record.active()) ++active;
    require(active == kCount / 2, "half still active after bulk revoke");
    require(store.all().size() == kCount, "revoked records kept for audit");

    // 撤销记录在重开后仍然不可用。
    TrustStore reopened(path);
    require(reopened.findActive(shapedKey(1000)).value_or(TrustStore::Record{}).bridgeId.isEmpty(),
            "revoked record stays inactive across reload");
    require(reopened.findActive(shapedKey(1001)).has_value(), "non-revoked record still active across reload");
}

static void revokeSemantics()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");
    const QString path = tempPath(dir);
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
    require(!store.revokeByFingerprint(TrustStore::fingerprintOf(key), &error), "double revoke by fingerprint rejected");

    // 撤销后可以重新登记（换板流程）。
    require(store.enrollIdentity(QStringLiteral("bridge-2"), key, &error), "re-enroll after revoke");
    require(store.findActive(key)->bridgeId == QStringLiteral("bridge-2"), "bridge_id updated on re-enroll");
    require(store.all().size() == 2, "re-enroll appends a new record, old revoked one kept");
}

// ---------------------------------------------------------------------------
// 6. 损坏/篡改一律失败关闭
// ---------------------------------------------------------------------------
static void corruptedStoreFailsClosed()
{
    struct Case { const char *name; QByteArray content; };
    const QByteArray validJson =
        R"({"schema":"pbr-trust-v1","records":[]})";
    const QVector<Case> cases{
        {"empty file", QByteArray()},
        {"not json", QByteArray("{not json")},
        {"json array instead of object", QByteArray("[]")},
        {"wrong schema", QByteArray(R"({"schema":"other","records":[]})")},
        {"missing schema", QByteArray(R"({"records":[]})")},
        {"records not an array", QByteArray(R"({"schema":"pbr-trust-v1","records":{}})")},
    };

    for (const Case &testCase : cases) {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir for corrupt case");
        const QString path = tempPath(dir);
        writeFile(path, testCase.content);

        TrustStore store(path);
        QString error;
        require(!store.hasActiveIdentity(), testCase.name);
        require(store.lookupIdentity(1).isEmpty(), "corrupt store yields no candidates");
        require(store.all().isEmpty(), "corrupt store yields no records");
        require(!store.enrollIdentity(QStringLiteral("bridge-1"), shapedKey(31), &error),
                "corrupt store refuses enroll");
        require(!error.isEmpty(), "corrupt store explains refusal");
    }

    // 合法但公钥非法的记录 → 损坏。
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir");
        const QString path = tempPath(dir);
        writeFile(path, QByteArray(R"({"schema":"pbr-trust-v1","records":[{"bridge_id":"b","public_key":"aabb","fingerprint":"0000000000000000","enrolled_at":1,"revoked_at":0}]})"));
        TrustStore store(path);
        require(!store.hasActiveIdentity(), "short public_key in store is corruption");
    }

    // fingerprint 字段被改 → 交叉校验失败。
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir");
        const QString path = tempPath(dir);
        const QByteArray key = shapedKey(37);
        {
            TrustStore seedStore(path);
            require(seedStore.enrollIdentity(QStringLiteral("bridge-1"), key, nullptr), "seed enroll");
        }
        QFile file(path);
        require(file.open(QIODevice::ReadOnly), "read-back");
        QByteArray content = file.readAll();
        file.close();
        const QByteArray hexFingerprint = TrustStore::fingerprintOf(key).toHex();
        const int at = content.indexOf(hexFingerprint);
        require(at > 0, "fingerprint present in file");
        content.replace(at, hexFingerprint.size(), QByteArray("0000000000000000"));
        writeFile(path, content);

        TrustStore tampered(path);
        QString tamperedError;
        require(!tampered.hasActiveIdentity(), "tampered fingerprint fails closed");
        require(!tampered.enrollIdentity(QStringLiteral("bridge-1"), key, &tamperedError),
                "tampered store refuses enroll");
    }

    // fingerprint 字段**缺失**是可以接受的：指纹由公钥自算。
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir");
        const QString path = tempPath(dir);
        const QByteArray key = shapedKey(41);
        const QByteArray json = QByteArray("{\"schema\":\"pbr-trust-v1\",\"records\":[{\"bridge_id\":\"b1\",\"public_key\":\"")
            + key.toHex() + QByteArray("\",\"enrolled_at\":1,\"revoked_at\":0}]}");
        writeFile(path, json);
        TrustStore store(path);
        require(store.hasActiveIdentity(), "record without fingerprint field is accepted");
        require(store.findActive(key).has_value(), "key derived from public_key alone");
    }

    Q_UNUSED(validJson);
}

int main()
{
    identityAndValidation();
    enrollAndPersist();
    enrollRejectsBadInput();
    keyIdCollisionReturnsAll();
    manyRecordsAndRevoke();
    revokeSemantics();
    corruptedStoreFailsClosed();
    std::cout << "trust store tests passed (" << g_checks << " assertions)\n";
    return 0;
}
