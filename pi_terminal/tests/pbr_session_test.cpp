// T-07：序号单调性、事件去重、TTL。对应 §6.2 第 5～7 步。
//
// 这里特别锁住一条安全不变量：**tag 必须先验，序号才可信**。
// 测试直接模拟「伪造大序号但 tag 无效」，断言序号窗口不推进。
#include "protocol/pbr_session.h"

#include <QtTest>

class PbrSessionTest : public QObject {
    Q_OBJECT

private slots:
    // §5.2：从 1 开始，0 与回退都是 REPLAY。
    void sequenceMustStrictlyIncrease()
    {
        ReplayGuard guard;
        QCOMPARE(guard.check(0), ReplayGuard::Verdict::ZeroSequence);
        QCOMPARE(guard.check(1), ReplayGuard::Verdict::Accepted);
        guard.advance(1);
        QCOMPARE(guard.check(1), ReplayGuard::Verdict::Replayed);   // 等值也拒
        QCOMPARE(guard.check(2), ReplayGuard::Verdict::Accepted);
        guard.advance(2);
        QCOMPARE(guard.check(1), ReplayGuard::Verdict::Replayed);   // 回退
    }

    // 关键安全用例（T07-15 的等价实现）：
    // 伪造 auth_seq = 2^63 但 tag 无效 → 序号窗口**不得推进**。
    void invalidTagDoesNotAdvanceSequence()
    {
        ReplayGuard guard;
        guard.advance(10);

        // 攻击者发一个超大序号的伪造帧。因为 tag 验不过，调用方不会
        // 走到 advance()。这里断言 check() 本身只是"查询"，不改状态。
        const auto forged = guard.check(quint64(1) << 63);
        QVERIFY(forged != ReplayGuard::Verdict::Accepted);
        // 窗口未动：合法帧 11 仍然可接受。
        QCOMPARE(guard.lastAccepted(), quint64(10));
        QCOMPARE(guard.check(11), ReplayGuard::Verdict::Accepted);
    }

    // 跳跃过大的序号按异常拒绝，防止用超大值把窗口推高。
    void hugeJumpIsRejected()
    {
        ReplayGuard guard(/*maxAdvance=*/100);
        guard.advance(1);
        QCOMPARE(guard.check(70), ReplayGuard::Verdict::Accepted);
        QCOMPARE(guard.check(1000), ReplayGuard::Verdict::TooFarAhead);
        // 被拒的帧不得改变状态。
        QCOMPARE(guard.lastAccepted(), quint64(1));
    }

    // §5.2：接近 2^64-1 必须重新握手，禁止环绕。
    void rehandshakeBeforeWrapAround()
    {
        ReplayGuard guard;
        guard.advance(0xffffffffffffffffULL - (1u << 20) - 1);
        QVERIFY(!guard.needsRehandshake());
        guard.advance(0xffffffffffffffffULL - 1);
        QVERIFY(guard.needsRehandshake());
    }

    void resetClearsSequence()
    {
        ReplayGuard guard;
        guard.advance(100);
        guard.reset();
        QCOMPARE(guard.lastAccepted(), quint64(0));
        QCOMPARE(guard.check(1), ReplayGuard::Verdict::Accepted);
    }

    // §6.2 第 7 步：重复 event_id 只分派一次。
    void duplicateEventIsDetectedOnce()
    {
        EventDedup dedup;
        const QString key = QStringLiteral("bridge-1|boot-1|session-1|7");
        QCOMPARE(dedup.check(key), EventDedup::Verdict::FirstTime);
        QCOMPARE(dedup.check(key), EventDedup::Verdict::Duplicate);
        // 不同 event_id 是不同事件。
        QCOMPARE(dedup.check(QStringLiteral("bridge-1|boot-1|session-1|8")),
                 EventDedup::Verdict::FirstTime);
    }

    // 表满必须报 TableFull（调用方重建会话），不能静默丢事件。
    void fullTableReportsRebuildInsteadOfSilentDrop()
    {
        EventDedup dedup(2);
        QCOMPARE(dedup.check(QStringLiteral("a")), EventDedup::Verdict::FirstTime);
        QCOMPARE(dedup.check(QStringLiteral("b")), EventDedup::Verdict::FirstTime);
        QCOMPARE(dedup.check(QStringLiteral("c")), EventDedup::Verdict::TableFull);
        QCOMPARE(dedup.size(), 2);
        // 已存在的事件在表满时仍然报 Duplicate，而不是 TableFull ——
        // 否则重放会伪装成"需要重建"。
        QCOMPARE(dedup.check(QStringLiteral("a")), EventDedup::Verdict::Duplicate);
    }

    // §6.4：ttl_ms 必须等于 3000。
    void ttlFieldMustBeExactly3000()
    {
        QCOMPARE(EventTtl::evaluate(3000, 1000, 1000, true), EventTtl::Verdict::Ok);
        QCOMPARE(EventTtl::evaluate(2999, 1000, 1000, true), EventTtl::Verdict::BadTtlField);
        QCOMPARE(EventTtl::evaluate(3001, 1000, 1000, true), EventTtl::Verdict::BadTtlField);
        QCOMPARE(EventTtl::evaluate(0, 1000, 1000, true), EventTtl::Verdict::BadTtlField);
    }

    // 过期与缺时钟必须区分：缺基准不能被当成"新鲜"。
    void expiredAndMissingClockAreDistinct()
    {
        QCOMPARE(EventTtl::evaluate(3000, 1000, 1000 + 3001, true), EventTtl::Verdict::Expired);
        QCOMPARE(EventTtl::evaluate(3000, 1000, 1000, false), EventTtl::Verdict::MissingClock);
    }
};

QTEST_GUILESS_MAIN(PbrSessionTest)
#include "pbr_session_test.moc"
