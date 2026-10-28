#include <QApplication>
#include <QByteArray>
#include <QCommandLineParser>
#include <QIcon>
#include <QFontDatabase>
#include <QFont>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "app/TerminalOptions.h"
#include "app/AdminInitializer.h"
#include "app/TerminalRuntime.h"
#include "ui/TerminalUi.h"
#include "SchemeRegistry.h"

int main(int argc, char **argv) {
    QByteArray chromiumFlags=qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if(!chromiumFlags.contains("--touch-events=")) {
        if(!chromiumFlags.isEmpty()) chromiumFlags.append(' ');
        chromiumFlags.append("--touch-events=enabled");
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS",chromiumFlags);
    }
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickStyle::setStyle("Material");
    SchemeRegistry::registerSchemes();
    QtWebEngineQuick::initialize();

    QApplication application(argc, argv);
    initializeTerminalUi();
    const int googleSansId=QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/GoogleSans.ttf"));
    if(googleSansId>=0) {
        const auto families=QFontDatabase::applicationFontFamilies(googleSansId);
        if(!families.isEmpty()) {
            QFont appFont(families.first());
            appFont.setPointSize(18);
            application.setFont(appFont);
            qInfo().noquote() << "UI_FONT_FAMILY:" << families.first();
        }
    } else qWarning() << "Google Sans resource unavailable; using system fallback font";
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/orange_pi_terminal.svg")));
    application.setOrganizationName("ModularTerminal");
    application.setApplicationName("OrangePiTerminal");

    QCommandLineParser options;
    TerminalOptions::configure(options);
    options.process(application);

    // 首次管理员初始化在起 UI 之前完成：无用户时可运行，之后永久拒绝。
    const int initResult = AdminInitializer::run(options);
    if (initResult >= 0) return initResult;

    TerminalRuntime runtime(application, options);
    return runtime.run();
}
