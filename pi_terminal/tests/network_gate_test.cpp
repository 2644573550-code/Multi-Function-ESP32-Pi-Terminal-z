// 报告 §9.3 / §3.2：联网总开关默认关闭，拒绝时可解释。
#include "core/NetworkGate.h"

#include <QSignalSpy>
#include <QtTest>

class NetworkGateTest : public QObject {
    Q_OBJECT

private slots:
    // 默认必须是关闭：不能"出厂就允许联网"。
    void defaultIsDisabled()
    {
        NetworkGate gate;
        QVERIFY(!gate.enabled());
        QVERIFY(!gate.allow(QStringLiteral("music.api")));
    }

    // 关闭时拒绝必须给出可读原因，且含调用方标识。
    void denialExplainsWhy()
    {
        NetworkGate gate;
        QVERIFY(!gate.allow(QStringLiteral("timesync")));
        QVERIFY2(gate.reason().contains(QStringLiteral("timesync")),
                 qPrintable(gate.reason()));
        QVERIFY(gate.reason().startsWith(QStringLiteral("network disabled")));
    }

    // 开启后放行，且清掉拒绝原因。
    void enablingAllowsAndClearsReason()
    {
        NetworkGate gate;
        QVERIFY(!gate.allow(QStringLiteral("music.api")));
        QVERIFY(!gate.reason().isEmpty());

        QSignalSpy changed(&gate, &NetworkGate::changed);
        gate.setEnabled(true);
        QCOMPARE(changed.count(), 1);
        QVERIFY(gate.allow(QStringLiteral("music.api")));
        QVERIFY(gate.reason().isEmpty());
    }

    // 关闭后立即恢复拒绝（不必等重启）。
    void disablingTakesEffectImmediately()
    {
        NetworkGate gate;
        gate.setEnabled(true);
        QVERIFY(gate.allow(QStringLiteral("music.api")));
        gate.setEnabled(false);
        QVERIFY(!gate.allow(QStringLiteral("music.api")));
        QVERIFY(!gate.reason().isEmpty());
    }

    // 重复设置同一状态不重复发信号（避免界面抖动与多余持久化）。
    void settingSameValueIsIdempotent()
    {
        NetworkGate gate;
        QSignalSpy changed(&gate, &NetworkGate::changed);
        gate.setEnabled(false);
        QCOMPARE(changed.count(), 0);
        gate.setEnabled(true);
        QCOMPARE(changed.count(), 1);
        gate.setEnabled(true);
        QCOMPARE(changed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(NetworkGateTest)
#include "network_gate_test.moc"
