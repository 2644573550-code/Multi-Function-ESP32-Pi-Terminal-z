// D8b Z 侧：卡匹配、绑卡、挂失、30 秒单次窗口。
// deriveUidIndex 由 D 实现；测试注入一个等价桩（HMAC-SHA256 + 域分隔），
// 桩的算法取自 f-day1 §3 原文，只用于验证匹配逻辑，不代表生产实现。
#include "security/CardMatcher.h"
#include "security/UserDatabase.h"
#include "security/uid_index_key.h"

#include <QCryptographicHash>
#include <QDir>
#include <QMessageAuthenticationCode>
#include <QTemporaryDir>
#include <QtTest>

using namespace security;

namespace {

// 与 F-REVIEW-2 §5.3.2 DR-14 描述一致的派生，仅用于测试替身。
QByteArray testDerive(const QByteArray &rawUid) {
    if (rawUid.size() != 4 && rawUid.size() != 7 && rawUid.size() != 10) return {};
    QByteArray key;
    if (!uidIndexKey(&key)) return {};   // 密钥不可用 → 失败关闭
    QByteArray message = QByteArrayLiteral("p0-rfid-index-v1");
    message.append(static_cast<char>(rawUid.size()));
    message.append(rawUid);
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

const QByteArray kCardA = QByteArray::fromHex("a1b2c3d4");
const QByteArray kCardB = QByteArray::fromHex("11223344556677");   // 7 字节

}  // namespace

class CardMatcherTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;
    QString root_() const { return dir_.filePath("users"); }
    QByteArray keyA_{QByteArray(32, 'a')};
    QByteArray keyB_{QByteArray(32, 'b')};

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        QStandardPaths::setTestModeEnabled(true);
        // 准备整机 UID 密钥（真实实现，不是桩）。
        QVERIFY(ensureUidIndexKey());
    }

    void init() {
        QDir(root_()).removeRecursively();
        QFile::remove(mapPath_());
    }

    QString mapPath_() const { return dir_.filePath("uid_map.json"); }

    void derivesOnlyForValidLengths() {
        QCOMPARE(testDerive(QByteArray::fromHex("aabbcc")).size(), 0);          // 3 字节非法
        QCOMPARE(testDerive(QByteArray::fromHex("a1b2c3d4")).size(), 32);       // 4 字节
        QCOMPARE(testDerive(kCardB).size(), 32);                                // 7 字节
        QCOMPARE(testDerive(QByteArray(10, '\x01')).size(), 32);                // 10 字节
    }

    void missingKeyFailsClosed() {
        // 密钥不可用时派生必须返回空，整条路径失败关闭。
        QByteArray saved;
        QVERIFY(uidIndexKey(&saved));
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/uid_index.key");
        QVERIFY(QFile::rename(path, path + ".bak"));
        QCOMPARE(testDerive(kCardA).size(), 0);
        QVERIFY(QFile::rename(path + ".bak", path));
    }

    void unmatchedCardReturnsNoMatch() {
        CardIndex matcher(mapPath_());
        // 映射表里没有这张卡。
        const CardMatch match = matcher.findOwner(kCardA, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::NoMatch);
        QVERIFY(match.userId.isEmpty());
    }

    void boundCardMatchesItsOwner() {
        CardIndex matcher(mapPath_());
        QVERIFY(matcher.bind(QStringLiteral("userB"), kCardA, testDerive));

        // 关键：未登录、没有任何 DB key 也能选出用户（R-02 死锁已解除）。
        const CardMatch match = matcher.findOwner(kCardA, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::Matched);
        QCOMPARE(match.userId, QStringLiteral("userB"));
    }

    void revokedCardNoLongerMatches() {
        CardIndex matcher(mapPath_());
        QVERIFY(matcher.bind(QStringLiteral("userA"), kCardA, testDerive));
        QVERIFY(matcher.revoke(QStringLiteral("userA"), kCardA, testDerive));

        const CardMatch match = matcher.findOwner(kCardA, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::NoMatch);
    }

    void invalidUidLengthIsRejected() {
        CardIndex matcher(mapPath_());
        const CardMatch match = matcher.findOwner(QByteArray(3, '\x01'), testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::IndexRejected);
        // 也不允许绑非法长度。
        QVERIFY(!matcher.bind(QStringLiteral("userA"), QByteArray(3, '\x01'), testDerive));
    }

    void hexCaseDoesNotMatterBecauseRawBytesAreUsed() {
        // 同一张卡以不同大小写 hex 文本输入，只要解成同一串原始字节就必须匹配。
        CardIndex matcher(mapPath_());
        QVERIFY(matcher.bind(QStringLiteral("userA"),
                             QByteArray::fromHex("A1B2C3D4"), testDerive));
        const CardMatch match = matcher.findOwner(QByteArray::fromHex("a1b2c3d4"), testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::Matched);
    }

    void cardBoundToOthersCannotBeRebound() {
        // 已绑他人的卡必须拒绝，不能靠一次 bind 抢走归属。
        CardIndex matcher(mapPath_());
        QVERIFY(matcher.bind(QStringLiteral("userA"), kCardA, testDerive));
        QVERIFY(!matcher.bind(QStringLiteral("userB"), kCardA, testDerive));
        QCOMPARE(matcher.findOwner(kCardA, testDerive).userId, QStringLiteral("userA"));

        // 原用户挂失后才可以重新绑给他人。
        QVERIFY(matcher.revoke(QStringLiteral("userA"), kCardA, testDerive));
        QVERIFY(matcher.bind(QStringLiteral("userB"), kCardA, testDerive));
        QCOMPARE(matcher.findOwner(kCardA, testDerive).userId, QStringLiteral("userB"));
    }

    void revokeAllClearsEveryCardOfUser() {
        CardIndex matcher(mapPath_());
        QVERIFY(matcher.bind(QStringLiteral("userA"), kCardA, testDerive));
        QVERIFY(matcher.bind(QStringLiteral("userA"), kCardB, testDerive));
        QCOMPARE(matcher.revokeAll(QStringLiteral("userA")), 2);
        QCOMPARE(matcher.findOwner(kCardA, testDerive).verdict, CardMatch::Verdict::NoMatch);
        QCOMPARE(matcher.findOwner(kCardB, testDerive).verdict, CardMatch::Verdict::NoMatch);
    }

    void mapFileIsOwnerOnly() {
        CardIndex matcher(mapPath_());
        QVERIFY(matcher.bind(QStringLiteral("userA"), kCardA, testDerive));
        const QFile::Permissions permissions = QFile(mapPath_()).permissions();
        QVERIFY(permissions.testFlag(QFile::ReadOwner));
        QVERIFY(permissions.testFlag(QFile::WriteOwner));
        QVERIFY(!permissions.testFlag(QFile::ReadGroup));
        QVERIFY(!permissions.testFlag(QFile::ReadOther));
    }

    void bindingWindowIsSingleUse() {
        CardBindingWindow window;
        const qint64 now = 1000000;
        QVERIFY(!window.isOpen(now));
        window.open(now);
        QVERIFY(window.isOpen(now + 1000));
        // 单次：消费一次后立即关闭。
        QVERIFY(window.consume(now + 1000));
        QVERIFY(!window.isOpen(now + 1001));
        QVERIFY(!window.consume(now + 1002));
    }

    void bindingWindowExpiresAfter30s() {
        CardBindingWindow window;
        const qint64 now = 2000000;
        window.open(now);
        QVERIFY(!window.isOpen(now + 30001));
        QVERIFY(!window.consume(now + 30001));
    }
};

QTEST_GUILESS_MAIN(CardMatcherTest)
#include "card_matcher_test.moc"

using namespace security;

