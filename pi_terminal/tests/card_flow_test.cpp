// T-08 / 报告 §5.1：刷卡只选用户、必须 PIN 登录、UID 不落盘、桥板失联时拒绝。
#include "core/SecurityService.h"
#include "security/CardMatcher.h"
#include "security/SessionManager.h"
#include "security/UserDatabase.h"
#include "protocol/uid_index.h"
#include "security/uid_index_key.h"

#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class CardFlowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // 整机 UID 密钥：真实实现，不是桩。
        QVERIFY(ensureUidIndexKey());
    }

    // 未配对/无归属的卡：不匹配任何用户，也不抛错。
    void unknownCardMatchesNobody()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("ORANGE_PI_WORKER_SOCKET", dir.filePath("ipc.sock").toUtf8());

        SecurityService service;
        service.setBridgeAvailable(true);
        const QVariantMap result = service.resolveCard(QStringLiteral("a1b2c3d4"));
        QVERIFY(!result.value(QStringLiteral("matched")).toBool());
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("NO_MATCH"));
    }

    // 非法长度必须失败关闭，不能退回明文比较。
    void invalidUidLengthIsRejected()
    {
        SecurityService service;
        service.setBridgeAvailable(true);
        QVERIFY(!service.resolveCard(QStringLiteral("aabbcc")).value("matched").toBool());
        QCOMPARE(service.resolveCard(QStringLiteral("aabbcc")).value("error").toString(),
                 QStringLiteral("INVALID_MESSAGE"));
        // 非 hex 也拒绝。
        QVERIFY(!service.resolveCard(QStringLiteral("zzzzzzzz")).value("matched").toBool());
    }

    // §10.3：桥板失联时位置未知，禁止新的刷卡。
    void bridgeUnavailableBlocksCardReads()
    {
        SecurityService service;
        service.setBridgeAvailable(false);
        const QVariantMap result = service.resolveCard(QStringLiteral("a1b2c3d4"));
        QVERIFY(!result.value(QStringLiteral("matched")).toBool());
        QCOMPARE(result.value(QStringLiteral("error")).toString(),
                 QStringLiteral("POSITION_UNKNOWN"));
    }

    // §5.1：刷卡只回 userId，绝不回传 PIN 或直接建立会话。
    void matchedCardOnlySelectsUser()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // 用独立映射表把一张卡绑到某个用户。
        const QString mapPath = dir.filePath("uid_map.json");
        security::CardIndex cards(mapPath);
        const QByteArray raw = QByteArray::fromHex("a1b2c3d4");
        QVERIFY(cards.bind(QStringLiteral("user-42"), raw, &deriveUidIndex));

        QVariantMap result;
        {
            // SecurityService 用自己的 CardIndex，因此这里直接把默认路径也绑一份。
            security::CardIndex defaults;
            QVERIFY(defaults.bind(QStringLiteral("user-42"), raw, &deriveUidIndex));
            SecurityService service;
            service.setBridgeAvailable(true);
            result = service.resolveCard(QStringLiteral("a1b2c3d4"));
            QVERIFY(result.value(QStringLiteral("matched")).toBool());
            QCOMPARE(result.value(QStringLiteral("userId")).toString(), QStringLiteral("user-42"));
            // 不得回传任何密钥材料。
            QVERIFY(!result.contains(QStringLiteral("pin")));
            QVERIFY(!result.contains(QStringLiteral("dbKey")));
            QVERIFY(!result.contains(QStringLiteral("token")));
        }
        // 清理默认映射表，避免影响其它用例。
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                      + QStringLiteral("/uid_map.json"));
    }

    // raw UID 不得落盘：整机映射表里只能有 HMAC 索引。
    void rawUidIsNotPersisted()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mapPath = dir.filePath("uid_map.json");
        security::CardIndex cards(mapPath);
        // 7 字节 UID，是合法长度之一。
        const QByteArray raw = QByteArray::fromHex("11223344556677");
        QVERIFY(cards.bind(QStringLiteral("user-x"), raw, &deriveUidIndex));

        QFile file(mapPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray content = file.readAll();
        file.close();
        QVERIFY2(!content.contains(raw.toHex()), "raw UID must never be stored");
        QVERIFY2(!content.contains(raw.toHex().toUpper()), "raw UID must never be stored");
    }
};

QTEST_GUILESS_MAIN(CardFlowTest)
#include "card_flow_test.moc"
