// IT-014 的 Z 侧门控部分（§0.8.5 / §0.8.4）。
// 传输层（BleTransport/HotspotLanTransport）归 D，这里只测会话门控。
#include "core/LaneGate.h"
#include "core/TransportPolicy.h"

#include <QSignalSpy>
#include <QtTest>

class LaneGateTest : public QObject {
    Q_OBJECT

private slots:
    void noSessionRejectsBusinessData() {
        LaneGate gate;
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Lan), LaneGate::Verdict::RejectedNoSession);
    }

    void switchRequiresReAuthentication() {
        LaneGate gate;
        gate.beginAttach(QStringLiteral("u1"), QStringLiteral("m-1"));
        // 未认证的 lane 不能接业务数据。
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Lan), LaneGate::Verdict::RejectedNotAuthed);

        gate.markAuthenticated(LaneGate::Lane::Lan);
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Lan), LaneGate::Verdict::Accepted);

        // LAN 断开后切 BLE：BLE 还没认证，必须拒绝。
        gate.dropLane(LaneGate::Lane::Lan);
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Ble), LaneGate::Verdict::RejectedNotAuthed);
        gate.markAuthenticated(LaneGate::Lane::Ble);
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Ble), LaneGate::Verdict::Accepted);
    }

    void staleAuthenticationIsDroppedOnDisconnect() {
        LaneGate gate;
        gate.beginAttach(QStringLiteral("u1"), QStringLiteral("m-1"));
        gate.markAuthenticated(LaneGate::Lane::Ble);
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Ble), LaneGate::Verdict::Accepted);
        gate.dropLane(LaneGate::Lane::Ble);
        // 断线后不得搬用旧认证直接切回。
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Ble), LaneGate::Verdict::RejectedNotAuthed);
    }

    void atMostThreeSwitchesPerAttach() {
        LaneGate gate;
        gate.beginAttach(QStringLiteral("u1"), QStringLiteral("m-1"));
        auto switchTo = [&](LaneGate::Lane lane) {
            gate.markAuthenticated(lane);
            return gate.requestSwitch(lane);
        };
        QCOMPARE(switchTo(LaneGate::Lane::Lan), LaneGate::Verdict::Accepted);   // 1
        QCOMPARE(switchTo(LaneGate::Lane::Ble), LaneGate::Verdict::Accepted);   // 2
        QCOMPARE(switchTo(LaneGate::Lane::Lan), LaneGate::Verdict::Accepted);   // 3
        // 第 4 次必须失败。
        QCOMPARE(switchTo(LaneGate::Lane::Ble), LaneGate::Verdict::RejectedTooMany);
    }

    void newAttachResetsSwitchCount() {
        LaneGate gate;
        gate.beginAttach(QStringLiteral("u1"), QStringLiteral("m-1"));
        for (int i = 0; i < 3; ++i) {
            gate.markAuthenticated(i % 2 ? LaneGate::Lane::Ble : LaneGate::Lane::Lan);
            gate.requestSwitch(i % 2 ? LaneGate::Lane::Ble : LaneGate::Lane::Lan);
        }
        QCOMPARE(gate.switchCount(), 3);
        // 重新 attach 后计数重置。
        gate.beginAttach(QStringLiteral("u1"), QStringLiteral("m-1"));
        QCOMPARE(gate.switchCount(), 0);
    }

    void invalidateBlocksFurtherSwitching() {
        LaneGate gate;
        gate.beginAttach(QStringLiteral("u1"), QStringLiteral("m-1"));
        gate.markAuthenticated(LaneGate::Lane::Lan);
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Lan), LaneGate::Verdict::Accepted);

        QSignalSpy invalidated(&gate, &LaneGate::invalidated);
        gate.invalidateAll(QStringLiteral("user switched"));
        QCOMPARE(invalidated.count(), 1);
        // 撤权后旧认证作废，下一条业务数据进不来。
        QCOMPARE(gate.requestSwitch(LaneGate::Lane::Lan), LaneGate::Verdict::RejectedNotAuthed);
    }

    void payloadLimitIs512Bytes() {
        QVERIFY(LaneGate::payloadAcceptable(0));
        QVERIFY(LaneGate::payloadAcceptable(512));
        // 超出返回 INVALID_MESSAGE（由调用方映射）。
        QVERIFY(!LaneGate::payloadAcceptable(513));
        QVERIFY(!LaneGate::payloadAcceptable(-1));
    }

    void fixedBleWithHotspotPackageIsValid() {
        // §0.8.6：业务走 BLE、插件包走 LAN 是合法组合，不得按「两类混用」拒绝。
        const QJsonObject profile{{"transport_mode", "fixed_ble"}, {"package_transport", "hotspot_lan"}};
        const TransportPolicy policy = TransportPolicy::fromProfile(profile);
        QVERIFY(policy.valid());
        QVERIFY(policy.allowsBle());
        QVERIFY(policy.packageNeedsHotspot());
    }
};

QTEST_GUILESS_MAIN(LaneGateTest)
#include "lane_gate_test.moc"
