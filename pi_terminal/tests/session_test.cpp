#include <QtTest>
#include "core/ConnectionManager.h"
#include "core/ModuleManager.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDir>

class Tests : public QObject {
    Q_OBJECT
    static QJsonObject moduleConfig() {
        return {{"schema", "module-transport-v1"}, {"transport_mode", "fixed_hotspot_lan"},
                {"package_transport", "data"}};
    }
    static void provideFakeHotspot(ConnectionManager &manager) {
        QObject::connect(&manager, &ConnectionManager::hotspotLeaseRequested, &manager,
            [&manager](const QString &instance) {
                QTimer::singleShot(0, &manager, [&manager, instance] {
                    manager.hotspotLeaseReady(instance, true);
                });
            });
        QObject::connect(&manager, &ConnectionManager::hotspotLeaseReleased, &manager,
            [](const QString &) {});
    }
private slots:
    void sessions() {
        ConnectionManager manager;
        manager.setActiveUserId(QStringLiteral("test-user"));
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        provideFakeHotspot(manager);
        connect(&manager, &ConnectionManager::trustRequired, &manager, [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        const QJsonObject event{{"event","attach"},{"module_id",80},{"module_instance","session-1"},{"module_config",moduleConfig()}};
        manager.attach(event);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(), 15000);
        manager.attach({{"event","detach"},{"module_id",80},{"module_instance","old-session"}});
        QVERIFY(manager.cards().first().toMap().value("contact").toBool());
        manager.attach({{"event","detach"},{"module_id",80},{"module_instance","session-1"}});
        QVERIFY(!manager.cards().first().toMap().value("contact").toBool());
        QVERIFY(manager.cards().first().toMap().value("ready").toBool());
        manager.action("session-1","start");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().first().toMap().value("message").toString().contains("42"),5000);
        manager.action("session-1","stale_data"); QTest::qWait(100);
        QVERIFY(!manager.cards().first().toMap().value("message").toString().contains("STALE"));
        manager.action("session-1","drop_link");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().first().toMap().value("ready").toBool(),2000);
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().first().toMap().value("ready").toBool(),12000);
        manager.disconnectModule("session-1");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
        // Cache reuse and a new instance must not receive old session state.
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","session-2"},{"module_config",moduleConfig()}});
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(),15000);
        manager.attach({{"event","detach"},{"module_id",80},{"module_instance","session-2"}});
        manager.action("session-2","request_disconnect"); QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
    }
    void physicalContactRejectsWirelessDisconnect() {
        ConnectionManager manager;
        manager.setActiveUserId(QStringLiteral("test-user"));
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        provideFakeHotspot(manager);
        connect(&manager,&ConnectionManager::trustRequired,&manager,[&manager](const QString &id,const QString &,const QStringList &){manager.trust(id,false);});
        connect(&manager,&ConnectionManager::pageMustClose,&manager,&ConnectionManager::releasePage,Qt::QueuedConnection);
        const QString id="contact-close-check";
        const QJsonObject attached{{"event","attach"},{"module_id",80},{"module_instance",id},{"module_config",moduleConfig()}};
        manager.attach(attached);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(),15000);
        QVERIFY(manager.cards().first().toMap().value("contact").toBool());
        manager.action(id,"request_disconnect");
        QTest::qWait(500);
        QVERIFY(!manager.cards().isEmpty());
        QVERIFY(manager.cards().first().toMap().value("ready").toBool());
        QVERIFY(manager.cards().first().toMap().value("contact").toBool());
        manager.attach({{"event","detach"},{"module_id",80},{"module_instance",id}});
        QVERIFY(!manager.cards().first().toMap().value("contact").toBool());
        manager.action(id,"request_disconnect");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
    }
    void lifecycle() {
        ModuleManager manager;
        const auto folder = qEnvironmentVariable("TEST_PLUGIN");
        for (int n=0;n<100;++n) {
            QVERIFY(manager.load("instance",80,folder,{}));
            manager.connected("instance",2); manager.data("instance",2,"{}");
            manager.unload("instance"); QVERIFY(manager.page("instance").isEmpty());
        }
            QVERIFY(!manager.load("bad-id",79,folder,{}));
    }
    void reconnectAfterDetachedDisconnect() {
        ConnectionManager manager;
        manager.setActiveUserId(QStringLiteral("test-user"));
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        provideFakeHotspot(manager);
        connect(&manager, &ConnectionManager::trustRequired, &manager, [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        const QString id = "reinsert-check";
        const QJsonObject attached{{"event","attach"},{"module_id",80},{"module_instance",id},{"module_config",moduleConfig()}};
        const QJsonObject detached{{"event","detach"},{"module_id",80},{"module_instance",id}};
        auto ready = [&manager] { return !manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(); };
        manager.attach(attached);
        QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
        // A radio-side request may not close a session while its physical
        // pogo contact is still asserted.
        manager.action(id,"request_disconnect");
        QTest::qWait(500);
        QVERIFY(ready()); QVERIFY(manager.cards().first().toMap().value("contact").toBool());
        manager.attach(attached);
        QVERIFY(ready());
        manager.attach(detached);
        manager.attach(attached);
        QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
        // User's sequence: remove contact, continue wireless, BOOT exit,
        // then reinsert. No second detach exists to clear suppression.
        manager.attach(detached);
        QVERIFY(ready());
        QVERIFY(!manager.cards().first().toMap().value("contact").toBool());
        manager.action(id,"request_disconnect");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
        manager.attach(attached);
        QTRY_VERIFY_WITH_TIMEOUT(ready(),15000);
        manager.disconnectModule(id);
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().isEmpty(),5000);
    }
    void concurrentLibraryInstances() {
        ModuleManager manager;
        QSignalSpy errors(&manager, &ModuleManager::error);
        const auto folder = qEnvironmentVariable("TEST_PLUGIN");
        for (int cycle=0; cycle<5; ++cycle) {
            for (int n=0; n<3; ++n) QVERIFY(manager.load(QString::number(n),80,folder,{}));
            manager.unload("1");
            manager.connected("0",2);manager.data("0",2,"{}");
            manager.connected("2",2);manager.data("2",2,"{}");
            manager.unload("0");manager.unload("2");
        }
        QCOMPARE(errors.count(),0);
    }
    void trustGate() {
        ConnectionManager manager;
        manager.setActiveUserId(QStringLiteral("test-user"));
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        provideFakeHotspot(manager);
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        QSignalSpy trust(&manager, &ConnectionManager::trustRequired);
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","trust-check"},{"module_config",moduleConfig()}});
        QTRY_COMPARE_WITH_TIMEOUT(trust.count(), 1, 15000);
        QTRY_COMPARE(manager.cards().first().toMap().value("state").toString(), QString("awaiting_trust"));
        QVERIFY(!manager.cards().first().toMap().value("ready").toBool());
        QVERIFY(manager.cards().first().toMap().value("page").toString().isEmpty());
        manager.disconnectModule("trust-check");
        QTRY_VERIFY(manager.cards().isEmpty());
    }
    void deniedDisconnect() {
        QFile source(qEnvironmentVariable("TEST_CONFIG")); QVERIFY(source.open(QIODevice::ReadOnly));
        auto config = QJsonDocument::fromJson(source.readAll()).object();
        auto profiles = config.value("modules").toObject(); auto profile = profiles.value("80").toObject();
        profile["allow_disconnect"] = false; profiles["80"] = profile; config["modules"] = profiles;
        QTemporaryDir dir; QFile file(dir.filePath("product.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(config).toJson()); file.close();
        ConnectionManager manager; manager.setActiveUserId(QStringLiteral("test-user"));
        QVERIFY(manager.configure(file.fileName()));
        provideFakeHotspot(manager);
        connect(&manager, &ConnectionManager::trustRequired, &manager, [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","deny-check"},{"module_config",moduleConfig()}});
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("ready").toBool(), 15000);
        manager.action("deny-check", "request_disconnect");
        QTRY_VERIFY(manager.cards().first().toMap().value("message").toString().contains("disconnect_denied"));
        QVERIFY(manager.cards().first().toMap().value("ready").toBool());
        manager.action("deny-check", "start");
        QTRY_VERIFY(manager.cards().first().toMap().value("message").toString().contains("42"));
        manager.disconnectModule("deny-check"); QTRY_VERIFY(manager.cards().isEmpty());
    }
    void failedInstallerCanRetry() {
        QFile source(qEnvironmentVariable("TEST_CONFIG")); QVERIFY(source.open(QIODevice::ReadOnly));
        auto config = QJsonDocument::fromJson(source.readAll()).object(); config["python"] = "/nonexistent/opi-python";
        QTemporaryDir dir; QFile file(dir.filePath("product.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(config).toJson()); file.close();
        ConnectionManager manager; manager.setActiveUserId(QStringLiteral("test-user"));
        QVERIFY(manager.configure(file.fileName()));
        provideFakeHotspot(manager);
        connect(&manager, &ConnectionManager::trustRequired, &manager, [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });
        connect(&manager, &ConnectionManager::pageMustClose, &manager, &ConnectionManager::releasePage, Qt::QueuedConnection);
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","retry-check"},{"module_config",moduleConfig()}});
        QTRY_VERIFY_WITH_TIMEOUT(!manager.cards().isEmpty() && manager.cards().first().toMap().value("state").toString() == "error", 15000);
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        manager.retry("retry-check");
        QTRY_VERIFY_WITH_TIMEOUT(manager.cards().first().toMap().value("ready").toBool(), 15000);
        manager.disconnectModule("retry-check"); QTRY_VERIFY(manager.cards().isEmpty());
    }
    // T-05：槽位路由在 ConnectionManager 里的接线。
    // 只覆盖**拒绝路径**，因此不需要真实模块或传输：被拒的事件在建会话之前
    // 就返回了。合法事件则继续走既有路径（此处因没有 profile 落到 error，
    // 恰好证明"路由没有误伤合法帧"）。
    void slotRoutingIntegration() {
        ConnectionManager manager;
        QSignalSpy detached(&manager, &ConnectionManager::deviceDetached);
        const QString goodGen = QStringLiteral("0123456789abcdef0123456789abcdef");
        // cards() 来自 QHash，顺序不确定；且状态经 QStateMachine 异步投递，
        // 因此按 instance 查找并用 QTRY 等待。
        auto stateOf = [&manager](const QString &instance) {
            for (const QVariant &card : manager.cards()) {
                const QVariantMap map = card.toMap();
                if (map.value("instance").toString() == instance) return map.value("state").toString();
            }
            return QString();
        };

        // 1) 非法 slot_id → 整条事件被拒，不建会话。
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","slot-bad"},
                        {"bridge_id","bridge-a"},{"slot_id",2},{"device_id","dev-1"},
                        {"binding_generation",goodGen}});
        QVERIFY2(manager.cards().isEmpty(), "invalid slot_id must not create a session");

        // 2) 非法代次（大写）→ 被拒。
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","gen-bad"},
                        {"bridge_id","bridge-a"},{"slot_id",0},{"device_id","dev-1"},
                        {"binding_generation","0123456789ABCDEF0123456789ABCDEF"}});
        QVERIFY2(manager.cards().isEmpty(), "uppercase generation must not create a session");

        // 3) 非法代次（长度不足）→ 被拒。
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","gen-short"},
                        {"bridge_id","bridge-a"},{"slot_id",0},{"device_id","dev-1"},
                        {"binding_generation","0123456789abcdef"}});
        QVERIFY2(manager.cards().isEmpty(), "short generation must not create a session");

        // 4) 槽位字段合法但**缺 device_id** → 不进入路由，按旧格式处理。
        //    没有 profile 所以落到 error，这是预期：证明合法帧没被路由误杀。
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","legacy"},
                        {"bridge_id","bridge-a"},{"slot_id",0},{"binding_generation",goodGen}});
        QCOMPARE(manager.cards().size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(QStringLiteral("legacy")), QStringLiteral("error"), 5000);

        // 5) 完全没有槽位字段 → 旧格式路径，行为不变。
        manager.attach({{"event","attach"},{"module_id",80},{"module_instance","legacy-2"}});
        QCOMPARE(manager.cards().size(), 2);

        // 6) 从未绑定过的槽上收到 detach → 路由拒绝，不得发 deviceDetached。
        detached.clear();
        manager.attach({{"event","detach"},{"module_id",80},{"module_instance","legacy"},
                        {"bridge_id","bridge-a"},{"slot_id",1},{"device_id","dev-9"},
                        {"binding_generation","ffffffffffffffffffffffffffffffff"}});
        QCOMPARE(detached.count(), 0);
        QCOMPARE(manager.cards().size(), 2);

        // 7) 旧格式 detach 仍然照原逻辑处理（不清空会话，只标触点）。
        manager.attach({{"event","detach"},{"module_id",80},{"module_instance","legacy"}});
        QCOMPARE(detached.count(), 1);
        QCOMPARE(manager.cards().size(), 2);
    }

    // 模块页 schema 版本：宿主必须在加载前拒绝不兼容页面，
    // 而不是让 QML 运行时抛一堆绑定错误。
    void unsupportedPageSchemaIsRejected() {
        ConnectionManager manager;
        manager.setActiveUserId(QStringLiteral("test-user"));
        QVERIFY(manager.configure(qEnvironmentVariable("TEST_CONFIG")));
        provideFakeHotspot(manager);
        connect(&manager, &ConnectionManager::trustRequired, &manager,
                [&manager](const QString &id, const QString &, const QStringList &) { manager.trust(id, false); });

        // 构造一个声明了未来 schema 的模块目录，直接走 ModuleManager 校验。
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject manifest{
            {"id", "schema.test"}, {"module_id", 80}, {"abi_version", 1},
            {"host_arch", QJsonArray{QSysInfo::currentCpuArchitecture()}},
            {"qt_major", 6}, {"unload_contract", "synchronous-stop-v1"},
            {"library", "libx.so"}, {"qml_page", "qml/Page.qml"},
            {"qml_schema_version", 99}};
        QFile mf(dir.filePath("manifest.json"));
        QVERIFY(mf.open(QIODevice::WriteOnly));
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();
        QDir().mkpath(dir.filePath("qml"));
        QFile page(dir.filePath("qml/Page.qml"));
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write("import QtQuick\nItem{}\n");
        page.close();

        ModuleManager modules;
        QSignalSpy errors(&modules, &ModuleManager::error);
        QVERIFY(!modules.load(QStringLiteral("schema-check"), 80, dir.path(), {}));
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.first().at(1).toString().contains(QStringLiteral("schema")));
    }

    // manifest 声称有页面但包里没有文件：必须在加载前拒绝。
    void missingModulePageIsRejected() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject manifest{
            {"id", "page.test"}, {"module_id", 80}, {"abi_version", 1},
            {"host_arch", QJsonArray{QSysInfo::currentCpuArchitecture()}},
            {"qt_major", 6}, {"unload_contract", "synchronous-stop-v1"},
            {"library", "libx.so"}, {"qml_page", "qml/Absent.qml"}};
        QFile mf(dir.filePath("manifest.json"));
        QVERIFY(mf.open(QIODevice::WriteOnly));
        mf.write(QJsonDocument(manifest).toJson());
        mf.close();

        ModuleManager modules;
        QSignalSpy errors(&modules, &ModuleManager::error);
        QVERIFY(!modules.load(QStringLiteral("page-check"), 80, dir.path(), {}));
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.first().at(1).toString().contains(QStringLiteral("missing")));
    }

};
QTEST_GUILESS_MAIN(Tests)
#include "session_test.moc"
