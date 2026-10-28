// 安全状态页的独立预览程序。
//
// 为什么单独一个可执行文件：T-09 的登录/绑卡页面属 Mr.Z，D 不碰
// Main.qml / AppLauncher。要"能直接看到效果"又不侵入别人的文件，
// 最干净的做法就是自己起一个只加载 SecurityStatusPage.qml 的窗口。
// 它只链接 terminal_core + QtQuick，不依赖 terminal_ui，因此不需要 VLC SDK。

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QUrl>

#include "core/ConnectionManager.h"
#include "core/SecurityStatus.h"

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    ConnectionManager connection;      // 不 configure：只读槽位快照用
    SecurityStatus status(&connection);

    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("securityStatus"), &status);
    view.setColor(Qt::black);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(QStringLiteral(PREVIEW_PAGE)));
    if (view.status() == QQuickView::Error) return 1;
    view.resize(720, 1280);
    view.show();
    return app.exec();
}
