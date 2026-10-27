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
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

// Repeater 生成的 delegate 的 QObject 父链不在页面对象下，findChild 找不到它们；
// 这里按可视树递归查找，页面里任何层级的控件都能定位。
QQuickItem *findItemByObjectName(QQuickItem *root, const QString &name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findItemByObjectName(child, name)) return found;
    }
    return nullptr;
}

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
        QQmlComponent syncComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(syncComponent.isReady(), qPrintable(syncComponent.errorString()));
        QScopedPointer<QObject> syncObject(syncComponent.create());
        QVERIFY(syncObject);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0}, {"active", true},
             {"timeSync", QVariant::fromValue(syncObject.data())}}));
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

    void clockFacesAndRingtoneDefaults() {
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
        // 数字/指针不再二选一：表盘在上、数字钟在下，两者同时存在
        auto *analog = page->findChild<QQuickItem *>(QStringLiteral("analogClock"));
        QVERIFY(analog);
        QVERIFY(analog->height() > 0);
        QVERIFY(page->findChild<QQuickItem *>(QStringLiteral("clockAnnounce")));
        QCOMPARE(page->property("clockTime").toString().size(), 8);

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

    void timeSyncAppliesStubOffsets() {
        StubTimeServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> sync(component.create());
        QVERIFY(sync);
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        sync->setProperty("primaryTimeHost", base);
        sync->setProperty("fallbackTimeHost", base);

        QMetaObject::invokeMethod(sync.data(), "syncTime");
        QTRY_VERIFY_WITH_TIMEOUT(sync->property("syncedOnce").toBool(), 8000);
        QCOMPARE(server.requests, 6); // one self-contained request per city
        // UI 只显示“线路一/线路二”，不暴露真实地址
        QCOMPARE(sync->property("provider").toString(), QStringLiteral("线路一"));
        QVERIFY(!sync->property("status").toString().contains(QStringLiteral("http")));
        QVariant losAngeles, london, newYork;
        QMetaObject::invokeMethod(sync.data(), "offsetOf", Q_RETURN_ARG(QVariant, losAngeles),
                                  Q_ARG(QVariant, QStringLiteral("America/Los_Angeles")));
        QMetaObject::invokeMethod(sync.data(), "offsetOf", Q_RETURN_ARG(QVariant, london),
                                  Q_ARG(QVariant, QStringLiteral("Europe/London")));
        QMetaObject::invokeMethod(sync.data(), "offsetOf", Q_RETURN_ARG(QVariant, newYork),
                                  Q_ARG(QVariant, QStringLiteral("America/New_York")));
        // 桩值刻意与内置表不同：洛杉矶 -420（内置 -480）、伦敦夏令时 +60、纽约夏令时 -240
        QCOMPARE(losAngeles.toInt(), -420);
        QCOMPARE(london.toInt(), 60);
        QCOMPARE(newYork.toInt(), -240);
        QVERIFY(sync->property("status").toString().contains(QStringLiteral("线路一")));

        // Reset restores the built-in standard offsets and the honest hint.
        QMetaObject::invokeMethod(sync.data(), "resetZones");
        QCOMPARE(sync->property("syncedOnce").toBool(), false);
        QMetaObject::invokeMethod(sync.data(), "offsetOf", Q_RETURN_ARG(QVariant, losAngeles),
                                  Q_ARG(QVariant, QStringLiteral("America/Los_Angeles")));
        QMetaObject::invokeMethod(sync.data(), "offsetOf", Q_RETURN_ARG(QVariant, london),
                                  Q_ARG(QVariant, QStringLiteral("Europe/London")));
        QCOMPARE(losAngeles.toInt(), -480);
        QCOMPARE(london.toInt(), 0);
        QVERIFY(sync->property("status").toString().contains(QStringLiteral("夏令时")));
    }

    // 整点报时：只认 :00:00、按小时时段、响铃期间跳过、同一小时只报一次。
    void hourlyChime() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> alarm(component.create());
        QVERIFY(alarm);
        QSignalSpy announced(alarm.data(), SIGNAL(announceRequested(QString)));
        QVERIFY(announced.isValid());

        QMetaObject::invokeMethod(alarm.data(), "setChime",
                                  Q_ARG(QVariant, true), Q_ARG(QVariant, 8), Q_ARG(QVariant, 22));
        QVERIFY(alarm->property("chimeEnabled").toBool());
        QVERIFY(host.settings.value(QStringLiteral("time/chime")).contains(QStringLiteral("\"from\":8")));

        const qint64 base = QDateTime(QDate(2026, 10, 27), QTime(8, 0, 0)).toMSecsSinceEpoch();
        auto at = [](int hour, int minute, int second) {
            return QVariantMap{{"hour", hour}, {"minute", minute}, {"second", second}};
        };
        // 09:00 命中，同一小时重复调用不重复播报
        QVERIFY(QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                          Q_ARG(QVariant, at(9, 0, 0)), Q_ARG(QVariant, double(base + 3600000))));
        QCOMPARE(announced.count(), 1);
        QVERIFY(announced.at(0).at(0).toString().contains(QStringLiteral("整点报时")));
        QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                  Q_ARG(QVariant, at(9, 0, 0)), Q_ARG(QVariant, double(base + 3600000)));
        QCOMPARE(announced.count(), 1);
        // 非整点（09:00:30 / 09:30）不报
        QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                  Q_ARG(QVariant, at(10, 0, 30)), Q_ARG(QVariant, double(base + 2 * 3600000)));
        QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                  Q_ARG(QVariant, at(11, 30, 0)), Q_ARG(QVariant, double(base + 3.5 * 3600000)));
        QCOMPARE(announced.count(), 1);
        // 时段外（03:00）不报
        QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                  Q_ARG(QVariant, at(3, 0, 0)), Q_ARG(QVariant, double(base + 19 * 3600000)));
        QCOMPARE(announced.count(), 1);
        // 响铃期间跳过（闹钟优先）
        alarm->setProperty("ringing", true);
        QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                  Q_ARG(QVariant, at(12, 0, 0)), Q_ARG(QVariant, double(base + 4 * 3600000)));
        QCOMPARE(announced.count(), 1);
        alarm->setProperty("ringing", false);
        // 跨午夜时段：22:00–08:00 含两端，02:00 命中
        QMetaObject::invokeMethod(alarm.data(), "setChime",
                                  Q_ARG(QVariant, true), Q_ARG(QVariant, 22), Q_ARG(QVariant, 8));
        QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                  Q_ARG(QVariant, at(2, 0, 0)), Q_ARG(QVariant, double(base + 18 * 3600000)));
        QCOMPARE(announced.count(), 2);
        // 起=止 视为全天；关掉后彻底不报
        QMetaObject::invokeMethod(alarm.data(), "setChime",
                                  Q_ARG(QVariant, true), Q_ARG(QVariant, 12), Q_ARG(QVariant, 12));
        QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                  Q_ARG(QVariant, at(4, 0, 0)), Q_ARG(QVariant, double(base + 20 * 3600000)));
        QCOMPARE(announced.count(), 3);
        QMetaObject::invokeMethod(alarm.data(), "setChime",
                                  Q_ARG(QVariant, false), Q_ARG(QVariant, 8), Q_ARG(QVariant, 22));
        QMetaObject::invokeMethod(alarm.data(), "checkChime",
                                  Q_ARG(QVariant, at(5, 0, 0)), Q_ARG(QVariant, double(base + 21 * 3600000)));
        QCOMPARE(announced.count(), 3);

        // 新实例从同一份设置恢复
        QScopedPointer<QObject> again(component.create());
        QVERIFY(again);
        QCOMPARE(again->property("chimeEnabled").toBool(), false);
        QCOMPARE(again->property("chimeFromHour").toInt(), 8);
        QCOMPARE(again->property("chimeToHour").toInt(), 22);
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
        QQmlComponent syncComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(syncComponent.isReady(), qPrintable(syncComponent.errorString()));
        QScopedPointer<QObject> sync(syncComponent.create());
        QVERIFY(sync);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0},
             {"timeSync", QVariant::fromValue(sync.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        auto cityCount = [&page] { return page->property("cities").toList().size(); };
        QCOMPARE(cityCount(), 6);
        QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, QStringLiteral("Europe/Paris")));
        QCOMPARE(cityCount(), 7);
        QVERIFY(host.settings.value(QStringLiteral("time/cities")).contains(QStringLiteral("Europe/Paris")));
        QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, QStringLiteral("Europe/Paris")));
        QCOMPARE(cityCount(), 6);
        // 上限 8：继续加会被拒绝，并在页面就地提示而不是静默丢弃
        QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, QStringLiteral("Asia/Seoul")));
        QCOMPARE(cityCount(), 7);
        QVERIFY(page->property("cityNotice").toString().isEmpty());
        QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, QStringLiteral("Asia/Dubai")));
        QCOMPARE(cityCount(), 8);
        QVERIFY(page->property("cityNotice").toString().isEmpty());
        QMetaObject::invokeMethod(page, "toggleCity", Q_ARG(QVariant, QStringLiteral("Europe/Berlin")));
        QCOMPARE(cityCount(), 8);
        QVERIFY(!page->property("cityNotice").toString().isEmpty());
        // 选择由 TimeSync 持久化：新的 TimeSync 实例（页面之外）也能读回同一份选择
        QScopedPointer<QObject> again(syncComponent.create());
        QVERIFY(again);
        QCOMPARE(again->property("zones").toList().size(), 8);
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
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> sync(component.create());
        QVERIFY(sync);
        sync->setProperty("fallbackTimeHost", QStringLiteral("http://127.0.0.1:%1").arg(fallback.serverPort()));
        sync->setProperty("customHost", QStringLiteral("http://127.0.0.1:%1").arg(custom.serverPort()));
        sync->setProperty("apiMode", QStringLiteral("custom"));
        QCOMPARE(host.settings.value(QStringLiteral("time/api-mode")), QStringLiteral("custom"));
        QMetaObject::invokeMethod(sync.data(), "syncTime");
        QTRY_VERIFY_WITH_TIMEOUT(!sync->property("syncing").toBool(), 8000);
        QCOMPARE(sync->property("syncedOnce").toBool(), false);
        QVERIFY(custom.requests >= 1);
        QCOMPARE(fallback.requests, 0);   // custom mode never silently uses the public APIs
        const QJSValue providerState = sync->property("providerState").value<QJSValue>();
        QCOMPARE(providerState.property(QStringLiteral("custom")).toInt(), -1);
    }

    void customServerNeedsAScheme() {
        StubTimeServer custom;
        QVERIFY(custom.listen(QHostAddress::LocalHost, 0));
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QScopedPointer<QObject> sync(component.create());
        QVERIFY(sync);
        sync->setProperty("customHost", QStringLiteral("127.0.0.1:%1").arg(custom.serverPort()));
        sync->setProperty("apiMode", QStringLiteral("custom"));
        QMetaObject::invokeMethod(sync.data(), "syncTime");
        QTRY_VERIFY(!sync->property("syncing").toBool());
        QCOMPARE(custom.requests, 0);                       // no request for an invalid URL
        QVERIFY(!sync->property("alertText").toString().isEmpty());
        QCOMPARE(sync->property("syncedOnce").toBool(), false);
    }

    void syncFailureIsExplicit() {
        StubTimeServer primary;
        StubTimeServer fallback;
        primary.failAll = true;
        fallback.failAll = true;
        QVERIFY(primary.listen(QHostAddress::LocalHost, 0));
        QVERIFY(fallback.listen(QHostAddress::LocalHost, 0));
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QScopedPointer<QObject> sync(component.create());
        QVERIFY(sync);
        sync->setProperty("primaryTimeHost", QStringLiteral("http://127.0.0.1:%1").arg(primary.serverPort()));
        sync->setProperty("fallbackTimeHost", QStringLiteral("http://127.0.0.1:%1").arg(fallback.serverPort()));
        QMetaObject::invokeMethod(sync.data(), "syncTime");
        QTRY_VERIFY_WITH_TIMEOUT(!sync->property("syncing").toBool(), 8000);
        QCOMPARE(sync->property("syncedOnce").toBool(), false);
        // Both providers were tried before the error was reported.
        QVERIFY(primary.requests >= 1);
        QVERIFY(fallback.requests >= 1);
        QVERIFY(sync->property("error").toString().contains(QStringLiteral("失败")));
        QVERIFY(!sync->property("alertText").toString().isEmpty());
        // 未校时时仍按内置标准偏移显示（北京 +8，伦敦标准 0）
        QCOMPARE(sync->property("syncedOffsets").toMap().size(), 0);
        QVariant offset;
        QVERIFY(QMetaObject::invokeMethod(sync.data(), "offsetOf", Q_RETURN_ARG(QVariant, offset),
                                          Q_ARG(QVariant, QStringLiteral("Europe/London"))));
        QCOMPARE(offset.toInt(), 0);
        QVERIFY(!sync->property("status").toString().isEmpty());
        QVERIFY(sync->property("status").toString().contains(QStringLiteral("线路二")));
    }

    // 计时器轮盘不能自己改值：Tumbler 首次布局/居中会动 currentIndex，
    // 之前会把默认 5 分钟写成别的值（真机 settings 已被污染过一次）。
    void timerWheelsDoNotChangeValuesOnTheirOwn() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 800}, {"height", 365}, {"uiScale", 0.75}, {"active", true}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QQuickWindow window;
        window.resize(800, 365);
        page->setParentItem(window.contentItem());
        page->setWidth(800);
        page->setHeight(365);
        window.show();
        QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, 1));
        QTest::qWait(1500);   // 超过轮盘的 ready(600ms) 与布局稳定时间
        QCOMPARE(page->property("timerHours").toInt(), 0);
        QCOMPARE(page->property("timerMinutes").toInt(), 5);
        QCOMPARE(page->property("timerSeconds").toInt(), 0);
        QCOMPARE(page->property("timerLabel").toString(), QStringLiteral("05:00"));

        // 预设仍然能改值（程序性变化要能推进轮盘，且不反向污染）
        QMetaObject::invokeMethod(page, "applyPreset", Q_ARG(QVariant, 10));
        QTest::qWait(300);
        QCOMPARE(page->property("timerMinutes").toInt(), 10);
        QCOMPARE(page->property("timerLabel").toString(), QStringLiteral("10:00"));
    }

    // 340px 宽的卡片是双栏的硬约束：卡片内任何可见控件都不得越过卡片右边界。
    // 计时器预设原本按页面宽度算列数、三组步进器挤一行，真机上「秒」直接被挤出卡片。
    void paneContentStaysInsideCard() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent syncComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(syncComponent.isReady(), qPrintable(syncComponent.errorString()));
        QScopedPointer<QObject> sync(syncComponent.create());
        QVERIFY(sync);
        QQmlComponent alarmComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QScopedPointer<QObject> alarm(alarmComponent.create());
        QVERIFY(alarm);
        QVariant ok;
        QVariantMap alarmRecord;
        alarmRecord.insert(QStringLiteral("hour"), 6);
        alarmRecord.insert(QStringLiteral("minute"), 45);
        alarmRecord.insert(QStringLiteral("name"), QStringLiteral("起床"));
        alarmRecord.insert(QStringLiteral("ringSeconds"), 30);
        alarmRecord.insert(QStringLiteral("snoozeMinutes"), 5);
        QMetaObject::invokeMethod(alarm.data(), "addAlarm", Q_RETURN_ARG(QVariant, ok),
                                  Q_ARG(QVariant, alarmRecord));
        QVERIFY(ok.toBool());

        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        // 真机视口：页面 800×365、uiScale 0.75 → 双栏、每栏 340
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 800}, {"height", 365}, {"uiScale", 0.75}, {"active", true},
             {"alarmEngine", QVariant::fromValue(alarm.data())},
             {"timeSync", QVariant::fromValue(sync.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QQuickWindow window;
        window.resize(800, 365);
        page->setParentItem(window.contentItem());
        page->setWidth(800);
        page->setHeight(365);
        window.show();
        QTest::qWait(200);

        const QStringList cards{QStringLiteral("clockLocalPane"), QStringLiteral("clockWorldPane"),
                                QStringLiteral("timerInputPane"), QStringLiteral("timerRunPane"),
                                QStringLiteral("stopwatchRunPane"), QStringLiteral("stopwatchLapPane"),
                                QStringLiteral("alarmEditPane"), QStringLiteral("alarmListPane")};
        QStringList violations;
        for (int tab = 0; tab < 4; ++tab) {
            QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, tab));
            QTest::qWait(200);
            for (const QString &name : cards) {
                auto *card = page->findChild<QQuickItem *>(name);
                if (!card || !card->isVisible())
                    continue;
                std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                    for (QQuickItem *child : item->childItems()) {
                        if (child->isVisible() && child->width() > 0) {
                            const QPointF topLeft = child->mapToItem(card, QPointF(0, 0));
                            const qreal right = topLeft.x() + child->width();
                            if (topLeft.x() < -1.0 || right > card->width() + 1.0) {
                                violations << QStringLiteral("%1/%2 x=%3 right=%4 cardW=%5")
                                                  .arg(name,
                                                       child->objectName().isEmpty()
                                                           ? QString::fromLatin1(child->metaObject()->className())
                                                           : child->objectName())
                                                  .arg(topLeft.x(), 0, 'f', 1)
                                                  .arg(right, 0, 'f', 1)
                                                  .arg(card->width(), 0, 'f', 1);
                            }
                        }
                        walk(child);
                    }
                };
                walk(card);
            }
        }
        for (const QString &violation : violations)
            qWarning("TIME_CARD_OVERFLOW %s", qPrintable(violation));
        QVERIFY2(violations.isEmpty(), qPrintable(violations.mid(0, 8).join(QStringLiteral(" | "))));
    }

    // 闹钟点行回填编辑：回填、就地保存（保留启用位）、取消回新建态、冲突拒绝。
    void alarmRowEditFlow() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent syncComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(syncComponent.isReady(), qPrintable(syncComponent.errorString()));
        QScopedPointer<QObject> sync(syncComponent.create());
        QVERIFY(sync);
        QQmlComponent alarmComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QVERIFY2(alarmComponent.isReady(), qPrintable(alarmComponent.errorString()));
        QScopedPointer<QObject> alarm(alarmComponent.create());
        QVERIFY(alarm);
        auto add = [&alarm](int hour, int minute, const QString &name, int ring, int snooze) {
            QVariantMap record;
            record.insert(QStringLiteral("hour"), hour);
            record.insert(QStringLiteral("minute"), minute);
            record.insert(QStringLiteral("repeatDaily"), true);
            record.insert(QStringLiteral("name"), name);
            record.insert(QStringLiteral("ringtone"), QString());
            record.insert(QStringLiteral("ringSeconds"), ring);
            record.insert(QStringLiteral("snoozeMinutes"), snooze);
            QVariant ok;
            QMetaObject::invokeMethod(alarm.data(), "addAlarm", Q_RETURN_ARG(QVariant, ok),
                                      Q_ARG(QVariant, record));
            return ok.toBool();
        };
        QVERIFY(add(6, 45, QStringLiteral("起床"), 20, 5));
        QVERIFY(add(7, 15, QStringLiteral("上班"), 30, 10));
        auto *model = alarm->property("alarms").value<QObject *>();
        QVERIFY(model);
        // 读回条目走持久化的 JSON：ListModel.get() 返回的是 QObject 包装，不适合断言，
        // 而 persist() 每次改动都会写 time/alarms，正好同时验证落盘内容。
        auto rowAt = [&host](int index) {
            const QJsonArray rows =
                QJsonDocument::fromJson(host.settings.value(QStringLiteral("time/alarms")).toUtf8())
                    .array();
            return index < rows.size() ? rows.at(index).toObject() : QJsonObject();
        };

        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0},
             {"alarmEngine", QVariant::fromValue(alarm.data())},
             {"timeSync", QVariant::fromValue(sync.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);
        // 标签页是 Loader 懒加载、行由 Repeater 生成，都要真实窗口才稳定存在。
        QQuickWindow window;
        window.resize(1024, 600);
        page->setParentItem(window.contentItem());
        page->setWidth(1024);
        page->setHeight(600);
        window.show();
        QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, 3));
        QTest::qWait(200);
        auto *pane = page->findChild<QQuickItem *>(QStringLiteral("alarmEditPane"));
        QVERIFY(pane);
        QCOMPARE(pane->property("title").toString(), QStringLiteral("新建闹钟"));

        // 点第 2 行 → 左栏回填、标题切编辑态
        auto *tap = findItemByObjectName(page, QStringLiteral("alarmRowTap1"));
        QVERIFY(tap);
        // 真点一下：用真实鼠标事件走行点击，避免直接调私有函数绕过接线。
        const QPointF tapCenter = tap->mapToScene(QPointF(tap->width() / 2, tap->height() / 2));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, tapCenter.toPoint());
        QTest::qWait(80);
        QCOMPARE(page->property("editingAlarmIndex").toInt(), 1);
        QVERIFY(page->property("editingAlarm").toBool());
        QCOMPARE(page->property("alarmHour").toInt(), 7);
        QCOMPARE(page->property("alarmMinute").toInt(), 15);
        QCOMPARE(page->property("alarmName").toString(), QStringLiteral("上班"));
        QCOMPARE(page->property("alarmRingSeconds").toInt(), 30);
        QCOMPARE(page->property("alarmSnoozeMinutes").toInt(), 10);
        QTRY_COMPARE(pane->property("title").toString(), QStringLiteral("编辑闹钟"));

        // 启用位先关掉：就地保存必须保留它，条数不变
        QMetaObject::invokeMethod(alarm.data(), "setEnabled", Q_ARG(QVariant, 1), Q_ARG(QVariant, false));
        page->setProperty("alarmHour", 21);
        page->setProperty("alarmMinute", 5);
        page->setProperty("alarmName", QStringLiteral("夜班"));
        QMetaObject::invokeMethod(page, "saveAlarm");
        QCOMPARE(model->property("count").toInt(), 2);
        QCOMPARE(rowAt(1).value(QStringLiteral("hour")).toInt(), 21);
        QCOMPARE(rowAt(1).value(QStringLiteral("minute")).toInt(), 5);
        QCOMPARE(rowAt(1).value(QStringLiteral("name")).toString(), QStringLiteral("夜班"));
        QCOMPARE(rowAt(1).value(QStringLiteral("alarmEnabled")).toBool(), false);
        // 保存后回新建态，表单复位
        QCOMPARE(page->property("editingAlarmIndex").toInt(), -1);
        QCOMPARE(page->property("alarmHour").toInt(), 7);
        QCOMPARE(page->property("alarmName").toString(), QString());
        QTRY_COMPARE(pane->property("title").toString(), QStringLiteral("新建闹钟"));

        // 取消：回新建态且不落盘
        QMetaObject::invokeMethod(page, "beginEditAlarm", Q_ARG(QVariant, 0));
        QCOMPARE(page->property("editingAlarmIndex").toInt(), 0);
        QMetaObject::invokeMethod(page, "cancelAlarmEdit");
        QCOMPARE(page->property("editingAlarmIndex").toInt(), -1);
        QCOMPARE(page->property("alarmName").toString(), QString());
        QCOMPARE(rowAt(0).value(QStringLiteral("name")).toString(), QStringLiteral("起床"));

        // 冲突：改成与另一条同名同时刻 → 不写模型，仍在编辑态并提示
        QMetaObject::invokeMethod(page, "beginEditAlarm", Q_ARG(QVariant, 0));
        page->setProperty("alarmHour", 21);
        page->setProperty("alarmMinute", 5);
        page->setProperty("alarmName", QStringLiteral("夜班"));
        QSignalSpy notified(page, SIGNAL(notifyRequested(QString, QString, bool)));
        QMetaObject::invokeMethod(page, "saveAlarm");
        QCOMPARE(notified.count(), 1);
        QCOMPARE(rowAt(0).value(QStringLiteral("hour")).toInt(), 6);
        QCOMPARE(page->property("editingAlarmIndex").toInt(), 0);

        // 编辑中的条目被删掉 → 自动回新建态
        QMetaObject::invokeMethod(alarm.data(), "removeAlarm", Q_ARG(QVariant, 0));
        QCOMPARE(page->property("editingAlarmIndex").toInt(), -1);
        QCOMPARE(model->property("count").toInt(), 1);
        QCOMPARE(page->property("alarmHour").toInt(), 7);
    }

    // 双栏断点跟随作者划定的缩放边界：width >= 900 * max(1, uiScale)。
    void twoPaneBreakpointFollowsScale_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<double>("uiScale");
        QTest::addColumn<bool>("twoPane");
        QTest::newRow("1024x600@1.0") << 1024 << 600 << 1.0 << true;
        QTest::newRow("1024x600@1.5-big-text") << 1024 << 600 << 1.5 << false;  // 宽断点 1140
        QTest::newRow("1400x900@1.5-big-text") << 1400 << 900 << 1.5 << true;   // 1140/630 都过
        QTest::newRow("1400x900@1.0") << 1400 << 900 << 1.0 << true;
        QTest::newRow("app-viewport-1097x502@1.09") << 1097 << 502 << 1.09 << true;
        QTest::newRow("pi-real-800x365@0.75") << 800 << 365 << 0.75 << true;    // 真机视口也两栏
        QTest::newRow("800x460@0.75") << 800 << 460 << 0.75 << true;
        QTest::newRow("760x600@1.0") << 760 << 600 << 1.0 << true;
        QTest::newRow("700x600@1.0") << 700 << 600 << 1.0 << false;
        QTest::newRow("900x600@0.65") << 900 << 600 << 0.65 << true;  // 低缩放仍按 760
    }

    void twoPaneBreakpointFollowsScale() {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(double, uiScale);
        QFETCH(bool, twoPane);
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", width}, {"height", height}, {"uiScale", uiScale}, {"active", true}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QCOMPARE(page->property("twoPane").toBool(), twoPane);
        QQuickWindow window;
        window.resize(width, height);
        page->setParentItem(window.contentItem());
        page->setWidth(width);
        page->setHeight(height);
        window.show();
        QTest::qWait(160);
        auto *left = page->findChild<QQuickItem *>(QStringLiteral("clockLocalPane"));
        auto *right = page->findChild<QQuickItem *>(QStringLiteral("clockWorldPane"));
        QVERIFY(left && right);
        if (twoPane) {
            // 并排：右卡在左卡右侧
            QVERIFY(right->mapToItem(page, QPointF(0, 0)).x() >
                    left->mapToItem(page, QPointF(0, 0)).x() + left->width() - 1);
            QCOMPARE(page->findChild<QQuickItem *>(QStringLiteral("timePage"))
                         ->property("contentHeight").toDouble(),
                     qobject_cast<QQuickItem *>(page->findChild<QQuickItem *>(QStringLiteral("timePage")))->height());
        } else {
            // 堆叠：右卡在左卡下方，页面整体可滚
            QVERIFY(right->mapToItem(page, QPointF(0, 0)).y() >
                    left->mapToItem(page, QPointF(0, 0)).y() + left->height() - 1);
            auto *flick = page->findChild<QQuickItem *>(QStringLiteral("timePage"));
            QVERIFY(flick);
            // 单栏：两张卡上下堆叠，第二张必须在滚动范围内（放得下就不滚，放不下整页滚）
            QVERIFY2(right->mapToItem(page, QPointF(0, right->height())).y() <=
                         flick->property("contentHeight").toDouble() + 1,
                     "narrow layout must keep the second card inside the scroll extent");
        }
    }

    // 双栏正文可滚时必须看得见：常驻细指示条 + 未滚到底时的底部渐隐。
    // 溢出基准取 App 真实视口（1400×900 窗口里的 1097×502 页面），因为 1024×600
    // 的整页高度反而更宽裕。
    void paneScrollAffordanceIsVisible() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent syncComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(syncComponent.isReady(), qPrintable(syncComponent.errorString()));
        QScopedPointer<QObject> syncObject(syncComponent.create());
        QVERIFY(syncObject);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1097}, {"height", 502}, {"uiScale", 1.09375}, {"active", true},
             {"timeSync", QVariant::fromValue(syncObject.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QQuickWindow window;
        window.resize(1097, 502);
        page->setParentItem(window.contentItem());
        page->setWidth(1097);
        page->setHeight(502);
        window.show();
        QTest::qWait(200);

        auto *body = page->findChild<QQuickItem *>(QStringLiteral("clockWorldPaneBody"));
        auto *rail = page->findChild<QQuickItem *>(QStringLiteral("clockWorldPaneScrollRail"));
        auto *fade = page->findChild<QQuickItem *>(QStringLiteral("clockWorldPaneScrollFade"));
        QVERIFY(body && rail && fade);
        const qreal content = body->property("contentHeight").toDouble();
        const qreal overflow = content - body->height();
        QVERIFY2(overflow > 4 * 1.09375,
                 qPrintable(QStringLiteral("world clock body must overflow at the app viewport: body=%1 content=%2")
                                .arg(body->height()).arg(content)));
        QVERIFY2(rail->height() > 0 && rail->width() > 0, "scroll rail must take space");
        QVERIFY(rail->isVisible());
        QVERIFY2(fade->isVisible(), "bottom fade must show while content remains below");
        QVERIFY2(rail->mapToItem(page, QPointF(0, 0)).x() + rail->width() <= page->width(),
                 "rail must stay inside the page");
        // 滚到底：渐隐消失，指示条仍常驻
        body->setProperty("contentY", overflow);
        QTest::qWait(140);
        QVERIFY(!fade->isVisible());
        QVERIFY(rail->isVisible());

        // 左栏时钟卡：假溢出（几像素取整）不得画出滚动条，真溢出才允许。
        auto *localBody = page->findChild<QQuickItem *>(QStringLiteral("clockLocalPaneBody"));
        auto *localRail = page->findChild<QQuickItem *>(QStringLiteral("clockLocalPaneScrollRail"));
        QVERIFY(localBody && localRail);
        const qreal localOverflow = localBody->property("contentHeight").toDouble() - localBody->height();
        QVERIFY2(localRail->isVisible() == (localOverflow > 4 * 1.09375),
                 qPrintable(QStringLiteral("clock card rail must match real overflow: body=%1 content=%2 rail=%3")
                                .arg(localBody->height())
                                .arg(localBody->property("contentHeight").toDouble())
                                .arg(localRail->isVisible())));

        // 1024×600（Pi 目标屏）：两栏都不该出现“假溢出”提示，能放下就绝不画滚动条
        page->setWidth(1024);
        page->setHeight(600);
        page->setProperty("uiScale", 1.0);
        window.resize(1024, 600);
        QTest::qWait(200);
        for (const QString &name : {QStringLiteral("clockLocalPane"), QStringLiteral("clockWorldPane")}) {
            auto *paneBody = page->findChild<QQuickItem *>(name + QStringLiteral("Body"));
            auto *paneRail = page->findChild<QQuickItem *>(name + QStringLiteral("ScrollRail"));
            QVERIFY(paneBody && paneRail);
            const qreal paneOverflow = paneBody->property("contentHeight").toDouble() - paneBody->height();
            QVERIFY2(paneRail->isVisible() == (paneOverflow > 4.0),
                     qPrintable(name + QStringLiteral(": body=%1 content=%2 rail=%3")
                                          .arg(paneBody->height())
                                          .arg(paneBody->property("contentHeight").toDouble())
                                          .arg(paneRail->isVisible())));
        }
    }

    // 设置页分区：控件的读写都落到 TimeSync / AlarmEngine 上，而不是页面自己的副本。
    void settingsSectionDrivesOwners() {
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent syncComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(syncComponent.isReady(), qPrintable(syncComponent.errorString()));
        QScopedPointer<QObject> sync(syncComponent.create());
        QVERIFY(sync);
        QQmlComponent alarmComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QScopedPointer<QObject> alarm(alarmComponent.create());
        QVERIFY(alarm);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSyncSettings.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"timeSync", QVariant::fromValue(sync.data())},
             {"alarmEngine", QVariant::fromValue(alarm.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *section = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(section);
        QQuickWindow window;
        window.resize(720, 900);
        section->setParentItem(window.contentItem());
        section->setWidth(700);
        window.show();
        QTest::qWait(160);

        // 模式按钮写回 TimeSync（并持久化）
        auto *custom = section->findChild<QQuickItem *>(QStringLiteral("timeSyncModeCustom"));
        QVERIFY(custom);
        QMetaObject::invokeMethod(custom, "clicked");
        QCOMPARE(sync->property("apiMode").toString(), QStringLiteral("custom"));
        QCOMPARE(host.settings.value(QStringLiteral("time/api-mode")), QStringLiteral("custom"));
        // 自定义地址输入框只在自定义模式下出现
        auto *field = section->findChild<QQuickItem *>(QStringLiteral("timeSyncHostField"));
        QVERIFY(field);
        QVERIFY(field->isVisible());
        // 校时状态就是 TimeSync 的状态
        auto *status = section->findChild<QQuickItem *>(QStringLiteral("timeSyncStatus"));
        QVERIFY(status);
        QCOMPARE(status->property("text").toString(), sync->property("status").toString());
        // 线路失效/校时失败必须弹窗（曾经只有状态行文字，用户看不到）
        QVERIFY(!section->property("alertPopupOpen").toBool());
        sync->setProperty("alertText", QStringLiteral("线路二 · HTTP 503\n所有校时接口均不可用，仍显示内置时区。"));
        QTRY_VERIFY_WITH_TIMEOUT(section->property("alertPopupOpen").toBool(), 2000);
        // 清掉提示就收（「知道了」就是 clearAlert + 关闭）
        QMetaObject::invokeMethod(sync.data(), "clearAlert");
        QTRY_VERIFY_WITH_TIMEOUT(!section->property("alertPopupOpen").toBool(), 2000);

        // 整点报时开关写回 AlarmEngine
        auto *chime = section->findChild<QQuickItem *>(QStringLiteral("chimeSwitch"));
        QVERIFY(chime);
        QVERIFY(!alarm->property("chimeEnabled").toBool());
        QMetaObject::invokeMethod(chime, "clicked");
        QVERIFY(alarm->property("chimeEnabled").toBool());
        QTest::qWait(80);
        QVERIFY(host.settings.value(QStringLiteral("time/chime")).contains(QStringLiteral("enabled")));
        // 时段步进器改的是 AlarmEngine 的小时
        auto *fromStepper = section->findChild<QQuickItem *>(QStringLiteral("chimeFromStepper"));
        QVERIFY(fromStepper);
        const int before = alarm->property("chimeFromHour").toInt();
        QMetaObject::invokeMethod(fromStepper, "adjusted", Q_ARG(int, 1));
        QCOMPARE(alarm->property("chimeFromHour").toInt(), (before + 1) % 24);

        // 新文案必须同时有英文词条（I18n.words 按键替换，缺词条会漏中文）
        TimeToolsHost english;
        english.language = QStringLiteral("en");
        QQmlEngine englishEngine;
        englishEngine.rootContext()->setContextProperty("systemTools", &english);
        QQmlComponent englishSyncComponent(&englishEngine,
            QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QScopedPointer<QObject> englishSync(englishSyncComponent.create());
        QVERIFY(englishSync);
        QQmlComponent englishComponent(&englishEngine,
            QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSyncSettings.qml")));
        QScopedPointer<QObject> englishObject(englishComponent.createWithInitialProperties(
            {{"timeSync", QVariant::fromValue(englishSync.data())},
             {"alarmEngine", QVariant::fromValue(alarm.data())}}));
        QVERIFY2(englishObject, qPrintable(englishComponent.errorString()));
        auto *englishSwitch = englishObject->findChild<QQuickItem *>(QStringLiteral("chimeSwitch"));
        QVERIFY(englishSwitch);
        QCOMPARE(englishSwitch->property("text").toString(), QStringLiteral("Hourly chime"));
        auto *englishStatus = englishObject->findChild<QQuickItem *>(QStringLiteral("timeSyncStatus"));
        QVERIFY(englishStatus);
        QVERIFY2(!englishStatus->property("text").toString().contains(QChar(0x6708)) &&
                 !englishStatus->property("text").toString().contains(QChar(0x5876)),
                 qPrintable(englishStatus->property("text").toString()));
    }

    // 双栏布局：两栏必须正好占满视口、等高、底边不外溢，正文改在卡片内部滚动。
    // 早期版本把列高写成 max(视口, 内容高) 且不给 Loader 下限，卡片被顶出视口后
    // 又被页面 clip，右栏末行（“管理城市”）永远看不到。
    // 构造顺序回归：属性初值在 Component.onCompleted 之前就写进属性，若那时允许持久化，
    // 默认值会先覆盖磁盘设置，load() 再读回默认值 —— 表现为“重启后设置全丢”。
    void storedSettingsSurviveConstruction() {
        TimeToolsHost host;
        host.settings.insert(QStringLiteral("time/cities"),
                             QStringLiteral("[\"Asia/Tokyo\",\"Europe/London\"]"));
        host.settings.insert(QStringLiteral("time/api-mode"), QStringLiteral("custom"));
        host.settings.insert(QStringLiteral("time/api-host"), QStringLiteral("https://time.example.com"));
        host.settings.insert(QStringLiteral("time/chime"),
                             QStringLiteral("{\"enabled\":true,\"from\":9,\"to\":21}"));
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);

        QQmlComponent syncComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(syncComponent.isReady(), qPrintable(syncComponent.errorString()));
        QScopedPointer<QObject> sync(syncComponent.create());
        QVERIFY(sync);
        QCOMPARE(sync->property("zones").toList().size(), 2);
        QCOMPARE(sync->property("zones").toList().value(0).toString(), QStringLiteral("Asia/Tokyo"));
        QCOMPARE(sync->property("apiMode").toString(), QStringLiteral("custom"));
        QCOMPARE(sync->property("customHost").toString(), QStringLiteral("https://time.example.com"));
        QCOMPARE(host.settings.value(QStringLiteral("time/cities")),
                 QStringLiteral("[\"Asia/Tokyo\",\"Europe/London\"]"));

        QQmlComponent alarmComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QVERIFY2(alarmComponent.isReady(), qPrintable(alarmComponent.errorString()));
        QScopedPointer<QObject> alarm(alarmComponent.create());
        QVERIFY(alarm);
        QCOMPARE(alarm->property("chimeEnabled").toBool(), true);
        QCOMPARE(alarm->property("chimeFromHour").toInt(), 9);
        QCOMPARE(alarm->property("chimeToHour").toInt(), 21);
        QVERIFY(host.settings.value(QStringLiteral("time/chime")).contains(QStringLiteral("\"from\":9")));

        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", 1024}, {"height", 600}, {"uiScale", 1.0},
             {"timeSync", QVariant::fromValue(sync.data())},
             {"alarmEngine", QVariant::fromValue(alarm.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QCOMPARE(page->property("alarmRingtone").toString(),
                 QStringLiteral("qrc:/ringtones/default-ringtone.mp3"));
    }

    void twoPanePanesFillViewport_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<double>("uiScale");
        // 真机视口：宽度够就两栏（短屏靠卡片内部滚动）
        QTest::newRow("pi-real-800x365") << 800 << 365 << 0.75;
        // 被窗口管理器压窄的桌面窗口：页面只有 560，左栏下限不能再吃满
        QTest::newRow("narrow-page-560x365") << 560 << 365 << 0.65;
        // 窄屏兜底：单栏、整页滚动
        QTest::newRow("narrow-700x600-single") << 700 << 600 << 1.0;
        QTest::newRow("pi-1024x600") << 1024 << 600 << 1.0;
        QTest::newRow("desktop-1400x900") << 1400 << 900 << 1.0;
    }

    void twoPanePanesFillViewport() {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(double, uiScale);
        TimeToolsHost host;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &host);
        QQmlComponent syncComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeSync.qml")));
        QVERIFY2(syncComponent.isReady(), qPrintable(syncComponent.errorString()));
        QScopedPointer<QObject> syncObject(syncComponent.create());
        QVERIFY(syncObject);
        QQmlComponent alarmComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/AlarmEngine.qml")));
        QVERIFY2(alarmComponent.isReady(), qPrintable(alarmComponent.errorString()));
        QScopedPointer<QObject> alarmObject(alarmComponent.create());
        QVERIFY(alarmObject);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/TimeTools.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties(
            {{"width", width}, {"height", height}, {"uiScale", uiScale}, {"active", true},
             {"alarmEngine", QVariant::fromValue(alarmObject.data())},
             {"timeSync", QVariant::fromValue(syncObject.data())}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(page);
        // 栅格分配需要真实的 polish 循环，offscreen 也要 show()。
        QQuickWindow window;
        window.resize(width, height);
        page->setParentItem(window.contentItem());
        page->setWidth(width);
        page->setHeight(height);
        window.show();
        QTest::qWait(150);

        auto *flick = page->findChild<QQuickItem *>(QStringLiteral("timePage"));
        QVERIFY(flick);

        struct Row { int tab; const char *name; const char *left; const char *right; };
        const Row rows[] = {{0, "clock", "clockLocalPane", "clockWorldPane"},
                            {1, "timer", "timerInputPane", "timerRunPane"},
                            {2, "stopwatch", "stopwatchRunPane", "stopwatchLapPane"},
                            {3, "alarm", "alarmEditPane", "alarmListPane"}};
        for (const Row &row : rows) {
            QMetaObject::invokeMethod(page, "setTab", Q_ARG(QVariant, row.tab));
            QTest::qWait(160);
            auto *left = page->findChild<QQuickItem *>(QLatin1String(row.left));
            auto *right = page->findChild<QQuickItem *>(QLatin1String(row.right));
            QVERIFY2(left && right, row.name);
            auto *leftBody = page->findChild<QQuickItem *>(QLatin1String(row.left) + QStringLiteral("Body"));
            auto *rightBody = page->findChild<QQuickItem *>(QLatin1String(row.right) + QStringLiteral("Body"));
            QVERIFY2(leftBody && rightBody, row.name);

            qInfo() << row.name << "page" << page->width() << page->height()
                    << "flick" << flick->height()
                    << "panes" << left->height() << right->height()
                    << "bodies" << leftBody->height() << rightBody->height()
                    << "content" << leftBody->property("contentHeight").toDouble()
                    << rightBody->property("contentHeight").toDouble();

            if (!page->property("twoPane").toBool()) {
                // 高不够或太窄：退回单栏，两卡上下堆叠、整页滚动
                QVERIFY2(right->mapToItem(page, QPointF(0, 0)).y() >
                             left->mapToItem(page, QPointF(0, 0)).y() + left->height() - 1,
                         row.name);
                // 单栏时两卡都在滚动范围内（放得下就不滚，放不下整页滚，绝不裁）
                const qreal extent = flick->property("contentHeight").toDouble();
                QVERIFY2(right->mapToItem(page, QPointF(0, right->height())).y() <= extent + 1,
                         qPrintable(row.name + QStringLiteral(": second card must be inside the scroll extent")));
                QVERIFY2(left->height() + 1 >= left->property("implicitHeight").toDouble(),
                         qPrintable(row.name + QStringLiteral(": single-column card keeps content height")));
                continue;
            }
            // 1) 双栏时页面自身不滚动：视口就是全部可见高度。
            QVERIFY2(qAbs(flick->property("contentHeight").toDouble() - flick->height()) <= 1,
                     qPrintable(row.name + QStringLiteral(": page scrolls in two-pane")));
            // 2) 两张卡并排，且左卡在左。
            const QPointF leftTop = left->mapToItem(page, QPointF(0, 0));
            const QPointF rightTop = right->mapToItem(page, QPointF(0, 0));
            QVERIFY2(rightTop.x() > leftTop.x() + left->width() - 1, row.name);
            // 3) 两卡等高，且底边不越出视口（曾被 clip 的就是这一条）。
            QVERIFY2(qAbs(left->height() - right->height()) <= 1, row.name);
            for (QQuickItem *card : {left, right}) {
                QVERIFY2(card->height() > 220, row.name);
                const qreal bottom = card->mapToItem(page, QPointF(0, card->height())).y();
                QVERIFY2(bottom <= page->height() + 1, qPrintable(row.name + QStringLiteral(": card spills out")));
                QVERIFY2(card->height() > flick->height() * 0.6,
                         qPrintable(row.name + QStringLiteral(": card did not fill the row")));
            }
            // 4) 正文铺满卡片剩余高度，溢出时在卡片内部滚动而不是被切掉。
            for (QQuickItem *body : {leftBody, rightBody}) {
                QVERIFY(body->height() > 80);
                const QPointF bodyBottom = body->mapToItem(page, QPointF(0, body->height()));
                auto *card = qobject_cast<QQuickItem *>(body->parentItem());
                QVERIFY(card);
                const qreal cardBottom = card->mapToItem(page, QPointF(0, card->height())).y();
                QVERIFY2(bodyBottom.y() <= cardBottom + 1, row.name);
                const qreal content = body->property("contentHeight").toDouble();
                if (content > body->height() + 2) {
                    // 内容比可视区高：必须能滚到底（contentHeight 覆盖整段内容）。
                    QVERIFY2(content >= body->height(), row.name);
                    QVERIFY(body->property("contentY").toDouble() >= 0);
                }
            }
        }
    }
};

QTEST_MAIN(TimeToolsTests)
#include "time_tools_test.moc"
