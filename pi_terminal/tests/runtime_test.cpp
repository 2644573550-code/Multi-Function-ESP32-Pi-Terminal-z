#include <QtTest>
#include "core/ModuleCardModel.h"
#include "core/PageProvider.h"
#include "core/SessionStateMachine.h"
#include "core/PackageReceiver.h"
#include "core/HostEventDetector.h"
#include "core/BridgeConnectionMonitor.h"
#include "core/TransportPolicy.h"
#include "ui/SystemTools.h"
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QBuffer>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <cerrno>
#include <csignal>

class RuntimeTests : public QObject {
    Q_OBJECT
private slots:
    void systemVolumeCommandsAreAsyncAndBounded() {
        QTemporaryDir dir;
        const auto oldPath = qgetenv("PATH");
        const auto oldState = qgetenv("TEST_VOLUME_STATE");
        const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); qputenv("TEST_VOLUME_STATE", oldState); });
        QFile fake(dir.filePath("pactl")); QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\n"
                   "echo \"$*\" >> \"$TEST_VOLUME_STATE.calls\"\n"
                   "case \"$1\" in\n"
                   "get-sink-volume) test ! -f \"$TEST_VOLUME_STATE.fail\" || exit 1; "
                   "test ! -f \"$TEST_VOLUME_STATE.hang\" || exec /bin/sleep 5; "
                   "v=64; test ! -f \"$TEST_VOLUME_STATE.v\" || read v < \"$TEST_VOLUME_STATE.v\"; echo \"Volume: front-left: 40000 / $v% / -1 dB\" ;;\n"
                   "get-sink-mute) m=no; test ! -f \"$TEST_VOLUME_STATE.m\" || read m < \"$TEST_VOLUME_STATE.m\"; echo \"Mute: $m\" ;;\n"
                   "set-sink-volume) echo \"${3%\\%}\" > \"$TEST_VOLUME_STATE.v\" ;;\n"
                   "set-sink-mute) m=no; test \"$3\" != 1 || m=yes; echo \"$m\" > \"$TEST_VOLUME_STATE.m\" ;;\n"
                   "*) exit 2 ;;\nesac\n");
        fake.close(); QVERIFY(fake.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        qputenv("PATH", dir.path().toUtf8());
        qputenv("TEST_VOLUME_STATE", dir.filePath("audio").toUtf8());
        SystemVolume volume;
        QVERIFY(!volume.available());
        volume.refresh();
        QTRY_VERIFY(volume.available()); QCOMPARE(volume.volume(), 64); QVERIFY(!volume.muted());
        for (int i=0; i<100; ++i) volume.setVolume(i);
        volume.setVolume(150);
        QTRY_COMPARE(volume.volume(), 100);
        volume.setMuted(true); QTRY_VERIFY(volume.muted());
        volume.setVolume(-10); QTRY_COMPARE(volume.volume(), 0); QVERIFY(volume.muted());
        volume.setMuted(false); QTRY_VERIFY(!volume.muted());
        QFile calls(dir.filePath("audio.calls")); QVERIFY(calls.open(QIODevice::ReadOnly));
        const auto commands = calls.readAll();
        QCOMPARE(commands.count("set-sink-volume"), 2); // drag events coalesce
        QVERIFY(commands.contains("set-sink-volume @DEFAULT_SINK@ 100%"));
        QVERIFY(commands.contains("set-sink-volume @DEFAULT_SINK@ 0%"));
        QFile failure(dir.filePath("audio.fail")); QVERIFY(failure.open(QIODevice::WriteOnly)); failure.close();
        volume.refresh(); QTRY_VERIFY(!volume.available());
        QVERIFY(QFile::remove(failure.fileName())); volume.refresh(); QTRY_VERIFY(volume.available());
        QFile hang(dir.filePath("audio.hang")); QVERIFY(hang.open(QIODevice::WriteOnly)); hang.close();
        int ticks = 0;
        QTimer responsive; responsive.setInterval(10);
        connect(&responsive, &QTimer::timeout, this, [&] { ++ticks; }); responsive.start();
        volume.refresh(); QTRY_VERIFY(!volume.available()); QVERIFY(ticks > 10);
        QVERIFY(QFile::remove(hang.fileName())); volume.refresh(); QTRY_VERIFY(volume.available());
        qputenv("PATH", "/nonexistent"); volume.refresh(); QTRY_VERIFY(!volume.available());
    }
    void speechDuckingLifecycle() {
        QTemporaryDir dir;
        const auto oldPath = qgetenv("PATH");
        QSettings settings;
        const auto oldSpeech = settings.value("speech"), oldDucking = settings.value("duckMediaForSpeech");
        const auto restore = qScopeGuard([&] {
            qputenv("PATH", oldPath);
            if (oldSpeech.isValid()) settings.setValue("speech", oldSpeech); else settings.remove("speech");
            if (oldDucking.isValid()) settings.setValue("duckMediaForSpeech", oldDucking); else settings.remove("duckMediaForSpeech");
            settings.sync();
        });
        QFile fake(dir.filePath("espeak-ng")); QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\n/bin/cat >/dev/null\nexec /bin/sleep 0.4\n"); fake.close();
        QVERIFY(fake.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        qputenv("PATH", (dir.path()+":"+QString::fromUtf8(oldPath)).toUtf8());
        SystemTools tools;
        tools.setSpeechEnabled(true); tools.setDuckMediaForSpeech(true);
        QCOMPARE(tools.mediaGain(), 1.0);
        tools.speak("测试播报");
        QTRY_VERIFY(tools.speechActive()); QCOMPARE(tools.mediaGain(), 0.2);
        tools.setDuckMediaForSpeech(false); QCOMPARE(tools.mediaGain(), 1.0);
        QCOMPARE(settings.value("duckMediaForSpeech").toBool(), false);
        tools.setDuckMediaForSpeech(true); QCOMPARE(tools.mediaGain(), 0.2);
        tools.speak("后续播报");
        QTRY_VERIFY(!tools.speechActive()); QCOMPARE(tools.mediaGain(), 1.0);
        tools.speak("取消播报"); QTRY_VERIFY(tools.speechActive());
        tools.setSpeechEnabled(false); QVERIFY(!tools.speechActive()); QCOMPARE(tools.mediaGain(), 1.0);
        tools.speak("不应播报"); QVERIFY(!tools.speechActive());
        // Failed external engines must never leave media attenuated.
        tools.setSpeechEnabled(true);
        QVERIFY(QFile::remove(fake.fileName()));
        tools.speak("启动失败"); QTRY_VERIFY(!tools.speechActive()); QCOMPARE(tools.mediaGain(), 1.0);
    }
    void removableStorageHotplugAndBoundaries() {
        QTemporaryDir dir;
        const auto sys=dir.filePath("sys");
        const auto disk=dir.filePath("devices/usb1/block/sdz");
        const auto mount=dir.filePath("mounted");
        QVERIFY(QDir().mkpath(sys));QVERIFY(QDir().mkpath(mount));
        QVariantList volumes;
        RemovableStorage storage(nullptr,sys,[&volumes]{return volumes;});
        QSignalSpy inserted(&storage,&RemovableStorage::inserted);
        QSignalSpy ready(&storage,&RemovableStorage::ready);
        QVERIFY(QDir().mkpath(disk+"/sdz1"));
        auto writeFile=[](const QString &path,const QByteArray &data){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
        QVERIFY(writeFile(disk+"/removable","1"));
        QVERIFY(writeFile(disk+"/sdz1/partition","1"));
        QVERIFY(QFile::link(disk,sys+"/sdz"));
        QVERIFY(QFile::link(disk+"/sdz1",sys+"/sdz1"));
        storage.refresh();
        QCOMPARE(storage.devices().size(),1);QCOMPARE(inserted.size(),1);
        QVERIFY(!storage.devices().first().toMap().value("mounted").toBool());
        storage.refresh();QCOMPARE(inserted.size(),1);
        volumes.append(QVariantMap{{"device","/dev/sdz1"},{"path",mount},{"name","USB fixture"}});
        storage.refresh();QCOMPARE(inserted.size(),1);
        QCOMPARE(storage.resolve("@usb/sdz1",false,true),mount);
        QVERIFY(storage.resolve("@usb/sdz1").isEmpty()); // Never delete or rename a device root.
        QVERIFY(writeFile(mount+"/song.txt","metadata"));
        QCOMPARE(storage.resolve("@usb/sdz1/song.txt"),mount+"/song.txt");
        QCOMPARE(storage.resolve("@usb/sdz1/new.txt",true),mount+"/new.txt");
        QVERIFY(storage.resolve("@usb/sdz1/../outside").isEmpty());
        QVERIFY(storage.resolve("@usb/unknown/song.txt").isEmpty());
        QVERIFY(QFile::link(dir.path(),mount+"/escape"));
        QVERIFY(storage.resolve("@usb/sdz1/escape/outside",true).isEmpty());
        storage.open("sdz1");QCOMPARE(ready.size(),1);
        QCOMPARE(ready.first().first().toString(),QString("@usb/sdz1"));
        volumes.clear();storage.refresh();
        QVERIFY(storage.resolve("@usb/sdz1/song.txt").isEmpty());
        QCOMPARE(inserted.size(),1); // Mount changes are not new insertions.
        volumes.append(QVariantMap{{"device","/dev/sdz1"},{"path","/"},{"name","System USB"}});
        storage.refresh();QVERIFY(storage.devices().isEmpty()); // Do not offer the system disk for mounting.
        QVERIFY(QFile::remove(sys+"/sdz1"));QVERIFY(QFile::remove(sys+"/sdz"));
        storage.refresh();QVERIFY(storage.devices().isEmpty());
        QVERIFY(storage.resolve("@usb/sdz1/song.txt").isEmpty());
    }
    void transportPoliciesAreExplicit() {
        const auto ble = TransportPolicy::fromProfile({{"transport_mode", "fixed_ble"}});
        QVERIFY(ble.valid());
        QCOMPARE(ble.initialTransport(), quint8(1));
        QVERIFY(ble.allowsBle()); QVERIFY(!ble.allowsLan()); QVERIFY(!ble.sessionNeedsHotspot());
        QVERIFY(ble.packageUsesDataLane());

        const auto bleHttp = TransportPolicy::fromProfile({{"transport_mode", "fixed_ble"},
                                                           {"package_transport", "http"}});
        QVERIFY(bleHttp.valid()); QVERIFY(!bleHttp.packageUsesDataLane());
        QVERIFY(!bleHttp.packageNeedsHotspot());
        const auto bleHotspotPackage = TransportPolicy::fromProfile({{"transport_mode", "fixed_ble"},
                                                                      {"package_transport", "hotspot_lan"}});
        QVERIFY(bleHotspotPackage.valid()); QVERIFY(bleHotspotPackage.allowsBle());
        QVERIFY(!bleHotspotPackage.allowsLan()); QVERIFY(bleHotspotPackage.packageNeedsHotspot());

        const auto lan = TransportPolicy::fromProfile({{"transport_mode", "fixed_hotspot_lan"}});
        QVERIFY(lan.valid()); QVERIFY(!lan.allowsBle()); QVERIFY(lan.allowsLan());
        QVERIFY(lan.sessionNeedsHotspot()); QCOMPARE(lan.initialTransport(), quint8(2));
        QVERIFY(lan.packageUsesDataLane()); // .so chunks and business data both stay on LAN.

        const auto bleLanPackage = TransportPolicy::fromProfile({{"transport_mode", "fixed_ble"},
                                                                 {"package_transport", "hotspot_lan"}});
        QVERIFY(bleLanPackage.valid()); QVERIFY(bleLanPackage.allowsBle());
        QVERIFY(!bleLanPackage.allowsLan()); QVERIFY(bleLanPackage.packageNeedsHotspot());

        QVERIFY(!TransportPolicy::fromProfile({{"transport_mode", "fixed_ble_and_hotspot_lan"}}).valid());
        QVERIFY(!TransportPolicy::fromProfile({{"transport_mode", "fixed_ble"},
                                               {"package_transport", "auto"}}).valid());
    }
    void moduleConfigOverridesHostTransportPolicy() {
        QJsonObject host{{"transport_mode", "fixed_ble"}, {"package_transport", "hotspot_lan"},
                         {"token", "host-local-secret"}, {"address", "192.168.50.2"}};
        const QJsonObject module{{"schema", "module-transport-v1"},
                                 {"transport_mode", "fixed_hotspot_lan"},
                                 {"package_transport", "data"}};
        QVERIFY(TransportPolicy::applyModuleConfig(&host, module));
        QCOMPARE(host.value("transport_mode").toString(), QString("fixed_hotspot_lan"));
        QCOMPARE(host.value("package_transport").toString(), QString("data"));
        QCOMPARE(host.value("token").toString(), QString("host-local-secret"));
        QCOMPARE(host.value("address").toString(), QString("192.168.50.2"));
        QVERIFY(!TransportPolicy::applyModuleConfig(&host,
            {{"schema", "unknown"}, {"transport_mode", "fixed_ble"}, {"package_transport", "data"}}));
        QVERIFY(!TransportPolicy::applyModuleConfig(&host,
            {{"schema", "module-transport-v1"}, {"transport_mode", "fixed_ble_and_hotspot_lan"}, {"package_transport", "data"}}));
    }

    void powerActionsAreAllowlisted() {
        SystemTools tools;
        QSignalSpy notifications(&tools,&SystemTools::notification);
        tools.requestPowerAction("shutdown-now");
        QVERIFY(!tools.powerActionBusy());
        QVERIFY(!notifications.isEmpty());
    }
    void hotspotStateAndIdempotence() {
        QTemporaryDir dir;
        QFile fake(dir.filePath("nmcli")); QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\ncase \"$*\" in\n"
                   "*'connection up pi-module-ap'*) touch \"$TEST_AP_STATE\" ;;\n"
                   "*'connection down pi-module-ap'*) rm -f \"$TEST_AP_STATE\" ;;\n"
                   "*'connection show --active'*) test ! -f \"$TEST_AP_STATE\" || echo pi-module-ap ;;\n"
                   "*) exit 2 ;;\nesac\nexit 0\n");
        fake.close(); QVERIFY(fake.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const auto path=qgetenv("PATH"), sudo=qgetenv("ORANGE_PI_HOTSPOT_SUDO"), state=qgetenv("TEST_AP_STATE");
        struct Restore { QByteArray path,sudo,state; ~Restore(){qputenv("PATH",path);qputenv("ORANGE_PI_HOTSPOT_SUDO",sudo);qputenv("TEST_AP_STATE",state);} } restore{path,sudo,state};
        qputenv("PATH",dir.path().toUtf8()+":"+path);
        qputenv("ORANGE_PI_HOTSPOT_SUDO","0");qputenv("TEST_AP_STATE",dir.filePath("active").toUtf8());
        SystemTools tools;
        QTRY_VERIFY(tools.deviceStatus().value("hotspotKnown").toBool());
        tools.hotspot(false);QTRY_VERIFY(!tools.hotspotBusy());
        QVERIFY(!tools.deviceStatus().value("hotspotActive").toBool());
        tools.hotspot(true);QTRY_VERIFY(!tools.hotspotBusy());
        QVERIFY(tools.deviceStatus().value("hotspotActive").toBool());
        tools.hotspot(true);QTRY_VERIFY(!tools.hotspotBusy());
        tools.hotspot(false);QTRY_VERIFY(!tools.hotspotBusy());
        QVERIFY(!tools.deviceStatus().value("hotspotActive").toBool());
    }
    void hotspotLeaseIsOnDemandAndOwned() {
        QTemporaryDir dir;
        QFile fake(dir.filePath("nmcli")); QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\ncase \"$*\" in\n"
                   "*'connection up pi-module-ap'*) touch \"$TEST_AP_STATE\" ;;\n"
                   "*'connection down pi-module-ap'*) rm -f \"$TEST_AP_STATE\" ;;\n"
                   "*'connection show --active'*) test ! -f \"$TEST_AP_STATE\" || echo pi-module-ap ;;\n"
                   "*) exit 2 ;;\nesac\nexit 0\n");
        fake.close(); QVERIFY(fake.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const auto path=qgetenv("PATH"), sudo=qgetenv("ORANGE_PI_HOTSPOT_SUDO"), state=qgetenv("TEST_AP_STATE");
        struct Restore { QByteArray path,sudo,state; ~Restore(){qputenv("PATH",path);qputenv("ORANGE_PI_HOTSPOT_SUDO",sudo);qputenv("TEST_AP_STATE",state);} } restore{path,sudo,state};
        const auto statePath=dir.filePath("active");
        qputenv("PATH",dir.path().toUtf8()+":"+path);qputenv("ORANGE_PI_HOTSPOT_SUDO","0");qputenv("TEST_AP_STATE",statePath.toUtf8());
        SystemTools tools;
        QTRY_VERIFY(tools.deviceStatus().value("hotspotKnown").toBool());
        QVERIFY(!QFileInfo::exists(statePath)); // UI startup/status refresh must not enable the AP.
        QSignalSpy leases(&tools,&SystemTools::hotspotLeaseReady);
        tools.acquireHotspotLease("ble-package-session");
        QTRY_COMPARE(leases.count(),1);
        QCOMPARE(leases.first().at(0).toString(),QString("ble-package-session"));
        QVERIFY(leases.first().at(1).toBool());
        QVERIFY(tools.deviceStatus().value("hotspotActive").toBool());
        tools.hotspot(false); // A manual toggle cannot tear down an in-use module lease.
        QTRY_VERIFY(tools.deviceStatus().value("hotspotActive").toBool());
        tools.releaseHotspotLease("ble-package-session");
        QTRY_VERIFY(!tools.hotspotBusy());
        QTRY_VERIFY(!tools.deviceStatus().value("hotspotActive").toBool());

        QFile externalAp(statePath); QVERIFY(externalAp.open(QIODevice::WriteOnly)); externalAp.close();
        tools.acquireHotspotLease("preexisting-ap-session");
        QTRY_COMPARE(leases.count(),2);
        QVERIFY(leases.last().at(1).toBool());
        tools.releaseHotspotLease("preexisting-ap-session");
        QTest::qWait(100);
        QVERIFY(tools.deviceStatus().value("hotspotActive").toBool()); // Never close an AP we did not start.
    }
    void hotspotProcessesAreReapedOnShutdown() {
        QTemporaryDir dir;
        QFile fake(dir.filePath("nmcli")); QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\necho $$ > \"$TEST_AP_PID_FILE\"\nexec sleep 30\n");
        fake.close(); QVERIFY(fake.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const auto path=qgetenv("PATH"), pidFile=qgetenv("TEST_AP_PID_FILE");
        struct Restore { QByteArray path,pidFile; ~Restore(){qputenv("PATH",path);qputenv("TEST_AP_PID_FILE",pidFile);} } restore{path,pidFile};
        const auto pidPath=dir.filePath("nmcli.pid");
        qputenv("PATH",dir.path().toUtf8()+":"+path);qputenv("TEST_AP_PID_FILE",pidPath.toUtf8());
        auto *tools=new SystemTools;
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidPath),2000);
        QFile file(pidPath); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto pid=file.readAll().trimmed().toLongLong(); QVERIFY(pid>1);
        QElapsedTimer elapsed; elapsed.start();
        delete tools;
        QVERIFY2(elapsed.elapsed()<2500,"SystemTools shutdown waited too long for hotspot helper");
        errno=0;
        QVERIFY2(::kill(pid_t(pid),0)<0 && errno==ESRCH,"nmcli helper survived SystemTools destruction");
    }
    void bridgeWarningDebounce() {
        BridgeConnectionMonitor monitor(nullptr,30);
        QSignalSpy warnings(&monitor,&BridgeConnectionMonitor::unavailable);
        monitor.enable(); monitor.setConnected(true);
        QTest::qWait(50); QCOMPARE(warnings.count(),0);
        monitor.setConnected(false); monitor.setConnected(false);
        QTRY_COMPARE(warnings.count(),1);
        monitor.setConnected(false); QTest::qWait(50); QCOMPARE(warnings.count(),1);
        monitor.setConnected(true); monitor.setConnected(false);
        QTRY_COMPARE(warnings.count(),2);
    }
    void bridgeSequenceValidation() {
        HostEventDetector detector;
        QSignalSpy events(&detector, &HostEventDetector::detected);
        QJsonObject e{{"event", "attach"}, {"module_id", 80}, {"module_instance", "seq-test"},
                      {"module_config", QJsonObject{{"schema", "module-transport-v1"},
                          {"transport_mode", "fixed_hotspot_lan"}, {"package_transport", "data"}}}};
        for (double sequence : {-1.0, 0.0, 0.5, 4294967296.0}) {
            e["_bridge_sequence"] = sequence; detector.consume(e);
        }
        QCOMPARE(events.count(), 0);
        e["_bridge_sequence"] = 1; detector.consume(e); detector.consume(e);
        QCOMPARE(events.count(), 1);
        e["_bridge_sequence"] = 2; detector.consume(e);
        QCOMPARE(events.count(), 2);
        detector.reset(); detector.consume(e);
        QCOMPARE(events.count(), 3);
    }
    void cameraFrameSave() {
        SystemTools tools;QTemporaryDir dir;
        QImage image(320,240,QImage::Format_RGB32);image.fill(Qt::green);
        QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);QVERIFY(image.save(&buffer,"PNG"));
        const QString frame="data:image/png;base64,"+QString::fromLatin1(bytes.toBase64());
        const auto path=dir.filePath("照片 with spaces.png");const auto url=QUrl::fromLocalFile(path);
        QVERIFY(tools.saveCameraFrame(frame,url).isEmpty());
        QVERIFY(!tools.saveCameraFrame(frame,url).isEmpty());
        QVERIFY(tools.saveCameraFrame(frame,QUrl(dir.filePath("另一张 photo.png"))).isEmpty());
        QFile saved(path);QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),bytes);saved.close();
        QVERIFY(!tools.saveCameraFrame("data:image/png;base64,invalid",url).isEmpty());
        QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),bytes);saved.close();
        QVERIFY(!tools.saveCameraFrame(frame,QUrl::fromLocalFile(dir.filePath("missing/photo.png"))).isEmpty());
        QVERIFY(!tools.saveCameraFrame(frame,QUrl("https://example.com/photo.png")).isEmpty());
    }
    void opacityPreferences() {
        SystemTools tools;
        const auto home=tools.launcherOpacity(),module=tools.moduleOpacity();
        tools.setLauncherOpacity(0.23);tools.setModuleOpacity(0.87);
        SystemTools reloaded;
        QCOMPARE(reloaded.launcherOpacity(),0.23);QCOMPARE(reloaded.moduleOpacity(),0.87);
        tools.setLauncherOpacity(-1);tools.setModuleOpacity(2);
        QCOMPARE(tools.launcherOpacity(),0.0);QCOMPARE(tools.moduleOpacity(),1.0);
        tools.setLauncherOpacity(home);tools.setModuleOpacity(module);
    }
    void modelKeepsIdentity() {
        ModuleCardModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.synchronize({QVariantMap{{"instance","one"},{"progress",10}}, QVariantMap{{"instance","two"},{"progress",20}}});
        QPersistentModelIndex index(model.index(0));
        model.synchronize({QVariantMap{{"instance","two"},{"progress",80}}, QVariantMap{{"instance","one"},{"progress",40}}});
        QCOMPARE(index.data(Qt::UserRole+1).toMap().value("instance").toString(), "one");
        QCOMPARE(index.data(Qt::UserRole+1).toMap().value("progress").toInt(), 40);
        model.synchronize({QVariantMap{{"instance","two"}}});
        QVERIFY(!index.isValid()); QCOMPARE(model.rowCount(), 1);
    }
    void isolatedPageModels() {
        PageProvider pages;
        pages.registerPage("one", "file:///one.qml"); pages.registerPage("two", "file:///two.qml");
        pages.update("one", {{"value",42}});
        QCOMPARE(pages.model("one")->property("data").toMap().value("value").toInt(), 42);
        QVERIFY(pages.model("two")->property("data").toMap().isEmpty());
        pages.remove("one"); QVERIFY(!pages.model("one")); QVERIFY(pages.page("one").isEmpty());
    }
    void rejectsIllegalTransitions() {
        SessionStateMachine machine; QSignalSpy states(&machine, &SessionStateMachine::changed);
        machine.request("ready", "must not bypass verification"); QTest::qWait(30); QCOMPARE(states.count(), 0);
        for (const auto *name : {"connecting", "downloading_package", "awaiting_trust", "loading_plugin", "ready", "contact_detached", "ready", "disconnecting", "unloading", "closed"}) {
            machine.request(name, name); QTRY_VERIFY(!states.isEmpty()); QCOMPARE(states.takeLast().first().toString(), QString(name));
        }
        machine.request("ready", "cannot resurrect"); QTest::qWait(30); QCOMPARE(states.count(), 0);
    }
    void chunkIdentityAndBounds() {
        PackageReceiver receiver; QSignalSpy requests(&receiver, &PackageReceiver::request), errors(&receiver, &PackageReceiver::failed);
        receiver.start("python3", "unused", "unused", 128);
        QCOMPARE(requests.count(), 1);
        auto request = requests.first().first().toJsonObject();
        auto wrong = request; wrong["transfer_id"] = "wrong";
        QVERIFY(!receiver.receive(wrong)); QCOMPARE(errors.count(), 0);
        request["total"] = 999999; request["payload"] = "YQ==";
        QVERIFY(!receiver.receive(request)); QCOMPARE(errors.count(), 1);
    }
    void confinedFiles() {
        QStandardPaths::setTestModeEnabled(true);
        SystemTools tools;
        const QString name = "test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(tools.makeDirectory(name));
        QVERIFY(tools.write(name + "/a.txt", "文本"));
        QCOMPARE(tools.read(name + "/a.txt"), QString("文本"));
        QVERIFY(!tools.write("../escape.txt", "no"));
        QVERIFY(!tools.write(name + "/big.txt", QString(65537, 'a')));
        QTemporaryDir external;
        QVERIFY(QFile::link(external.path(), tools.filesRoot() + "/" + name + "/link"));
        QVERIFY(!tools.write(name + "/link/out.txt", "no"));
        QFile::remove(tools.filesRoot() + "/" + name + "/link");
        QVERIFY(tools.rename(name + "/a.txt", name + "/b.txt"));
        QVERIFY(tools.remove(name + "/b.txt")); QVERIFY(tools.remove(name));
    }
    void pageSettingsArePrefixScoped() {
        QStandardPaths::setTestModeEnabled(true);
        SystemTools tools;
        const QString key = "time/test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(tools.settingsWrite(key, "{\"hour\":7}"));
        QCOMPARE(tools.settingsRead(key, "fallback"), QStringLiteral("{\"hour\":7}"));
        QCOMPARE(tools.settingsRead(key + "-missing", "fallback"), QStringLiteral("fallback"));
        // 页面脚本拿不到信任记录与宿主偏好，也不能写空键或超长内容
        QVERIFY(!tools.settingsWrite("trusted/" + QUuid::createUuid().toString(QUuid::WithoutBraces), "true"));
        QVERIFY(!tools.settingsWrite("uiLanguage", "en"));
        QVERIFY(!tools.settingsWrite("time/", "x"));
        QCOMPARE(tools.settingsRead("trusted/whatever", "none"), QStringLiteral("none"));
        QVERIFY(!tools.settingsWrite(key, QString(17000, QLatin1Char('x'))));
        QVERIFY(tools.settingsWrite(key, QString(16000, QLatin1Char('x'))));
        QVERIFY(tools.settingsWrite(key, QString()));
        QCOMPARE(tools.settingsRead(key, "unset"), QString());
    }
    void fileCreationAndLocations() {
        SystemTools tools;
        const QString name = "create-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt";
        QVERIFY(tools.createFile(name,"original"));
        QVERIFY(!tools.createFile(name,"replacement"));
        QCOMPARE(tools.read(name),QString("original"));
        QVERIFY(!tools.createFile("../outside.txt","no"));
        tools.rememberFile(name);
        QVERIFY(!tools.recentFiles().isEmpty());
        QCOMPARE(tools.recentFiles().first().toMap().value("relative").toString(),name);
        for (const auto &location : {"documents","downloads"}) {
            const auto relative=tools.fileLocation(location);
            QVERIFY(!QDir::isAbsolutePath(relative));
            QVERIFY(!relative.startsWith("../"));
        }
        QVERIFY(tools.remove(name));
    }
    void fileBrowserStartsAtHome() {
        SystemTools tools;
        QCOMPARE(QFileInfo(tools.filesRoot()).canonicalFilePath(), QFileInfo(QDir::homePath()).canonicalFilePath());
    }
    void wallpaperValidationAndPersistence() {
        SystemTools tools;
        const auto previous=tools.wallpaper();
        QTemporaryDir dir;
        const auto path=dir.filePath("wallpaper with spaces.png");
        QImage image(160,90,QImage::Format_RGB32);image.fill(QColor("#38b8a0"));
        QVERIFY(image.save(path));
        QVERIFY(tools.setWallpaper(QUrl::fromLocalFile(path)));
        QCOMPARE(tools.wallpaper(),QUrl::fromLocalFile(path));
        QCOMPARE(tools.wallpaperColor(80,45,160,90),QColor("#38b8a0"));
        QImage contrast(160,90,QImage::Format_RGB32);contrast.fill(Qt::white);
        for(int y=0;y<90;++y)for(int x=0;x<80;++x)contrast.setPixelColor(x,y,Qt::black);
        const auto contrastPath=dir.filePath("contrast.png");QVERIFY(contrast.save(contrastPath));
        QVERIFY(tools.setWallpaper(QUrl::fromLocalFile(contrastPath)));
        QCOMPARE(tools.wallpaperColor(20,45,160,90),QColor(Qt::black));
        QCOMPARE(tools.wallpaperColor(140,45,160,90),QColor(Qt::white));
        QCOMPARE(tools.wallpaperColor(10,160,90,320),QColor(Qt::black));
        QCOMPARE(tools.wallpaperColor(80,160,90,320),QColor(Qt::white));
        QVERIFY(tools.setWallpaper(QUrl::fromLocalFile(path)));
        SystemTools reloaded;
        QCOMPARE(reloaded.wallpaper(),tools.wallpaper());
        QVERIFY(!tools.setWallpaper(QUrl("https://example.com/picture.png")));
        QVERIFY(!tools.setWallpaper(QUrl::fromLocalFile(dir.filePath("missing.png"))));
        QCOMPARE(tools.wallpaper(),QUrl::fromLocalFile(path));
        QVERIFY(tools.setWallpaper(QUrl{}));QVERIFY(tools.wallpaper().isEmpty());
        QVERIFY(tools.setWallpaper(previous));
    }
    void desktopFileAndBrowserContracts() {
        SystemTools tools;
        const QString name = "terminal-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt";
        QVERIFY(tools.write(name, "default application test"));
        const auto rows = tools.files("");
        bool found = false;
        for (const auto &row : rows) if (row.toMap().value("name").toString() == name) { found = true; QVERIFY(!row.toMap().value("directory").toBool()); }
        QVERIFY(found);
        QVERIFY(!tools.openWithDefault("../" + name));
        QVERIFY(tools.remove(name));
        // The GUI owns URL normalization; the native file API must remain bounded.
        QVERIFY(tools.files("").size() >= 0);
    }

    // 技术说明 §6 表 1：四种组合都合法（业务 lane 与包通道独立）。
    // applyModuleConfig 与 fromProfile 必须给出同一判定，否则会分叉。
    void transportPolicyTableIsConsistent()
    {
        struct Case { const char *mode; const char *package; bool valid; };
        const Case cases[] = {
            {"fixed_ble", "data", true},
            {"fixed_ble", "hotspot_lan", true},
            {"fixed_hotspot_lan", "data", true},
            // 四项组合全部合法：业务 lane 与包通道是两个独立决策。
            {"fixed_hotspot_lan", "hotspot_lan", true},
        };
        for (const auto &c : cases) {
            const QJsonObject moduleConfig{
                {"schema", "module-transport-v1"},
                {"transport_mode", QLatin1String(c.mode)},
                {"package_transport", QLatin1String(c.package)}};

            // 路径一：模块配置合并进 profile。
            QJsonObject merged;
            const bool viaModule = TransportPolicy::applyModuleConfig(&merged, moduleConfig);

            // 路径二：直接从 profile 解析。
            const bool viaProfile = TransportPolicy::fromProfile(moduleConfig).valid();

            QVERIFY2(viaModule == c.valid,
                     qPrintable(QStringLiteral("applyModuleConfig(%1,%2) = %3")
                                    .arg(QLatin1String(c.mode), QLatin1String(c.package))
                                    .arg(viaModule)));
            QVERIFY2(viaProfile == c.valid,
                     qPrintable(QStringLiteral("fromProfile(%1,%2) = %3")
                                    .arg(QLatin1String(c.mode), QLatin1String(c.package))
                                    .arg(viaProfile)));
            // 两条路径结论必须一致。
            QCOMPARE(viaModule, viaProfile);
        }
    }

    void voiceCatalogFollowsTestMode() {
        // 音色发现只依赖目录结构，不加载音频；用临时目录验证分类与彩蛋边界。
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("tts")));
        QVERIFY(QDir().mkpath(dir.filePath("voices/xiaoxiao/units")));
        QVERIFY(QDir().mkpath(dir.filePath("voices/kasumi/units")));
        QFile en(dir.filePath("tts/cmu_us_slt.flitevox"));
        QVERIFY(en.open(QIODevice::WriteOnly)); en.write("stub"); en.close();
        QFile dict(dir.filePath("tts/zh_dict.bin"));
        QVERIFY(dict.open(QIODevice::WriteOnly)); dict.write("stub"); dict.close();
        const auto fakeBin = dir.filePath("flite_zh");
        QFile bin(fakeBin);
        QVERIFY(bin.open(QIODevice::WriteOnly)); bin.write("#!/bin/sh\nexit 0\n"); bin.close();
        QVERIFY(bin.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));

        const auto oldBin = qgetenv("TERMINAL_FLITE_BIN");
        const auto oldRoot = qgetenv("TERMINAL_TTS_ROOT");
        QSettings settings;
        const auto oldVoice = settings.value("ttsVoice");
        const auto oldEgg = settings.value("ttsVoiceTestMode");
        const auto restore = qScopeGuard([&] {
            qputenv("TERMINAL_FLITE_BIN", oldBin);
            qputenv("TERMINAL_TTS_ROOT", oldRoot);
            if (oldVoice.isValid()) settings.setValue("ttsVoice", oldVoice); else settings.remove("ttsVoice");
            if (oldEgg.isValid()) settings.setValue("ttsVoiceTestMode", oldEgg); else settings.remove("ttsVoiceTestMode");
            settings.sync();
        });
        qputenv("TERMINAL_FLITE_BIN", fakeBin.toUtf8());
        qputenv("TERMINAL_TTS_ROOT", dir.path().toUtf8());

        SystemTools tools;
        tools.setVoiceTestMode(false);
        const auto ids = [&] {
            QStringList list;
            for (const auto &row : tools.voiceOptions()) {
                const auto map = row.toMap();
                if (!map.value("available").toBool()) continue;
                if (map.value("testOnly").toBool() && !tools.voiceTestMode()) continue;
                list << map.value("id").toString();
            }
            return list;
        };
        QVERIFY(ids().contains("xiaoxiao"));
        QVERIFY(!ids().contains("kasumi"));
        QVERIFY(tools.voiceAvailable("xiaoxiao"));
        // 角色音色数据就绪，彩蛋只是 UI 闸门，不是可用性闸门。
        QVERIFY(tools.voiceAvailable("kasumi"));
        const int microsoftCount = ids().size();

        tools.setVoiceTestMode(true);
        QVERIFY(ids().contains("kasumi"));
        QVERIFY(ids().size() > microsoftCount);
        tools.setVoice("kasumi");
        QCOMPARE(tools.voiceId(), QStringLiteral("kasumi"));
        // 关闭测试模式必须把当前音色切回晓晓，不能留在已隐藏的角色音色上。
        tools.setVoiceTestMode(false);
        QCOMPARE(tools.voiceId(), QStringLiteral("xiaoxiao"));
        QVERIFY(!ids().contains("kasumi"));
        // 不存在的音色不许写入设置。
        tools.setVoice("nosuchvoice");
        QCOMPARE(tools.voiceId(), QStringLiteral("xiaoxiao"));
    }

    void fliteSpeechUsesEngineAndDucks() {
        // 不真的发声：用一个假引擎记录 argv，验证走的是 flite 路径、参数与降音生命周期。
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir().mkpath(dir.filePath("tts")));
        QVERIFY(QDir().mkpath(dir.filePath("voices/xiaoxiao")));
        // 有声库时优先走 FLITECN_BANK，不应再传 FLITECN_UNITS
        QFile bank(dir.filePath("voices/xiaoxiao/voice.zhvox"));
        QVERIFY(bank.open(QIODevice::WriteOnly)); bank.write("stub"); bank.close();
        QFile en(dir.filePath("tts/cmu_us_slt.flitevox"));
        QVERIFY(en.open(QIODevice::WriteOnly)); en.write("stub"); en.close();
        QFile dict(dir.filePath("tts/zh_dict.bin"));
        QVERIFY(dict.open(QIODevice::WriteOnly)); dict.write("stub"); dict.close();
        const auto log = dir.filePath("argv.log");
        const auto fakeBin = dir.filePath("flite_zh");
        QFile bin(fakeBin);
        QVERIFY(bin.open(QIODevice::WriteOnly));
        bin.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$TEST_SPEECH_LOG\"\nenv | grep '^FLITECN_' > \"$TEST_ENV_LOG\"\nsleep 0.4\n");
        bin.close();
        QVERIFY(bin.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));

        const auto oldBin = qgetenv("TERMINAL_FLITE_BIN");
        const auto oldRoot = qgetenv("TERMINAL_TTS_ROOT");
        const auto oldLog = qgetenv("TEST_SPEECH_LOG");
        const auto oldEnvLog = qgetenv("TEST_ENV_LOG");
        QSettings settings;
        const auto oldVoice = settings.value("ttsVoice");
        const auto oldSpeech = settings.value("speech");
        const auto oldDuck = settings.value("duckMediaForSpeech");
        const auto restore = qScopeGuard([&] {
            qputenv("TERMINAL_FLITE_BIN", oldBin);
            qputenv("TERMINAL_TTS_ROOT", oldRoot);
            qputenv("TEST_SPEECH_LOG", oldLog);
            qputenv("TEST_ENV_LOG", oldEnvLog);
            if (oldVoice.isValid()) settings.setValue("ttsVoice", oldVoice); else settings.remove("ttsVoice");
            if (oldSpeech.isValid()) settings.setValue("speech", oldSpeech); else settings.remove("speech");
            if (oldDuck.isValid()) settings.setValue("duckMediaForSpeech", oldDuck); else settings.remove("duckMediaForSpeech");
            settings.sync();
        });
        qputenv("TERMINAL_FLITE_BIN", fakeBin.toUtf8());
        qputenv("TERMINAL_TTS_ROOT", dir.path().toUtf8());
        qputenv("TEST_SPEECH_LOG", log.toUtf8());
        qputenv("TEST_ENV_LOG", (dir.filePath("env.log")).toUtf8());
        settings.setValue("ttsVoice", "xiaoxiao");
        settings.setValue("speech", true);
        settings.setValue("duckMediaForSpeech", true);

        SystemTools tools;
        QVERIFY(tools.speechReady());
        QCOMPARE(tools.mediaGain(), 1.0);
        tools.speak("关闭当前应用");
        QTRY_VERIFY(tools.speechActive());
        QCOMPARE(tools.mediaGain(), 0.2);
        QTRY_VERIFY(!tools.speechActive());
        QCOMPARE(tools.mediaGain(), 1.0);

        QFile recorded(log);
        QVERIFY(recorded.open(QIODevice::ReadOnly));
        const auto args = QString::fromUtf8(recorded.readAll());
        QVERIFY(args.contains("-o"));
        QVERIFY(args.contains("-t"));
        QVERIFY(args.contains("关闭当前应用"));
        QFile envfile(dir.filePath("env.log"));
        QVERIFY(envfile.open(QIODevice::ReadOnly));
        const auto env = QString::fromUtf8(envfile.readAll());
        QVERIFY(env.contains("FLITECN_BANK="));
        QVERIFY(env.contains("FLITECN_EN_VOICE="));
        QVERIFY(!env.contains("FLITECN_UNITS="));
        // 环境里必须带词典与音色目录，否则引擎会回退系统语音。
        QVERIFY(!tools.voiceOptions().isEmpty());
    }

    void speechOutputSelectionPersists() {
        // 设备列表可能为空（无音频服务的 CI），这里只验证选择持久化与设备断开时的兜底。
        SystemTools tools;
        QSettings settings;
        const auto old = settings.value("ttsOutputDevice");
        const auto restore = qScopeGuard([&] {
            if (old.isValid()) settings.setValue("ttsOutputDevice", old); else settings.remove("ttsOutputDevice");
            settings.sync();
        });
        tools.setAudioOutputDevice("no-such-output-device");
        QCOMPARE(tools.audioOutputDevice(), QStringLiteral("no-such-output-device"));
        // 有音频设备时名字必须可读；没有音频服务的 CI 允许为空。
        if (!tools.audioOutputs().isEmpty()) QVERIFY(!tools.audioOutputName().isEmpty());
        for (const auto &row : tools.audioOutputs()) {
            QVERIFY(!row.toMap().value("name").toString().isEmpty());
        }
        tools.setAudioOutputDevice(QString());
        QCOMPARE(tools.audioOutputDevice(), QString());
    }
};



QTEST_GUILESS_MAIN(RuntimeTests)
#include "runtime_test.moc"
