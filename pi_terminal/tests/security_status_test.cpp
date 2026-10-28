#include <QtTest>

#include "core/ConnectionManager.h"
#include "core/SecurityStatus.h"
#include "security/TrustStore.h"

#if defined(TERMINAL_HAVE_MBEDTLS)
#include "security/HostIdentity.h"
#endif

#include <QFile>
#include <QTemporaryDir>

// 界面接点的验收：四块数据都要能被读到，而且"缺失 / 损坏 / 未就绪"
// 必须是三种不同表现——页面最怕把"损坏"显示成"还没有"。
class SecurityStatusTest : public QObject
{
    Q_OBJECT

    static void writeFile(const QString &path, const QByteArray &content)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
        file.close();
    }

private slots:
    // 1. 全新机器：没有信任库、没有身份、没有 UID 密钥。
    //    三者要分别报"空"、"未生成"、"未就绪"，不能混成一个状态。
    void freshMachine()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ConnectionManager connection;
        SecurityStatus status(&connection, dir.filePath("pbr_trust.json"),
                              dir.filePath("host_identity.pem"));

        QVERIFY(status.trustStoreHealthy());
        QCOMPARE(status.trustRecords().size(), 0);
        QVERIFY(!status.uidKeyReady());
        QCOMPARE(status.identityKeyId(), 0);
        QVERIFY(status.identityFingerprint().isEmpty());
#if defined(TERMINAL_HAVE_MBEDTLS)
        QCOMPARE(status.identityState(), QStringLiteral("missing"));
#else
        QCOMPARE(status.identityState(), QStringLiteral("unavailable"));
#endif
        QCOMPARE(status.trustStorePath(), dir.filePath("pbr_trust.json"));
    }

    // 2. 槽位快照：未绑定时也返回两条（0/1），bound=false。
    void slotsAreAlwaysTwo()
    {
        QTemporaryDir dir;
        ConnectionManager connection;
        SecurityStatus status(&connection, dir.filePath("pbr_trust.json"),
                              dir.filePath("host_identity.pem"));
        const QVariantList slotList = status.slotSnapshot();
        QCOMPARE(slotList.size(), 2);
        for (int i = 0; i < slotList.size(); ++i) {
            const QVariantMap slot = slotList.at(i).toMap();
            QCOMPARE(slot.value("slot").toInt(), i);
            QCOMPARE(slot.value("bound").toBool(), false);
        }
    }

    // 2b. 槽位判定计数透传：初始为 0，页面能读到结构。
    void slotStatsPassThrough()
    {
        QTemporaryDir dir;
        ConnectionManager connection;
        SecurityStatus status(&connection, dir.filePath("pbr_trust.json"),
                              dir.filePath("host_identity.pem"));
        const QVariantMap stats = status.slotStats();
        QCOMPARE(stats.value("accepted").toULongLong(), 0ULL);
        QCOMPARE(stats.value("rejected").toULongLong(), 0ULL);
        QVERIFY(stats.value("lastRejection").toString().isEmpty());
    }

    // 3. 信任库写入后，文件监听必须把它读进来（响应式模型）。
    void trustRecordsFollowFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString trustPath = dir.filePath("pbr_trust.json");
        ConnectionManager connection;
        SecurityStatus status(&connection, trustPath, dir.filePath("host_identity.pem"));
        QSignalSpy spy(&status, &SecurityStatus::trustChanged);

        QByteArray publicKey(65, '\x01');
        publicKey[0] = char(0x04);
        {
            TrustStore writer(trustPath);
            QString error;
            QVERIFY2(writer.enrollIdentity(QStringLiteral("bridge-1"), publicKey, &error),
                     qPrintable(error));
        }

        QTRY_VERIFY_WITH_TIMEOUT(status.trustRecords().size() == 1, 5000);
        const QVariantMap record = status.trustRecords().first().toMap();
        QCOMPARE(record.value("bridgeId").toString(), QStringLiteral("bridge-1"));
        QCOMPARE(record.value("active").toBool(), true);
        QCOMPARE(record.value("keyIdText").toString().size(), 2);
        QVERIFY(spy.count() > 0);
    }

    // 4. 损坏的信任库必须报不健康，而不是"还没有配对"。
    void corruptTrustStoreIsNotHealthy()
    {
        QTemporaryDir dir;
        const QString trustPath = dir.filePath("pbr_trust.json");
        writeFile(trustPath, QByteArray("{not json"));

        ConnectionManager connection;
        SecurityStatus status(&connection, trustPath, dir.filePath("host_identity.pem"));
        QVERIFY(!status.trustStoreHealthy());
        QCOMPARE(status.trustRecords().size(), 0);
    }

    // 5. 身份：生成后状态变 ready，指纹与 key_id 都能读到。
#if defined(TERMINAL_HAVE_MBEDTLS)
    void identityBecomesReady()
    {
        QTemporaryDir dir;
        const QString identityPath = dir.filePath("host_identity.pem");
        ConnectionManager connection;
        SecurityStatus status(&connection, dir.filePath("pbr_trust.json"), identityPath);
        QSignalSpy spy(&status, &SecurityStatus::identityChanged);
        QCOMPARE(status.identityState(), QStringLiteral("missing"));

        {
            HostIdentity writer(identityPath);
            QVERIFY(writer.generate());
        }

        QTRY_COMPARE_WITH_TIMEOUT(status.identityState(), QStringLiteral("ready"), 5000);
        QCOMPARE(status.identityFingerprint().size(), 16);   // 8 字节 → 16 hex
        QVERIFY(status.identityKeyId() >= 0 && status.identityKeyId() <= 255);
        QVERIFY(spy.count() > 0);
    }

    // 6. 身份文件损坏：报 corrupt，不是 missing。
    void corruptIdentityIsReported()
    {
        QTemporaryDir dir;
        const QString identityPath = dir.filePath("host_identity.pem");
        writeFile(identityPath, QByteArray("not a pem"));
        ConnectionManager connection;
        SecurityStatus status(&connection, dir.filePath("pbr_trust.json"), identityPath);
        QTRY_COMPARE_WITH_TIMEOUT(status.identityState(), QStringLiteral("corrupt"), 5000);
        QVERIFY(status.identityFingerprint().isEmpty());
        QCOMPARE(status.identityKeyId(), 0);
    }
#endif

    // 7. refresh() 是文件监听之外的兜底：外部改动后手动拉一次也能同步。
    void manualRefreshPicksUpChanges()
    {
        QTemporaryDir dir;
        const QString trustPath = dir.filePath("pbr_trust.json");
        ConnectionManager connection;
        SecurityStatus status(&connection, trustPath, dir.filePath("host_identity.pem"));
        QCOMPARE(status.trustRecords().size(), 0);

        QByteArray publicKey(65, '\x02');
        publicKey[0] = char(0x04);
        TrustStore writer(trustPath);
        QVERIFY(writer.enrollIdentity(QStringLiteral("bridge-2"), publicKey, nullptr));

        status.refresh();
        QCOMPARE(status.trustRecords().size(), 1);
        QCOMPARE(status.trustRecords().first().toMap().value("bridgeId").toString(),
                 QStringLiteral("bridge-2"));
    }
};

QTEST_GUILESS_MAIN(SecurityStatusTest)
#include "security_status_test.moc"
