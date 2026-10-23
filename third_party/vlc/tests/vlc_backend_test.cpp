#include "VlcVideoItem.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtTest>
#include <QTemporaryFile>
#include <cmath>
#include <ctime>
#include <limits>
#include <memory>

class VlcTests : public QObject {
    Q_OBJECT
    QUrl clip() const { return QUrl::fromLocalFile(qEnvironmentVariable("TERMINAL_TEST_VIDEO")); }
private slots:
    void initTestCase() {
        QVERIFY2(QFileInfo::exists(clip().toLocalFile()), "Run the fixture generator, or set TERMINAL_TEST_VIDEO.");
        qmlRegisterType<VlcVideoItem>("Terminal.Native", 1, 0, "VlcVideoItem");
    }
    void functional() {
        VlcVideoItem item;
        item.setVolume(2); QCOMPARE(item.volume(), 1.0);
        item.setVolume(-1); QCOMPARE(item.volume(), 0.0);
        item.setVolume(std::numeric_limits<double>::quiet_NaN()); QCOMPARE(item.volume(), 0.0);
        item.setMuted(true); QVERIFY(item.muted());
        item.setSource(clip()); item.play();
        QTRY_VERIFY_WITH_TIMEOUT(item.decodedFrames() > 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(item.playing() && item.duration() > 4000 && item.seekable(), 5000);
        item.pause();
        QTRY_VERIFY_WITH_TIMEOUT(!item.playing(), 2000);
        const qint64 paused = item.position();
        QTest::qWait(350);
        QVERIFY(qAbs(item.position() - paused) < 150);
        item.setPosition(3000);
        item.play();
        QTRY_VERIFY_WITH_TIMEOUT(item.position() >= 2800, 5000);
        item.setVolume(0.45); item.setMuted(false);
        QCOMPARE(item.volume(), 0.45); QVERIFY(!item.muted());
        item.setPosition(item.duration() - 250);
        QTRY_VERIFY_WITH_TIMEOUT(!item.playing(), 5000);
        QVERIFY(!item.loading());
        item.stop(); QVERIFY(!item.playing()); QCOMPARE(item.position(), qint64(0));
        item.play(); QTRY_VERIFY_WITH_TIMEOUT(item.playing(), 5000);
        item.setSource(QUrl::fromLocalFile(clip().toLocalFile() + ".missing"));
        QCOMPARE(item.duration(), qint64(0)); QCOMPARE(item.decodedFrames(), qulonglong(0));
        item.play(); QTRY_VERIFY_WITH_TIMEOUT(!item.errorString().isEmpty(), 5000);
        QVERIFY(!item.playing()); QVERIFY(!item.loading());
        QTemporaryFile invalid; QVERIFY(invalid.open()); invalid.write("not a video"); invalid.flush();
        item.setSource(QUrl::fromLocalFile(invalid.fileName())); item.play();
        QTRY_VERIFY_WITH_TIMEOUT(!item.errorString().isEmpty(), 5000);
        item.setSource(clip()); QVERIFY(item.errorString().isEmpty()); item.play();
        QTRY_VERIFY_WITH_TIMEOUT(item.decodedFrames() > 0, 5000);
        item.setSource(QUrl()); QVERIFY(!item.playing()); QCOMPARE(item.duration(), qint64(0));
        item.setSource(QUrl("javascript:alert(1)")); item.play(); QVERIFY(!item.errorString().isEmpty());
        for (int i = 0; i < 3; ++i) {
            auto temporary = std::make_unique<VlcVideoItem>();
            temporary->setSource(clip()); temporary->setMuted(true); temporary->play();
            QTRY_VERIFY_WITH_TIMEOUT(temporary->decodedFrames() > 0, 5000);
        } // Destroy while callbacks are active.
    }
    void interface() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport Terminal.Native 1.0\n"
                          "VlcVideoItem { width: 640; height: 480; volume: 0.4; muted: true }", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *item = qobject_cast<VlcVideoItem *>(object.data()); QVERIFY(item);
        QCOMPARE(item->volume(), 0.4); QVERIFY(item->muted());
        item->setSource(clip()); item->play();
        QTRY_VERIFY_WITH_TIMEOUT(item->decodedFrames() > 0, 10000);
        QImage output(640, 480, QImage::Format_RGB32); output.fill(Qt::magenta);
        QPainter painter(&output); item->paint(&painter); painter.end();
        QCOMPARE(output.pixelColor(320, 0), QColor(Qt::black)); // Letterbox, not stretch.
        QVERIFY(output.pixelColor(320, 240) != QColor(Qt::black));
        const QString artifacts = qEnvironmentVariable("TERMINAL_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) QVERIFY(output.save(artifacts + "/vlc-letterbox.png"));
        for (const QSize size : {QSize(320, 320), QSize(1024, 600), QSize(1920, 1080)}) {
            item->setSize(size);
            QImage resized(size, QImage::Format_RGB32);
            QPainter resizedPainter(&resized); item->paint(&resizedPainter);
        }
    }
    void performance() {
        QQuickWindow window;
        window.setGeometry(0, 0, 640, 360);
        VlcVideoItem item(window.contentItem());
        item.setSize(QSizeF(640, 360));
        window.show();
        item.setMuted(true); item.setSource(clip());
        QElapsedTimer clock; clock.start();
        const auto cpuStart = std::clock();
        item.play();
        QTRY_VERIFY_WITH_TIMEOUT(item.decodedFrames() > 0, 10000);
        const auto firstFrame = clock.elapsed();
        const auto initialFrames = item.decodedFrames();
        QElapsedTimer sample; sample.start();
        qint64 worstHeartbeat = 0;
        while (sample.elapsed() < 2000) {
            const auto before = clock.elapsed(); QTest::qWait(10);
            worstHeartbeat = qMax(worstHeartbeat, clock.elapsed() - before);
        }
        const double fps = (item.decodedFrames() - initialFrames) * 1000.0 / sample.elapsed();
        const double cpu = 100.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC / (clock.elapsed() / 1000.0);
        const QJsonObject metrics{{"first_frame_ms", double(firstFrame)}, {"decoded_fps", fps},
                                  {"heartbeat_max_ms", double(worstHeartbeat)}, {"cpu_percent", cpu}};
        qInfo().noquote() << "MEDIA_METRICS" << QJsonDocument(metrics).toJson(QJsonDocument::Compact);
        auto limit = [](const char *name, double fallback) {
            bool ok = false; const double value = qEnvironmentVariable(name).toDouble(&ok);
            return ok ? value : fallback;
        };
        QVERIFY(firstFrame <= limit("TERMINAL_MAX_STARTUP_MS", 5000));
        QVERIFY(fps >= limit("TERMINAL_MIN_FPS", 10));
        QVERIFY(worstHeartbeat <= limit("TERMINAL_MAX_HEARTBEAT_MS", 200));
        QVERIFY(cpu <= limit("TERMINAL_MAX_CPU_PERCENT", 200));
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    VlcTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "vlc_backend_test.moc"
