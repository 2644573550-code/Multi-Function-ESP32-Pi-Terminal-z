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
    // 安全服务门面：QML 只能通过它做信任/绑卡/授权/审计/导出。
    securityService_.setSessionManager(&session_);
    engine_.rootContext()->setContextProperty("securityService", &securityService_);
    // §9.3 联网总开关：默认关闭，QML 只读写状态，实际放行由 C++ 判定。
    engine_.rootContext()->setContextProperty("networkGate", &networkGate_);
    // §13 供电状态：P0 无数据源，保持 unknown，界面据此显示"状态未知"。
    engine_.rootContext()->setContextProperty("powerStatus", &powerStatus_);
    engine_.rootContext()->setContextProperty("systemTools", &systemTools_);
    engine_.rootContext()->setContextProperty("musicService", &musicService_);
    engine_.rootContext()->setContextProperty("viperBrowser", &browserController_);
    engine_.rootContext()->setContextProperty("desktopShell", &desktopShell_);
    engine_.rootContext()->setContextProperty("desktopDebug", options.isSet("demo"));
    // 登录/锁屏：QML 只调这些方法，服务端再做会话与权限校验。
    engine_.rootContext()->setContextProperty("sessionManager", &session_);
    // §0.8.5：业务数据必须挂在当前登录用户上。会话变化时同步给连接管理器，
    // 否则 lane 门控拿不到用户，所有业务数据都会被拒。
    QObject::connect(&session_, &security::SessionManager::changed, &connection_, [this] {
        connection_.setActiveUserId(session_.activeUserId());
    // 报告 §6.1：模块观测写进当前用户的加密库，模块侧不再写明文 JSON。
    connection_.observationSink = [this](const QString &, double value) {
        return session_.recordObservation(QStringLiteral("module"), value);
    };
    });
    connection_.setActiveUserId(session_.activeUserId());
    // 自动化审计（--ui-test）以无用户配置启动，门控会拒绝业务数据；
    // 仅测试路径注入一个占位用户，让审计能走完既有业务断言。
    if (options.isSet("ui-test"))
        connection_.setActiveUserId(QStringLiteral("ui-test-user"));
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
