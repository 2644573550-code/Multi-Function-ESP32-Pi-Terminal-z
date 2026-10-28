#include "TerminalApplication.h"

#include <QCommandLineParser>
#include <QQmlContext>
#include <QSysInfo>
#include <QTimer>

int TerminalApplication::initialize(const QCommandLineParser &options) {
    if (options.isSet("paint-test")) {
        QObject::connect(&connection_, &ConnectionManager::trustRequired, &connection_,
            [this](const QString &instance, const QString &, const QStringList &) {
                connection_.trust(instance, false);
            });
    }
    QObject::connect(&connection_, &ConnectionManager::announcement,
                     &systemTools_, &SystemTools::speak);
    QObject::connect(&connection_, &ConnectionManager::announcement,
                     &systemTools_, &SystemTools::log);
    if (options.isSet("demo") || options.isSet("ui-test")) {
        // The desktop integration fixture uses loopback TCP, not a real AP.
        // Inject its virtual hotspot readiness instead of changing NetworkManager.
        QObject::connect(&connection_, &ConnectionManager::hotspotLeaseRequested,
                         &connection_, [this](const QString &instance) {
            QTimer::singleShot(0, &connection_, [this, instance] {
                connection_.hotspotLeaseReady(instance, true);
            });
        });
    } else {
        QObject::connect(&connection_, &ConnectionManager::hotspotLeaseRequested,
                         &systemTools_, &SystemTools::acquireHotspotLease);
    }
    QObject::connect(&systemTools_, &SystemTools::hotspotLeaseReady,
                     &connection_, &ConnectionManager::hotspotLeaseReady);
    if (!options.isSet("demo"))
        QObject::connect(&connection_, &ConnectionManager::hotspotLeaseReleased,
                         &systemTools_, &SystemTools::releaseHotspotLease);

    if (options.isSet("config") && !connection_.configure(options.value("config"))) {
        qCritical().noquote() << connection_.status();
        return 2;
    }
    const QString device = qEnvironmentVariable("ORANGE_PI_CONTROL_DEVICE");
    if (!device.isEmpty())
        connection_.start(device);

    engine_.rootContext()->setContextProperty("connectionManager", &connection_);
    engine_.rootContext()->setContextProperty("securityStatus", &securityStatus_);
    engine_.rootContext()->setContextProperty("systemTools", &systemTools_);
    engine_.rootContext()->setContextProperty("musicService", &musicService_);
    engine_.rootContext()->setContextProperty("viperBrowser", &browserController_);
    engine_.rootContext()->setContextProperty("desktopShell", &desktopShell_);
    engine_.rootContext()->setContextProperty("desktopDebug", options.isSet("demo"));
    // 登录/锁屏：QML 只调这些方法，服务端再做会话与权限校验。
    engine_.rootContext()->setContextProperty("sessionManager", &session_);
    // 自动化审计（--ui-test）以全新配置启动、从不登录；门禁若照常生效，
    // 审计脚本点不到桌面会被误判为产品回归。仅测试路径关闭门禁。
    engine_.rootContext()->setContextProperty("sessionGateBypassed", options.isSet("ui-test"));
    // §0.7：模块页用独立引擎，宿主对象不在它的 context 里。
    engine_.rootContext()->setContextProperty("modulePageHost", &modulePageHost_);
    // QML binds directly to the persisted setting; never freeze it at startup.
    engine_.rootContext()->setContextProperty("efficientRendering", systemTools().energySaving());
    engine_.load(QUrl("qrc:/qt/qml/OrangePiTerminal/Main.qml"));
    return engine_.rootObjects().isEmpty() ? 1 : 0;
}
