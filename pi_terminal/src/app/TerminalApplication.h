#pragma once

#include <QQmlApplicationEngine>

#include "core/ConnectionManager.h"
#include "core/ModulePageHost.h"
#include "security/AuthService.h"
#include "security/SessionManager.h"
#include "security/UserService.h"
#include "ui/DesktopShell.h"
#include "ui/MusicService.h"
#include "ui/SystemTools.h"
#include "ui/ViperBrowserController.h"

class QCommandLineParser;

class TerminalApplication final {
public:
    int initialize(const QCommandLineParser &options);

    ConnectionManager &connection() { return connection_; }
    SystemTools &systemTools() { return systemTools_; }
    DesktopShell &desktopShell() { return desktopShell_; }
    security::SessionManager &session() { return session_; }
    ModulePageHost &modulePageHost() { return modulePageHost_; }
    QQmlApplicationEngine &engine() { return engine_; }

private:
    // Reverse destruction order: QML pages/engine, shell, connection sessions
    // (which release hotspot leases), then SystemTools and its helper processes.
    SystemTools systemTools_;
    ConnectionManager connection_;
    DesktopShell desktopShell_;
    MusicService musicService_;
    ViperBrowserController browserController_;

    // 安全层：注册表与认证是 SessionManager 的依赖，必须先于它构造。
    // 放在 security 命名空间里：Viper-Browser 有一个同名的全局 SessionManager，
    // 不隔离会 ODR 冲突并导致析构时 SIGSEGV（已实测）。
    security::UserService userService_;
    security::AuthService authService_{userService_};
    security::SessionManager session_{&userService_, &authService_};

    // 模块页沙箱（§0.7）：独立 QQmlEngine，宿主对象不可见。
    ModulePageHost modulePageHost_;

    QQmlApplicationEngine engine_;
};
