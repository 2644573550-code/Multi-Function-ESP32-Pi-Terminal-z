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

    // §9.3 第 2 条：分功能开关只在总开关打开后生效。
    void perFeatureSwitchesRequireMasterOn()
    {
        NetworkGate gate;
        gate.setBrowserAllowed(true);
        // 总开关关闭时，即使分功能开也拒绝。
        QVERIFY(!gate.allow(NetworkGate::Purpose::Browser));

        gate.setEnabled(true);
        QVERIFY(gate.allow(NetworkGate::Purpose::Browser));

        gate.setBrowserAllowed(false);
        QVERIFY(!gate.allow(NetworkGate::Purpose::Browser));
        QVERIFY(gate.reason().contains(QStringLiteral("browser")));
        // 其他功能不受影响。
        QVERIFY(gate.allow(NetworkGate::Purpose::Music));
    }

    // §9.3 第 3 条：三态互不推导。接口有 IP 不代表互联网可达。
    void interfaceReadyDoesNotImplyInternet()
    {
        NetworkGate gate;
        gate.setEnabled(true);
        gate.setInterfaceReady(true);
        QVERIFY(gate.interfaceReady());
        QVERIFY2(!gate.internetReachable(), "interface up must not imply internet");
        // 局域模块链路是第三态，与互联网无关。
        gate.setModuleLinkReady(true);
        QVERIFY(gate.moduleLinkReady());
        QVERIFY(!gate.internetReachable());
        // 只有显式置位才算可达。
        gate.setInternetReachable(true);
        QVERIFY(gate.internetReachable());
    }

    // §9.3 第 6 条：更新下载默认关闭，避免网络恢复即自动拉大包。
    void updateDownloadsDefaultOff()
    {
        NetworkGate gate;
        gate.setEnabled(true);
        QVERIFY2(!gate.updatesAllowed(), "update downloads must default to off");
        QVERIFY(!gate.allow(NetworkGate::Purpose::Updates));
        QVERIFY(gate.reason().contains(QStringLiteral("updates")));
    }

    // §9.3 第 2 条：关闭总开关时进行中的请求必须被丢弃。
    void disablingDropsInFlightRequests()
    {
        NetworkGate gate;
        gate.setEnabled(true);
        gate.beginRequest();
        gate.beginRequest();
        QCOMPARE(gate.inFlight(), 2);

        gate.setEnabled(false);
        QCOMPARE(gate.inFlight(), 0);
    }

    // 请求计数不得为负（重复 end 不能把计数打穿）。
    void requestCounterNeverGoesNegative()
    {
        NetworkGate gate;
        gate.endRequest();
        QCOMPARE(gate.inFlight(), 0);
        gate.beginRequest();
        QCOMPARE(gate.inFlight(), 1);
        gate.endRequest();
        gate.endRequest();
        QCOMPARE(gate.inFlight(), 0);
    }

    // 校时不阻断离线功能：即使总开关关闭，调用方也只是拿到 false，
    // 不抛错、不阻塞；这条锁住"离线健康功能照常工作"的前提。
    void timeSyncDenialIsGraceful()
    {
        NetworkGate gate;
        QVERIFY(!gate.allow(NetworkGate::Purpose::TimeSync));
        QVERIFY(gate.reason().contains(QStringLiteral("timesync")));
        // 打开后校时放行。
        gate.setEnabled(true);
        QVERIFY(gate.allow(NetworkGate::Purpose::TimeSync));
    }

};

QTEST_GUILESS_MAIN(NetworkGateTest)
#include "network_gate_test.moc"
