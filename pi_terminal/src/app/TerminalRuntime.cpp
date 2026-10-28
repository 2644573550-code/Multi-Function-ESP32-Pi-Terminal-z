#include "TerminalRuntime.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSysInfo>
#include <QTimer>
#include <QVariantMap>
#include <algorithm>
#include <memory>

#include "DiagnosticRunner.h"
#include "TerminalApplication.h"
#ifdef TERMINAL_GUI_AUDIT
#include "GuiAudit.h"
#endif

TerminalRuntime::TerminalRuntime(QApplication &application, const QCommandLineParser &options)
    : application_(application), options_(options) {}

int TerminalRuntime::run() {
    TerminalApplication terminalApplication;
    const int initializeResult = terminalApplication.initialize(options_);
    if (initializeResult != 0) return initializeResult;

    auto &connection = terminalApplication.connection();
    auto &tools = terminalApplication.systemTools();
    auto &desktopShell = terminalApplication.desktopShell();
    auto &engine = terminalApplication.engine();
    const bool controlDeviceConfigured = !qEnvironmentVariable("ORANGE_PI_CONTROL_DEVICE").isEmpty();
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    desktopShell.setWindow(window);
    DiagnosticRunner::schedule(options_, engine.rootObjects().first(), window, tools);

    if (window && options_.isSet("window-size")) {
        const auto parts = options_.value("window-size").split('x');
        if (parts.size() != 2 || parts[0].toInt() < 320 || parts[1].toInt() < 320) {
            qCritical("Invalid window size (minimum 320x320 logical pixels)");
            return 2;
        }
        window->resize(parts[0].toInt(), parts[1].toInt());
    }
    if (window && !options_.isSet("window-size") && !options_.isSet("ui-audit") &&
        !options_.isSet("ui-test"))
        window->showFullScreen();

    struct Metrics {
        QElapsedTimer clock;
        QList<double> gaps;
        qint64 last = 0;
        bool measuring = true;
    };
    auto metrics = std::make_shared<Metrics>();
    metrics->clock.start();
    if (options_.isSet("render-benchmark")) {
        metrics->measuring = false;
        QTimer::singleShot(5000, &application_, [metrics] {
            metrics->gaps.clear();
            metrics->last = 0;
            metrics->clock.restart();
            metrics->measuring = true;
        });
    }
    if (window) {
        QObject::connect(window, &QQuickWindow::frameSwapped, &application_, [metrics] {
            if (!metrics->measuring) return;
            const auto ns = metrics->clock.nsecsElapsed();
            if (metrics->last && metrics->gaps.size() < 100000)
                metrics->gaps.append((ns - metrics->last) / 1e6);
            metrics->last = ns;
        });
        QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window, [] {
            if (auto *context = QOpenGLContext::currentContext()) {
                auto *gl = context->functions();
                qInfo("GL_RENDERER: %s; GL_VERSION: %s", gl->glGetString(GL_RENDERER),
                      gl->glGetString(GL_VERSION));
            }
        }, Qt::DirectConnection);
    }

    bool uiTestPassed = false;
    if (options_.isSet("ui-audit")) {
#ifdef TERMINAL_GUI_AUDIT
        QTimer::singleShot(700, &application_, [this, window] {
            application_.exit(runGuiAudit(window, options_.value("ui-audit")));
        });
#else
        qCritical("Rebuild with BUILD_TESTING=ON for --ui-audit");
        return 2;
#endif
    }
    if (options_.isSet("demo") && !controlDeviceConfigured && !options_.isSet("paint-test") &&
        !options_.isSet("notification-layout-test") && !options_.isSet("notification-banner-test") &&
        !options_.isSet("app-drawer-test")) {
        QTimer::singleShot(200, &connection, [&connection] {
            connection.attach({{"event", "attach"}, {"module_id", 80},
                               {"module_instance", "desktop-demo"},
                               {"module_config", QJsonObject{{"schema", "module-transport-v1"},
                                   {"transport_mode", "fixed_hotspot_lan"}, {"package_transport", "data"}}}});
        });
    }
    if (options_.isSet("open-module")) {
        const QString instance = options_.value("open-module");
        auto *openTimer = new QTimer(&application_);
        openTimer->setInterval(250);
        QObject::connect(openTimer, &QTimer::timeout, &application_,
            [this, openTimer, instance, &connection, &engine] {
                for (const auto &row : connection.cards()) {
                    const auto card = row.toMap();
                    if (card.value("instance").toString() != instance ||
                        !card.value("ready").toBool())
                        continue;
                    // 关键：openModule 在 phase!=="home" 时会提前返回，
                    // 因此必须等主页就绪后再调用，并检查它是否真的打开。
                    auto *root = engine.rootObjects().first();
                    if (root->property("phase").toString() != QLatin1String("home"))
                        continue;
                    QMetaObject::invokeMethod(root, "openModule",
                                              Q_ARG(QVariant, QVariant(instance)));
                    // openModule 成功后 activeSession 立即置位；phase 还要等动画。
                    if (root->property("activeSession").toString() == instance) {
                        qInfo().noquote() << "Foreground module opened:" << instance;
                        openTimer->stop();
                        if (options_.isSet("invoke-action")) {
                            const QString action = options_.value("invoke-action");
                            QTimer::singleShot(2500, &application_, [&connection, instance, action] {
                                qInfo().noquote() << "Invoking module action:" << instance << action;
                                connection.action(instance, action);
                            });
                        }
                    }
                    return;
                }
            });
        openTimer->start();
    }
    if (options_.isSet("ui-test")) {
        auto *timer = new QTimer(&application_);
        timer->setInterval(100);
        auto stage = std::make_shared<int>(0);
        auto lastReportedStage = std::make_shared<int>(-1);
        QObject::connect(timer, &QTimer::timeout, &application_,
            [this, timer, stage, lastReportedStage, &connection, &engine, window, controlDeviceConfigured,
              &uiTestPassed] {
                // The launcher is intentionally input-blocked until startup
                // completes. Exercise the real ready UI, not a lost early tap.
                if (!window->property("startupComplete").toBool()) return;
                const auto cards = connection.cards();
                if (*lastReportedStage != *stage) {
                    *lastReportedStage = *stage;
                    const auto card = cards.isEmpty() ? QVariantMap{} : cards.first().toMap();
                    qInfo().noquote() << "UI_TEST_STAGE" << *stage
                        << "state=" << card.value("state").toString()
                        << "ready=" << card.value("ready").toBool()
                        << "message=" << card.value("message").toString()
                        << "status=" << connection.status()
                        << "phase=" << window->property("phase").toString()
                        << "permissionPending=" << window->property("permissionPending").toBool();
                }
                if (*stage == 0 && !cards.isEmpty() &&
                    cards.first().toMap().value("state").toString() == "awaiting_trust")
                    QMetaObject::invokeMethod(engine.rootObjects().first(), "approveTrustOnce");
                if (*stage == 0 && !cards.isEmpty() &&
                    cards.first().toMap().value("ready").toBool() &&
                    !window->property("permissionPending").toBool()) {
                    if (controlDeviceConfigured && !window->property("bridgeConnectedPromptShown").toBool()) {
                        qCritical("USB control bridge connection prompt was not shown");
                        application_.exit(3);
                        return;
                    }
                    *stage = 1;
#ifdef TERMINAL_GUI_AUDIT
                    if (!touchLauncherEntry(window, "desktop-demo")) {
                        qCritical("Home module icon missing");
                        application_.exit(3);
                    }
#else
                    QMetaObject::invokeMethod(engine.rootObjects().first(), "openModule",
                                              Q_ARG(QVariant, QVariant("desktop-demo")));
#endif
                } else if (*stage == 1 && window->property("phase").toString() == "open") {
                    if (window->property("section").toInt() != 5) {
                        qCritical("Module feature opened inside connection management");
                        application_.exit(3);
                        return;
                    }
                    QMetaObject::invokeMethod(window, "toggleMaximize");
                    if (!window->property("appMaximized").toBool() ||
                        !window->property("modulePageLoaded").toBool()) {
                        qCritical("Module page disappeared while maximizing");
                        application_.exit(3);
                        return;
                    }
                    QMetaObject::invokeMethod(window, "toggleMaximize");
                    QMetaObject::invokeMethod(window, "minimizeApp");
                    *stage = 10;
                } else if (*stage == 10 && window->property("phase").toString() == "home" &&
                           window->property("appMinimized").toBool()) {
                    QMetaObject::invokeMethod(window, "restoreApp");
                    *stage = 11;
                } else if (*stage == 11 && window->property("phase").toString() == "open") {
                    if (window->property("section").toInt() != 5 ||
                        window->property("activeSession").toString() != "desktop-demo" ||
                        !window->property("modulePageLoaded").toBool()) {
                        qCritical("Module page was blank after minimize/restore");
                        application_.exit(3);
                        return;
                    }
                    connection.action("desktop-demo", "start");
                    *stage = 2;
                } else if (*stage == 2 && !cards.isEmpty() &&
                           cards.first().toMap().value("message").toString().contains("42")) {
                    *stage = 3;
                    QTimer::singleShot(350, &application_, [this, window, &engine] {
                        if (options_.isSet("screenshot") && window)
                            window->grabWindow().save(options_.value("screenshot"));
                        QMetaObject::invokeMethod(window, "closeApp");
                    });
                } else if (*stage == 3 && window->property("phase").toString() == "home") {
                    if (cards.isEmpty() || !cards.first().toMap().value("ready").toBool()) {
                        qCritical("Closing the app unexpectedly disconnected the module");
                        application_.exit(3);
                        return;
                    }
                    *stage = 30;
                    QTimer::singleShot(800, &application_, [this, stage, window] {
                        if (*stage != 30) return;
                        if (options_.isSet("screenshot")) {
                            const QFileInfo image(options_.value("screenshot"));
                            window->grabWindow().save(image.dir().filePath(
                                image.completeBaseName() + "-home.png"));
                        }
                        *stage = 4;
#ifdef TERMINAL_GUI_AUDIT
                        if (!touchLauncherEntry(window, "desktop-demo")) {
                            qCritical("Home module icon missing after app close");
                            application_.exit(3);
                        }
#else
                        QMetaObject::invokeMethod(window, "openModule",
                                                  Q_ARG(QVariant, QVariant("desktop-demo")));
#endif
                    });
                } else if (*stage == 4 && window->property("phase").toString() == "open") {
                    connection.disconnectModule("desktop-demo");
                    *stage = 5;
                } else if (*stage == 5 && cards.isEmpty()) {
                    if (window->property("phase").toString() != "home" ||
                        !window->property("activeSession").toString().isEmpty()) {
                        qCritical("Plugin unloaded before close animation completed");
                        application_.exit(3);
                        return;
                    }
                    QMetaObject::invokeMethod(engine.rootObjects().first(), "beginBrowserTest");
                    *stage = 6;
                } else if (*stage == 6 &&
                           engine.rootObjects().first()->property("browserTestPassed").toBool()) {
                    qInfo("UI_TEST_PASS: home module icon tapped, feature opened directly, zoom close/reopen preserved session, animated disconnect released page before unload, browser verified");
                    timer->stop();
                    *stage = 7;
                    uiTestPassed = true;
                }
            });
        timer->start();
    }

    if (options_.isSet("quit-after"))
        QTimer::singleShot(options_.value("quit-after").toInt(), &application_, [this, &engine] {
            if (options_.isSet("screenshot") && !options_.isSet("ui-test")) {
                auto *quickWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
                if (quickWindow) quickWindow->grabWindow().save(options_.value("screenshot"));
            }
            application_.quit();
        });

    const int result = application_.exec();
    if (options_.isSet("metrics")) {
        auto gaps = metrics->gaps;
        std::sort(gaps.begin(), gaps.end());
        QFile status("/proc/self/status");
        QString rss;
        if (status.open(QIODevice::ReadOnly))
            for (const auto &line : status.readAll().split('\n'))
                if (line.startsWith("VmRSS:")) rss = QString::fromLatin1(line).simplified();
        const double seconds = metrics->clock.elapsed() / 1000.0;
        QQuickWindow *quickWindow = window;
        QJsonObject report{{"qt_version", qVersion()}, {"architecture", QSysInfo::currentCpuArchitecture()},
            {"elapsed_seconds", seconds}, {"frame_intervals", gaps.size()},
            {"render_profile", engine.rootContext()->contextProperty("efficientRendering").toBool()
                                   ? "efficient" : "quality"},
            {"continuous_animation", options_.isSet("render-benchmark")},
            {"width", quickWindow ? quickWindow->width() : 0},
            {"height", quickWindow ? quickWindow->height() : 0},
            {"median_frame_interval_ms", gaps.isEmpty() ? 0 : gaps[gaps.size() / 2]},
            {"frames_over_50_ms", int(std::count_if(gaps.begin(), gaps.end(),
                                                     [](double gap) { return gap > 50; }))},
            {"average_frame_events_per_second", seconds > 0 ? (gaps.size() + 1) / seconds : 0},
            {"p95_frame_interval_ms", gaps.isEmpty() ? 0 : gaps[int((gaps.size() - 1) * 0.95)]},
            {"memory", rss}};
        QFile output(options_.value("metrics"));
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(QJsonDocument(report).toJson()) < 0) {
            qCritical("Cannot write metrics");
            return 4;
        }
    }
    return options_.isSet("ui-test") && !uiTestPassed ? 3 : result;
}
