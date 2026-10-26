// Time page regression: real TimeTools.qml against a stub host object, plus a
// loopback HTTP time server so the world-clock sync path is exercised offline.
// Not a device acceptance test: no Pi, no real upstream service, no touch input.
#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QJSValue>
#include "ui/TerminalUi.h"

namespace {

class TimeToolsHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString uiLanguage MEMBER language NOTIFY languageChanged)
public:
    QString language = QStringLiteral("zh");
    QHash<QString, QString> settings;
    Q_INVOKABLE QString monochromeIcon(const QString &name, bool) {
        return QStringLiteral("qrc:/icons/material/light/") + name + QStringLiteral(".svg");
    }
    // Mirrors SystemTools: only "time/" keys are writable from a page.
    Q_INVOKABLE QString settingsRead(const QString &key, const QString &fallback = QString()) const {
        return settings.value(key, fallback);
    }
    Q_INVOKABLE QVariantList files(const QString &) const {
        QVariantList list;
        QVariantMap folder;
        folder.insert("name", "Music"); folder.insert("directory", true); folder.insert("relative", "Music");
        QVariantMap song;
        song.insert("name", "alarm.mp3"); song.insert("directory", false); song.insert("relative", "Music/alarm.mp3");
        QVariantMap text;
        text.insert("name", "notes.txt"); text.insert("directory", false); text.insert("relative", "Music/notes.txt");
        list << folder << song << text;
        return list;
    }
    Q_INVOKABLE bool settingsWrite(const QString &key, const QString &value) {
        if (!key.startsWith(QStringLiteral("time/")) || key.size() > 96 ||
            value.toUtf8().size() > 16384) return false;
        settings.insert(key, value);
        return true;
    }
signals:
    void languageChanged();
};

// Minimal worldtimeapi.org-shaped responder. Offsets intentionally differ from
// the built-in standard table so a passing test proves they were applied.
class StubTimeServer : public QTcpServer {
public:
    using QTcpServer::QTcpServer;

    QHash<QString, int> offsets{{QStringLiteral("Asia/Shanghai"), 480},
                                {QStringLiteral("Asia/Tokyo"), 540},
                                {QStringLiteral("Asia/Singapore"), 480},
                                {QStringLiteral("Europe/London"), 60},
                                {QStringLiteral("America/New_York"), -240},
                                {QStringLiteral("America/Los_Angeles"), -420}};
    int requests = 0;
    bool failAll = false;

protected:
    void incomingConnection(qintptr descriptor) override {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        auto *buffer = new QByteArray;
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::destroyed, socket, [buffer] { delete buffer; });
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            buffer->append(socket->readAll());
            if (!buffer->contains("\r\n\r\n")) return;
            ++requests;
            const QByteArray path = buffer->split(' ').value(1);
            const QString zone = QUrl::fromPercentEncoding(path.mid(QByteArray("/api/timezone/").size()));
            QByteArray body;
            if (!failAll && offsets.contains(zone)) {
                body = QStringLiteral("{\"utc_offset\":\"%1\",\"unixtime\":%2,\"dst\":false,\"timezone\":\"%3\"}")
                           .arg(offsetText(offsets.value(zone)))
                           .arg(QDateTime::currentSecsSinceEpoch())
                           .arg(zone)
                           .toUtf8();
            }
            const QByteArray status = body.isEmpty() ? QByteArray("HTTP/1.1 503 Service Unavailable")
                                                     : QByteArray("HTTP/1.1 200 OK");
            socket->write(status + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }

    static QString offsetText(int minutes) {
        const QString sign = minutes < 0 ? QStringLiteral("-") : QStringLiteral("+");
        const int absolute = qAbs(minutes);
        return sign + QStringLiteral("%1:%2").arg(absolute / 60, 2, 10, QLatin1Char('0'))
                                              .arg(absolute % 60, 2, 10, QLatin1Char('0'));
    }
};

} // namespace

class TimeToolsTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Material"));
        initializeTerminalUi();
    }

    void pageStructure() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}, {"active", true}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);

        // Every tab is a real, switchable page and the clock already formatted.
        const QString time = page->property("clockTime").toString();
        QVERIFY2(QRegularExpression(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d$")).match(time).hasMatch(),
                 qPrintable(time));
        QVERIFY(page->property("clockWeekday").toString().size() > 0);
        QCOMPARE(page->property("tab").toInt(), 0);
        QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, 3));
        QCOMPARE(page->property("tab").toInt(), 3);
        QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, 9)); // clamped
        QCOMPARE(page->property("tab").toInt(), 3);

        // Default countdown state must match the 5 minute default.
        QCOMPARE(page->property("timerLabel").toString(), QStringLiteral("05:00"));
        QCOMPARE(page->property("cities").toList().size(), 6);
        QCOMPARE(page->property("zoneOffsets").toList().value(0).toInt(), 480);
    }

    void countdownRunsInTheEngine() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent alarmComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QVERIFY2(alarmComponent.isReady(), qPrintable(alarmComponent.errorString()));
        QScopedPointer<QObject> alarmObject(alarmComponent.create());
        QVERIFY(alarmObject);
        QSignalSpy notified(alarmObject.data(), SIGNAL(notifyRequested(QString, QString, bool)));
        QSignalSpy announced(alarmObject.data(), SIGNAL(announceRequested(QString)));

        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0},
             {"alarmEngine", QVariant::fromValue(alarmObject.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);

        // 计时器归引擎所有：页面只是视图，页面销毁后仍会到点提示。
        QMetaObject::invokeMethod(page, "applyPreset", Q_ARG(QVariant, 1));
        QCOMPARE(page->property("timerTotalMs").toDouble(), 60000.0);
        QMetaObject::invokeMethod(page, "startCountdown");
        QVERIFY(alarmObject->property("timerRunning").toBool());
        QVERIFY(page->property("timerRunning").toBool());
        alarmObject->setProperty("timerEndsAtMs", QDateTime::currentMSecsSinceEpoch() - 10);
        QMetaObject::invokeMethod(alarmObject.data(), "tick");
        QVERIFY(alarmObject->property("timerFinished").toBool());
        QVERIFY(page->property("timerFinished").toBool());
        QCOMPARE(notified.count(), 1);
        QCOMPARE(announced.count(), 1);
        QCOMPARE(page->property("timerLabel").toString(), QStringLiteral("计时结束"));

        // 暂停 / 复位仍由引擎驱动
        QMetaObject::invokeMethod(page, "resetCountdown");
        QVERIFY(!alarmObject->property("timerRunning").toBool());
        QVERIFY(!alarmObject->property("timerFinished").toBool());
        QCOMPARE(page->property("timerLabel").toString(), QStringLiteral("01:00"));
    }

    void clockStyleAndRingtoneDefaults() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent alarmComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QScopedPointer<QObject> alarmObject(alarmComponent.create());
        QVERIFY(alarmObject);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0},
             {"alarmEngine", QVariant::fromValue(alarmObject.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());

        // 新页面的铃声默认取内置 15 秒节选
        QCOMPARE(page->property("alarmRingtone").toString(),
                 QStringLiteral("qrc:/ringtones/default-ringtone.mp3"));
        auto *analog = page->findChild<QQuickItem *>(QStringLiteral("analogClock"));
        QVERIFY(analog);
        QCOMPARE(page->property("clockStyle").toString(), QStringLiteral("digital"));
        page->setProperty("clockStyle", QStringLiteral("analog"));
        QTest::qWait(60);
        QCOMPARE(page->property("clockStyle").toString(), QStringLiteral("analog"));
        QVERIFY(analog->height() > 0);
        QCOMPARE(host.settings.value(QStringLiteral("time/clock-style")), QStringLiteral("analog"));

        // 试听按钮：无论默认还是自选铃声都走引擎的同一个入口，且可安全停止。
        // 闹钟页是 Loader 懒加载的，先切过去。
        QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, 3));
        QTest::qWait(80);
        auto *preview = page->findChild<QQuickItem *>(QStringLiteral("alarmRingtonePreview"));
        QVERIFY(preview);
        QMetaObject::invokeMethod(alarmObject.data(), "togglePreview",
                                  Q_ARG(QVariant, page->property("alarmRingtone")));
        QMetaObject::invokeMethod(alarmObject.data(), "stopPreview");
        QVERIFY(!alarmObject->property("previewing").toBool());
    }

    void worldClockSync() {
        StubTimeServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        page->setProperty("primaryTimeHost", base);
        page->setProperty("fallbackTimeHost", base);

        QMetaObject::invokeMethod(page, "syncWorld");
        QTRY_VERIFY_WITH_TIMEOUT(page->property("worldSyncedOnce").toBool(), 8000);
        QCOMPARE(server.requests, 6); // one self-contained request per city
        // UI 只显示“线路一/线路二”，不暴露真实地址
        QCOMPARE(page->property("worldProvider").toString(), QStringLiteral("线路一"));
        QVERIFY(!page->property("worldStatus").toString().contains(QStringLiteral("http")));
        const auto offsets = page->property("zoneOffsets").toList();
        QCOMPARE(offsets.value(0).toInt(), 480);
        QCOMPARE(offsets.value(3).toInt(), 60);    // London DST offset from the server
        QCOMPARE(offsets.value(4).toInt(), -240);
        QVERIFY(page->property("worldStatus").toString().contains(QStringLiteral("线路一")));

        // Reset restores the built-in standard offsets and the honest hint.
        QMetaObject::invokeMethod(page, "resetWorld");
        QCOMPARE(page->property("worldSyncedOnce").toBool(), false);
        QCOMPARE(page->property("zoneOffsets").toList().value(3).toInt(), 0);
        QVERIFY(page->property("worldStatus").toString().contains(QStringLiteral("夏令时")));
    }

    void tabsShowTheirOwnPage() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}, {"active", true}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);
        auto *loader = page->findChild<QQuickItem *>(QStringLiteral("tabLoader"));
        QVERIFY(loader);
        const QStringList expected{QStringLiteral("timeClockTab"), QStringLiteral("timeTimerTab"),
                                   QStringLiteral("timeStopwatchTab"), QStringLiteral("timeAlarmTab")};
        for (int i = 0; i < expected.size(); ++i) {
            QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, i));
            QTest::qWait(80);
            QCOMPARE(page->property("tab").toInt(), i);
            auto *current = loader->property("item").value<QQuickItem *>();
            QVERIFY2(current, qPrintable(QString::number(i)));
            // Regression: an earlier revision left unused scroll height below the
            // card (the tab page must define the scroll extent exactly).
            QCOMPARE(current->objectName(), expected.at(i));
            QVERIFY2(current->height() > 100, qPrintable(current->objectName() + " " + QString::number(current->height())));
            auto *flick = page->findChild<QQuickItem *>(QStringLiteral("timePage"));
            QVERIFY(flick);
            // 滚动静止区只允许出现标签行与间距，不允许留下整屏空白。
            QVERIFY2(flick->property("contentHeight").toDouble() - current->height() < 200,
                     qPrintable(QString::number(flick->property("contentHeight").toDouble()) + " vs " +
                                QString::number(current->height())));
        }
    }

    void lapBoxGrowsWithLaps() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}, {"active", true}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        // 布局随内容变化需要真实窗口里的 polish 循环，offscreen 也要 show()。
        QQuickWindow window;
        window.resize(1024, 700);
        page->setParentItem(window.contentItem());
        window.show();
        QTest::qWait(120);
        QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, 2));
        QTest::qWait(120);
        auto *box = page->findChild<QQuickItem *>(QStringLiteral("stopwatchLapBox"));
        QVERIFY(box);
        // 空状态不占位；有计次后容器随条目增高（offline 渲染下 visible 读数不可靠，
        // 因此用高度判断，而不是 isVisible()）。
        QVERIFY2(box->height() < 1 || !box->property("visible").toBool(), "empty lap box must not take space");
        QMetaObject::invokeMethod(page, "toggleStopwatch");
        QTest::qWait(60);
        for (int i = 0; i < 3; ++i) {
            QMetaObject::invokeMethod(page, "addLap");
            QTest::qWait(40);
        }
        QTRY_VERIFY_WITH_TIMEOUT(box->height() > 120, 2000);
        const qreal threeRows = box->height();
        QMetaObject::invokeMethod(page, "addLap");
        QTRY_VERIFY_WITH_TIMEOUT(box->height() > threeRows + 20, 2000);
        QMetaObject::invokeMethod(page, "resetStopwatch");
        QTRY_VERIFY_WITH_TIMEOUT(page->property("stopwatchLaps").toList().isEmpty(), 2000);
        QMetaObject::invokeMethod(page, "toggleStopwatch");   // stop the timer for a clean shutdown
    }

    void alarmEngineOwnsAlarms() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent alarmComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QVERIFY2(alarmComponent.isReady(), qPrintable(alarmComponent.errorString()));
        QScopedPointer<QObject> alarmObject(alarmComponent.create());
        QVERIFY2(alarmObject, qPrintable(alarmComponent.errorString()));
        QSignalSpy notified(alarmObject.data(), SIGNAL(notifyRequested(QString, QString, bool)));
        QVERIFY(notified.isValid());

        QVariantMap alarm{{QStringLiteral("hour"), 6}, {QStringLiteral("minute"), 45},
                          {QStringLiteral("repeatDaily"), true},
                          {QStringLiteral("name"), QStringLiteral("起床")},
                          {QStringLiteral("ringtone"), QString()},
                          {QStringLiteral("ringSeconds"), 20},
                          {QStringLiteral("snoozeMinutes"), 5}};
        QVariant added;
        QVERIFY(QMetaObject::invokeMethod(alarmObject.data(), "addAlarm",
                                          Q_RETURN_ARG(QVariant, added), Q_ARG(QVariant, alarm)));
        QVERIFY(added.toBool());
        auto *model = alarmObject->property("alarms").value<QObject *>();
        QVERIFY(model);
        QCOMPARE(model->property("count").toInt(), 1);
        QCOMPARE(alarmObject->property("enabledCount").toInt(), 1);
        QVERIFY(alarmObject->property("hasEnabledAlarms").toBool());
        QVERIFY(host.settings.value(QStringLiteral("time/alarms")).contains(QStringLiteral("起床")));

        // 直接触发响铃：验证提示、再响与停止的语义（不依赖系统时间）。
        QMetaObject::invokeMethod(alarmObject.data(), "startRing", Q_ARG(QVariant, QVariant(alarm)));
        QVERIFY(alarmObject->property("ringing").toBool());
        QCOMPARE(alarmObject->property("ringingName").toString(), QStringLiteral("起床"));
        QCOMPARE(notified.count(), 1);
        QMetaObject::invokeMethod(alarmObject.data(), "snoozeRing");
        QVERIFY(!alarmObject->property("ringing").toBool());
        QVERIFY(alarmObject->property("snoozedAlarm").isValid());
        QMetaObject::invokeMethod(alarmObject.data(), "stopRing");

        // 关闭全部闹钟后状态坞图标应消失
        QMetaObject::invokeMethod(alarmObject.data(), "setEnabled", Q_ARG(QVariant, 0), Q_ARG(QVariant, false));
        QCOMPARE(alarmObject->property("enabledCount").toInt(), 0);
        QVERIFY(!alarmObject->property("hasEnabledAlarms").toBool());

        // 新实例从同一份设置里恢复
        QScopedPointer<QObject> again(alarmComponent.create());
        QVERIFY(again);
        QCOMPARE(again->property("alarms").value<QObject *>()->property("count").toInt(), 1);
    }

    void ringtonePickerUsesExistingFiles() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QMetaObject::invokeMethod(page, "openRingtonePicker");
        QVERIFY(page->property("ringtonePickerOpen").toBool());
        auto *layer = page->findChild<QQuickItem *>(QStringLiteral("ringtonePicker"));
        QVERIFY(layer);
        const auto rows = page->property("ringtoneRows").toList();
        QCOMPARE(rows.size(), 2);                       // folder + mp3, txt filtered out
        QCOMPARE(rows.at(1).toMap().value("name").toString(), QStringLiteral("alarm.mp3"));
        QMetaObject::invokeMethod(page, "openRingtoneEntry", Q_ARG(QVariant, rows.at(1)));
        QCOMPARE(page->property("alarmRingtone").toString(), QStringLiteral("Music/alarm.mp3"));
        QVERIFY(!page->property("ringtonePickerOpen").toBool());
    }

    void citiesAreSelectableAndBounded() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        auto cityCount = [&page] { return page->property("cities").toList().size(); };
        QCOMPARE(cityCount(), 6);
        QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, QStringLiteral("Europe/Paris")));
        QCOMPARE(cityCount(), 7);
        QVERIFY(host.settings.value(QStringLiteral("time/cities")).contains(QStringLiteral("Europe/Paris")));
        QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, QStringLiteral("Europe/Paris")));
        QCOMPARE(cityCount(), 6);
        // 上限 8：继续加会被拒绝并给出提示，而不是静默丢弃
        const QStringList extra{QStringLiteral("Asia/Seoul"), QStringLiteral("Asia/Dubai"),
                                QStringLiteral("Europe/Berlin")};
        for (const auto &id : extra)
            QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, id));
        QCOMPARE(cityCount(), 8);
        QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, QStringLiteral("Australia/Sydney")));
        QCOMPARE(cityCount(), 8);
        QVERIFY(!page->property("apiAlertText").toString().isEmpty());
        // 选择也会跨页面重建保留
        QQmlComponent again(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> second(again.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}}));
        QVERIFY(second);
        QCOMPARE(second->property("cities").toList().size(), 8);
    }

    void customServerIsStrict() {
        StubTimeServer custom;
        StubTimeServer fallback;
        custom.failAll = true;
        QVERIFY(custom.listen(QHostAddress::LocalHost, 0));
        QVERIFY(fallback.listen(QHostAddress::LocalHost, 0));
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}}));
        QVERIFY(object);
        auto *page = qobject_cast<QQuickItem *>(object.data());
        page->setProperty("fallbackTimeHost", QStringLiteral("http://127.0.0.1:%1").arg(fallback.serverPort()));
        page->setProperty("customHost", QStringLiteral("http://127.0.0.1:%1").arg(custom.serverPort()));
        page->setProperty("apiMode", QStringLiteral("custom"));
        QCOMPARE(host.settings.value(QStringLiteral("time/api-mode")), QStringLiteral("custom"));
        QMetaObject::invokeMethod(page, "syncWorld");
        QTRY_VERIFY_WITH_TIMEOUT(!page->property("worldSyncing").toBool(), 8000);
        QCOMPARE(page->property("worldSyncedOnce").toBool(), false);
        QVERIFY(custom.requests >= 1);
        QCOMPARE(fallback.requests, 0);   // custom mode never silently uses the public APIs
        const QJSValue providerState = page->property("providerState").value<QJSValue>();
        QCOMPARE(providerState.property(QStringLiteral("custom")).toInt(), -1);
    }

    void customServerNeedsAScheme() {
        StubTimeServer custom;
        QVERIFY(custom.listen(QHostAddress::LocalHost, 0));
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}}));
        QVERIFY(object);
        auto *page = qobject_cast<QQuickItem *>(object.data());
        page->setProperty("customHost", QStringLiteral("127.0.0.1:%1").arg(custom.serverPort()));
        page->setProperty("apiMode", QStringLiteral("custom"));
        QMetaObject::invokeMethod(page, "syncWorld");
        QTRY_VERIFY(!page->property("worldSyncing").toBool());
        QCOMPARE(custom.requests, 0);                       // no request for an invalid URL
        QVERIFY(!page->property("apiAlertText").toString().isEmpty());
        QCOMPARE(page->property("worldSyncedOnce").toBool(), false);
    }

    void worldClockFailureIsExplicit() {
        StubTimeServer primary;
        StubTimeServer fallback;
        primary.failAll = true;
        fallback.failAll = true;
        QVERIFY(primary.listen(QHostAddress::LocalHost, 0));
        QVERIFY(fallback.listen(QHostAddress::LocalHost, 0));
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}}));
        QVERIFY(object);
        auto *page = qobject_cast<QQuickItem *>(object.data());
        page->setProperty("primaryTimeHost", QStringLiteral("http://127.0.0.1:%1").arg(primary.serverPort()));
        page->setProperty("fallbackTimeHost", QStringLiteral("http://127.0.0.1:%1").arg(fallback.serverPort()));
        QMetaObject::invokeMethod(page, "syncWorld");
        QTRY_VERIFY_WITH_TIMEOUT(!page->property("worldSyncing").toBool(), 8000);
        QCOMPARE(page->property("worldSyncedOnce").toBool(), false);
        // Both providers were tried before the error was reported.
        QVERIFY(primary.requests >= 1);
        QVERIFY(fallback.requests >= 1);
        QVERIFY(page->property("worldError").toString().contains(QStringLiteral("失败")));
        QVERIFY(!page->property("apiAlertText").toString().isEmpty());
        // The failure overlay is page-local and must actually take space.
        auto *alert = page->findChild<QQuickItem *>(QStringLiteral("timeApiAlert"));
        QVERIFY(alert);
        QVERIFY(alert->isVisible());
        QVERIFY(alert->width() > 100 && alert->height() > 100);
        auto *alertText = page->findChild<QQuickItem *>(QStringLiteral("timeApiAlertText"));
        QVERIFY(alertText);
        QVERIFY(!alertText->property("text").toString().isEmpty());
        auto *closeButton = page->findChild<QQuickItem *>(QStringLiteral("timeApiAlertClose"));
        QVERIFY(closeButton);
        QCOMPARE(page->property("zoneOffsets").toList().value(0).toInt(), 480); // untouched
    }
};

QTEST_MAIN(TimeToolsTests)
#include "time_tools_test.moc"
