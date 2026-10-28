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
    }

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
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyA_));
        db.close();

        CardIndex matcher(&db);
        // 库里没有这张卡。
        const CardMatch match = matcher.findOwner(kCardA,
            {{QStringLiteral("userA"), keyA_}}, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::NoMatch);
        QVERIFY(match.userId.isEmpty());
    }

    void boundCardMatchesItsOwner() {
        UserDatabase db(root_());
        CardIndex matcher(&db);
        // 先建两个用户库。
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyA_)); db.close();
        QVERIFY(db.openForSession(QStringLiteral("userB"), keyB_)); db.close();

        QVERIFY(matcher.bind(QStringLiteral("userB"), keyB_, kCardA, testDerive));

        const CardMatch match = matcher.findOwner(kCardA,
            {{QStringLiteral("userA"), keyA_}, {QStringLiteral("userB"), keyB_}}, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::Matched);
        // 必须选中绑定的那个用户，不是第一个试开的。
        QCOMPARE(match.userId, QStringLiteral("userB"));
    }

    void revokedCardNoLongerMatches() {
        UserDatabase db(root_());
        CardIndex matcher(&db);
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyA_)); db.close();
        QVERIFY(matcher.bind(QStringLiteral("userA"), keyA_, kCardA, testDerive));
        QVERIFY(matcher.revoke(QStringLiteral("userA"), keyA_, kCardA, testDerive));

        const CardMatch match = matcher.findOwner(kCardA,
            {{QStringLiteral("userA"), keyA_}}, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::NoMatch);
    }

    void invalidUidLengthIsRejected() {
        UserDatabase db(root_());
        CardIndex matcher(&db);
        const CardMatch match = matcher.findOwner(QByteArray(3, '\x01'),
            {{QStringLiteral("userA"), keyA_}}, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::IndexRejected);
        // 也不允许绑非法长度。
        QVERIFY(!matcher.bind(QStringLiteral("userA"), keyA_, QByteArray(3, '\x01'), testDerive));
    }

    void hexCaseDoesNotMatterBecauseRawBytesAreUsed() {
        // 同一张卡以不同大小写 hex 文本输入，只要解成同一串原始字节就必须匹配。
        UserDatabase db(root_());
        CardIndex matcher(&db);
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyA_)); db.close();
        QVERIFY(matcher.bind(QStringLiteral("userA"), keyA_,
                             QByteArray::fromHex("A1B2C3D4"), testDerive));
        const CardMatch match = matcher.findOwner(QByteArray::fromHex("a1b2c3d4"),
            {{QStringLiteral("userA"), keyA_}}, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::Matched);
    }

    void wrongDbKeySkipsCandidateNotMatch() {
        UserDatabase db(root_());
        CardIndex matcher(&db);
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyA_)); db.close();
        QVERIFY(matcher.bind(QStringLiteral("userA"), keyA_, kCardA, testDerive));

        // 用一个错误的 DB key 去试开 userA：应当跳过而不是误判为匹配。
        const CardMatch match = matcher.findOwner(kCardA,
            {{QStringLiteral("userA"), QByteArray(32, 'z')}}, testDerive);
        QCOMPARE(match.verdict, CardMatch::Verdict::NoMatch);
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
