#pragma once

#include <QQmlApplicationEngine>

#include "core/ConnectionManager.h"
#include "core/SecurityStatus.h"
#include "ui/DesktopShell.h"
#include "ui/SystemTools.h"
#include "ui/MusicService.h"
#include "ui/ViperBrowserController.h"

class QCommandLineParser;

class TerminalApplication final {
public:
    int initialize(const QCommandLineParser &options);

    ConnectionManager &connection() { return connection_; }
    SystemTools &systemTools() { return systemTools_; }
    DesktopShell &desktopShell() { return desktopShell_; }
    QQmlApplicationEngine &engine() { return engine_; }

private:
    // Reverse destruction order: QML pages/engine, shell, connection sessions
    // (which release hotspot leases), then SystemTools and its helper processes.
    SystemTools systemTools_;
    ConnectionManager connection_;
    SecurityStatus securityStatus_{&connection_};
    DesktopShell desktopShell_;
    MusicService musicService_;
    ViperBrowserController browserController_;
    QQmlApplicationEngine engine_;
};
