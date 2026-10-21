#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <functional>
#include "ui/TerminalUi.h"

class StartupTools : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString uiLanguage MEMBER language NOTIFY languageChanged)
public:
    QString language = QStringLiteral("zh");
    Q_INVOKABLE QString monochromeIcon(const QString &name, bool) {
        return QStringLiteral("qrc:/icons/material/light/") + name + QStringLiteral(".svg");
    }
signals:
    void languageChanged();
};

class StartupSplashTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Material"));
        initializeTerminalUi();
    }
    void milestonesAndLayout_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const auto &language : {QStringLiteral("zh"), QStringLiteral("en")})
            for (const QSize size : {QSize(320, 320), QSize(1024, 538), QSize(1024, 600)})
                QTest::newRow(qPrintable(language + QString::number(size.height()))) << language << size;
    }
    void milestonesAndLayout() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        StartupTools tools;
        tools.language = language;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &tools);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/OrangePiTerminal/StartupSplash.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *splash = qobject_cast<QQuickItem *>(object.data());
        QVERIFY(splash);
        splash->setSize(size);
        QSignalSpy finished(splash, SIGNAL(finished()));
        QCOMPARE(splash->property("completedSteps").toInt(), 0);
        splash->setProperty("cacheReady", true); // Later work cannot skip earlier milestones.
        QCOMPARE(splash->property("completedSteps").toInt(), 0);
        splash->setProperty("interfaceReady", true);
        QCOMPARE(splash->property("completedSteps").toInt(), 1);
        splash->setProperty("wallpaperReady", true);
        QCOMPARE(splash->property("completedSteps").toInt(), 2);
        splash->setProperty("cacheReady", false);
        splash->setProperty("frameReady", true);
        QCOMPARE(splash->property("completedSteps").toInt(), 3);
        QCOMPARE(splash->property("progress").toDouble(), 0.75);
        QTest::qWait(80);
        auto *progress = splash->findChild<QObject *>("startupProgress");
        QVERIFY(progress);
        QCOMPARE(progress->property("value").toDouble(), 0.75);
        auto *content = splash->findChild<QQuickItem *>("startupContent");
        QVERIFY(content);
        QVERIFY2(content->y() >= 0, "Startup content clips above the window");
        QVERIFY(content->y() + content->height() <= size.height());
        QVERIFY(content->x() >= 0);
        QVERIFY(content->x() + content->width() <= size.width());
        if (size.height() >= 440) {
            std::function<QQuickItem *(QQuickItem *, const QString &)> findVisual;
            findVisual = [&findVisual](QQuickItem *item, const QString &name) -> QQuickItem * {
                if (item->objectName() == name) return item;
                for (auto *child : item->childItems())
                    if (auto *found = findVisual(child, name)) return found;
                return nullptr;
            };
            for (int index = 0; index < 4; ++index) {
                auto *slot = findVisual(splash, QString("startupStepIconSlot%1").arg(index));
                auto *text = findVisual(splash, QString("startupStepText%1").arg(index));
                auto *icon = findVisual(splash, QString("startupStepIcon%1").arg(index));
                QVERIFY(slot && text && icon);
                QCOMPARE(slot->width(), 24.0);
                QCOMPARE(slot->height(), 24.0);
                QVERIFY(qAbs(slot->y() + slot->height()/2 - text->y() - text->height()/2) < 0.5);
                QCOMPARE(icon->property("name").toString(), index < 3 ? QString("check_circle") : QString("pending"));
                QVERIFY(icon->property("source").toUrl().path().contains(index < 3 ? "check_circle" : "pending"));
            }
        }
        QVERIFY(splash->property("statusText").toString().contains(
            language == "en" ? "blur caches" : "模糊缓存"));
        QCOMPARE(finished.count(), 0);
        splash->setProperty("cacheReady", true);
        QCOMPARE(splash->property("completedSteps").toInt(), 4);
        QVERIFY(!splash->property("usingFallback").toBool());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2500);
        QCOMPARE(splash->opacity(), 0.0);
    }
    void slowCacheFallback() {
        StartupTools tools;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &tools);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/OrangePiTerminal/StartupSplash.qml"));
        QScopedPointer<QObject> splash(component.create());
        QVERIFY2(splash, qPrintable(component.errorString()));
        QSignalSpy finished(splash.data(), SIGNAL(finished()));
        splash->setProperty("interfaceReady", true);
        splash->setProperty("wallpaperReady", true);
        splash->setProperty("frameReady", true);
        splash->setProperty("minimumElapsed", true);
        QCOMPARE(splash->property("completedSteps").toInt(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(splash->property("cacheDeadlineElapsed").toBool(), 7500);
        QVERIFY(splash->property("usingFallback").toBool());
        QVERIFY(!splash->property("cacheReady").toBool());
        QVERIFY(splash->property("statusText").toString().contains("兼容渲染"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1500);
    }
};
QTEST_MAIN(StartupSplashTests)
#include "startup_splash_test.moc"
