// 最小视频探针：只创建一个 GstVideoItem，用于 Pi 上的管线/渲染性能对比。
// 用法：TERMINAL_TV_URL=<hls> ./tv_probe
#include "ui/GstVideoItem.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml>
#include <memory>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString url = qEnvironmentVariable("TERMINAL_TV_URL");
    if (url.isEmpty()) {
        qCritical("TERMINAL_TV_URL is required");
        return 2;
    }

    qmlRegisterType<GstVideoItem>("Terminal.Native", 1, 0, "GstVideoItem");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("testUrl", url);
    int probeWidth = 0, probeHeight = 0;
    const QString probeSize = qEnvironmentVariable("TERMINAL_PROBE_SIZE");
    if (const auto parts = probeSize.split(QLatin1Char('x')); parts.size() == 2) {
        probeWidth = parts[0].toInt();
        probeHeight = parts[1].toInt();
    }
    engine.rootContext()->setContextProperty("probeWidth", probeWidth);
    engine.rootContext()->setContextProperty("probeHeight", probeHeight);
    engine.rootContext()->setContextProperty("probeAnimate", qEnvironmentVariableIsSet("TERMINAL_PROBE_ANIMATE"));
    engine.loadData(R"(
        import QtQuick
        import QtQuick.Window
        import Terminal.Native 1.0
        Window {
            visible: true
            width: 1024
            height: 600
            color: "black"
            visibility: Window.FullScreen
            Rectangle {
                visible: probeAnimate
                width: 48; height: 48; radius: 8; color: "red"; z: 10
                NumberAnimation on rotation { running: probeAnimate; loops: Animation.Infinite; from: 0; to: 360; duration: 3000 }
            }
            GstVideoItem {
                width: probeWidth > 0 ? probeWidth : parent.width
                height: probeHeight > 0 ? probeHeight : parent.height
                anchors.centerIn: parent
                source: testUrl
                Component.onCompleted: play()
            }
        }
    )");
    if (engine.rootObjects().isEmpty())
        return 3;

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] { qInfo("PROBE_ABOUT_TO_QUIT"); });

    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        QObject::connect(window, &QQuickWindow::visibleChanged, window, [window] {
            qInfo("PROBE_WIN visible=%d exposed=%d", window->isVisible(), window->isExposed());
        });
        auto beat = new QTimer(window);
        QObject::connect(beat, &QTimer::timeout, window, [window] {
            qInfo("PROBE_HEARTBEAT visible=%d exposed=%d active=%d", window->isVisible(),
                  window->isExposed(), window->isActive());
        });
        beat->start(1000);
        auto clock = std::make_shared<QElapsedTimer>();
        auto frames = std::make_shared<int>(0);
        clock->start();
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [clock, frames] {
            if (++*frames % 30 != 0)
                return;
            qInfo("PROBE_SWAP fps=%.1f", *frames * 1000.0 / qMax<qint64>(1, clock->elapsed()));
            clock->restart();
            *frames = 0;
        });
    }

    bool ok = false;
    const int quitAfter = qEnvironmentVariableIntValue("TERMINAL_QUIT_AFTER", &ok);
    if (ok && quitAfter > 0) {
        qInfo("PROBE_QUIT_AFTER %dms", quitAfter * 1000);
        QTimer::singleShot(quitAfter * 1000, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
