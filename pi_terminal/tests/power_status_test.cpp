// 报告 §13 + §3.2：电池遥测的 Pi 侧语义。
// P0 没有数据源，因此重点是「未知就是未知」，不能伪造读数或据此关机。
#include "core/PowerStatus.h"

#include <QtTest>

using namespace Power;

class PowerStatusTest : public QObject {
    Q_OBJECT

private slots:
    // 默认必须是 unknown，不能猜成电池或外接。
    void defaultIsUnknown()
    {
        PowerStatus status;
        QCOMPARE(status.sourceName(), QStringLiteral("unknown"));
        QCOMPARE(status.chargeName(), QStringLiteral("unknown"));
        QVERIFY(!status.known());
        QCOMPARE(status.percent(), -1);   // -1 = 没有可信百分比
    }

    // §3.2：未知电量绝不能触发自动关机。
    void unknownPowerForbidsAutoShutdown()
    {
        Status status;
        QVERIFY(!status.mayAutoShutdown());

        // 知道是电池但不知道百分比 —— 仍然不许关机。
        status.source = Source::Battery;
        status.hasPercent = false;
        QVERIFY(!status.mayAutoShutdown());

        // 百分比越界同样不许。
        status.hasPercent = true;
        status.percent = 150;
        QVERIFY(!status.mayAutoShutdown());

        // 外接供电也不由电量关机。
        status.source = Source::External;
        status.percent = 5;
        QVERIFY(!status.mayAutoShutdown());

        // 只有电池 + 可信百分比才允许。
        status.source = Source::Battery;
        status.percent = 5;
        QVERIFY(status.mayAutoShutdown());
    }

    // 越界百分比必须被丢弃，界面不能显示假读数。
    void invalidPercentIsDropped()
    {
        PowerStatus status;
        Status next;
        next.source = Source::Battery;
        next.hasPercent = true;
        next.percent = 120;
        status.update(next);
        QVERIFY(!status.status().hasPercent);
        QCOMPARE(status.percent(), -1);
    }

    // 接入真实遥测后，状态与百分比要能如实反映。
    void knownStatusIsReported()
    {
        PowerStatus status;
        QSignalSpy changed(&status, &PowerStatus::changed);

        Status next;
        next.source = Source::External;
        next.charge = ChargeState::Charging;
        next.hasPercent = true;
        next.percent = 80;
        status.update(next);

        QCOMPARE(changed.count(), 1);
        QCOMPARE(status.sourceName(), QStringLiteral("external"));
        QCOMPARE(status.chargeName(), QStringLiteral("charging"));
        QVERIFY(status.known());
        QCOMPARE(status.percent(), 80);

        // 相同状态不重复发信号。
        status.update(next);
        QCOMPARE(changed.count(), 1);
    }

    // 从已知退回未知（失联）：必须如实变成 unknown，而不是保留最后好状态。
    void lostTelemetryReturnsToUnknown()
    {
        PowerStatus status;
        Status known;
        known.source = Source::External;
        known.hasPercent = true;
        known.percent = 70;
        status.update(known);
        QVERIFY(status.known());

        status.update(Status{});
        QVERIFY(!status.known());
        QCOMPARE(status.percent(), -1);
        QVERIFY(!status.status().mayAutoShutdown());
    }
};

QTEST_GUILESS_MAIN(PowerStatusTest)
#include "power_status_test.moc"
