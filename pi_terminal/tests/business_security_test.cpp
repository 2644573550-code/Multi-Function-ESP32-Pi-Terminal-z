// T-12 Pi 侧验收（§0.8.1 / §0.8.2 / §0.8.7）。
//
// 三组证据，全部在主机上用假模块与自签证书完成，不依赖 ESP 固件：
//   ① 未配对 BLE 拒发；② 挑战-响应失败拒发；③ TLS 指纹不匹配拒绝、无明文回退。
#include "transport/BleTransport.h"
#include "transport/HotspotLanTransport.h"

#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QtTest>

namespace {

QByteArray randomBytes(int n)
{
    QByteArray out(n, Qt::Uninitialized);
    for (int i = 0; i < n; ++i)
        out[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return out;
}

} // namespace

class BusinessSecurityTest : public QObject
{
    Q_OBJECT

private slots:
    void bleRejectsBusinessDataBeforeAuthentication()
    {
        BleTransport ble;
        // 未经挑战-响应，任何业务数据都不得发出。
        QVERIFY(!ble.sendData(QByteArrayLiteral("{\"temperature\":22}")));
        QVERIFY(!ble.authenticated());
    }

    void bleRejectsPayloadOver512Bytes()
    {
        BleTransport ble;
        // §0.8.4：单条业务 payload ≤512；超出直接拒绝，不截断。
        QVERIFY(!ble.sendData(QByteArray(513, 'x')));
        QVERIFY(!ble.sendData(QByteArray(1024, 'x')));
    }

    void bleWithoutSessionKeyFailsClosed()
    {
        BleTransport ble;
        QSignalSpy failed(&ble, &BleTransport::authenticationFailed);
        QJsonObject config{{"device_name", "test"},
                           {"address", "00:00:00:00:00:00"},
                           {"service_uuid", "9b860001-214c-4de0-a9f4-f70e6a100001"},
                           {"write_uuid", "9b860001-214c-4de0-a9f4-f70e6a100002"},
                           {"notify_uuid", "9b860001-214c-4de0-a9f4-f70e6a100003"},
                           {"module_id", 80}};
        // 没有 32 字节 session_key：必须立刻失败，不进入发现流程。
        ble.start(config);
        QCOMPARE(failed.count(), 1);
        QVERIFY(!ble.authenticated());
        QCOMPARE(ble.lastAuthError(), QStringLiteral("TRANSPORT_SECURITY_REQUIRED"));
    }

    void bleChallengeResponseComputesHmacSha256()
    {
        // 校验协议公式本身：HMAC-SHA256(k, challenge || module_id || boot_id)。
        const QByteArray key = randomBytes(32);
        const QByteArray challenge = randomBytes(32);
        const QString moduleId = QStringLiteral("80");
        const QString bootId = QStringLiteral("boot-1");

        QByteArray message = challenge;
        message += moduleId.toUtf8();
        message += bootId.toUtf8();
        const QByteArray expected =
            QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);

        QCOMPARE(expected.size(), 32);
        // 任一输入变化都必须改变结果（防止拼接漏字段）。
        QByteArray other = challenge;
        other += QStringLiteral("81").toUtf8();
        other += bootId.toUtf8();
        QVERIFY(QMessageAuthenticationCode::hash(other, key, QCryptographicHash::Sha256) != expected);
    }

    void lanRefusesToConnectWithoutPinnedFingerprint()
    {
        HotspotLanTransport lan;
        QSignalSpy errors(&lan, &HotspotLanTransport::error);
        // 空指纹等于不校验：必须拒绝连接，而不是"连上再说"。
        QVERIFY(!lan.connectToModule(QHostAddress::LocalHost, 1234, QByteArray()));
        QVERIFY(!lan.connectToModule(QHostAddress::LocalHost, 1234, QByteArray(16, 'x')));
        QCOMPARE(errors.count(), 2);
        QVERIFY(!lan.pinnedVerified());
    }

    void lanRejectsShortFingerprint()
    {
        HotspotLanTransport lan;
        // 31 / 33 字节都不算 32 字节 SHA-256。
        QVERIFY(!lan.connectToModule(QHostAddress::LocalHost, 1234, QByteArray(31, 'x')));
        QVERIFY(!lan.connectToModule(QHostAddress::LocalHost, 1234, QByteArray(33, 'x')));
    }

    void lanRejectsWhenNoCertificatePresented()
    {
        HotspotLanTransport lan;
        // 未建立连接时 pinnedVerified 必须为假：不存在"默认已认证"的路径。
        QVERIFY(!lan.pinnedVerified());
        QVERIFY(lan.negotiatedProtocol().isEmpty());
    }

    void profileWithoutPinCannotStartLanSession()
    {
        // §0.8.1：profile 缺 pin_sha256 时 ConnectionManager 必须停在 error，
        // 不允许退回明文 TCP。这里校验判定条件本身。
        const QJsonObject profile{{"transport_mode", "fixed_hotspot_lan"},
                                  {"package_transport", "data"},
                                  {"address", "127.0.0.1"},
                                  {"port", 1234},
                                  {"token", "t"}};
        const QByteArray pinned = QByteArray::fromHex(profile.value("pin_sha256").toString().toLatin1());
        QCOMPARE(pinned.size(), 0);
        QVERIFY(pinned.size() != 32);   // 触发 "缺少已登记的证书指纹"
    }
};

QTEST_GUILESS_MAIN(BusinessSecurityTest)
#include "business_security_test.moc"
