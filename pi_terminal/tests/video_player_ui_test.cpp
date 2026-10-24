#include "ui/TerminalUi.h"
#ifdef TERMINAL_ISOLATED_UI_TEST
#include "VlcVideoItem.h"
#endif
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QtTest>

class VideoTools : public QObject {
    Q_OBJECT
    Q_PROPERTY(double mediaGain MEMBER gain NOTIFY gainChanged)
    Q_PROPERTY(QString uiLanguage MEMBER language CONSTANT)
public:
    double gain = 1;
    QString language = QStringLiteral("zh");
    Q_INVOKABLE QVariantList files(const QString &) { return {}; }
    Q_INVOKABLE QUrl fileUrl(const QString &file) { return QUrl::fromLocalFile(file); }
    Q_INVOKABLE QString monochromeIcon(const QString &, bool) { return {}; }
signals:
    void gainChanged();
};

class VideoUiTests : public QObject {
    Q_OBJECT
private slots:
    void layoutAndGain_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("small") << QSize(320, 320);
        QTest::newRow("pi") << QSize(1024, 600);
        QTest::newRow("desktop") << QSize(1920, 1080);
    }
    void layoutAndGain() {
        QFETCH(QSize, size);
        VideoTools tools;
        QQuickWindow window;
        window.resize(size);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &tools);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/OrangePiTerminal/VideoPlayer.qml"));
        QScopedPointer<QObject> object(component.createWithInitialProperties({{"width", size.width()}, {"height", size.height()}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *page = qobject_cast<QQuickItem *>(object.data()); QVERIFY(page);
        page->setSize(size);
        page->setParentItem(window.contentItem());
        window.show();
        auto *backend = page->findChild<QQuickItem *>(QStringLiteral("vlcVideoOutput"));
        QVERIFY(backend);
        QVERIFY(!page->findChild<QObject *>(QStringLiteral("qtMultimediaVideoOutput")));
        page->setProperty("pageIndex", 1);
        QTest::qWait(50);
        QVERIFY(backend->width() > 0 && backend->height() > 0);
        QVERIFY(backend->width() <= size.width());
        page->setProperty("volume", 0.5); page->setProperty("muted", true);
        QCOMPARE(backend->property("volume").toDouble(), 0.5);
        QVERIFY(backend->property("muted").toBool());
        tools.gain = 0.5; emit tools.gainChanged();
        QTRY_VERIFY(qAbs(backend->property("volume").toDouble() - 0.25) < 0.01);
        QVERIFY(QMetaObject::invokeMethod(page, "pauseMedia"));
        QVERIFY(!page->property("playing").toBool());
        const QString artifacts = qEnvironmentVariable("TERMINAL_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            const QImage image = window.grabWindow();
            if (!image.isNull())
                QVERIFY(image.save(artifacts + QString("/video-ui-%1x%2.png").arg(size.width()).arg(size.height())));
            else
                qInfo("Window screenshot unavailable on this platform; layout/binding assertions still run.");
        }
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Material"));
#ifdef TERMINAL_ISOLATED_UI_TEST
    qmlRegisterType<VlcVideoItem>("Terminal.Native", 1, 0, "VlcVideoItem");
#else
    initializeTerminalUi();
#endif
    VideoUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "video_player_ui_test.moc"
