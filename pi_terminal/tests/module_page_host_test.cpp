// §0.7 / Z-7：模块页必须用独立 QQmlEngine，宿主对象不可见。
#include "core/ModulePageHost.h"

#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml>
#include <QtTest>
#include <QVector>

class ModulePageHostTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

private slots:
    void initTestCase() { QVERIFY(dir_.isValid()); }

    void hostObjectsAreNotVisibleInModuleEngine() {
        ModulePageHost host;
        // 独立引擎的根 context 不得含宿主注入项。
        for (const auto *name : {"connectionManager", "systemTools", "musicService",
                                 "desktopShell", "viperBrowser"}) {
            QVERIFY2(!host.hasHostObject(QLatin1String(name)), name);
        }
    }

    void modulePageCanReadUserContextButNotHostServices() {
        const QString path = dir_.filePath("Page.qml");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"(
import QtQuick
Item {
    property string uid: userContext.activeUserId
    property bool locked: userContext.locked
    property bool admin: userContext.isAdmin
}
)");
        file.close();

        ModulePageHost host;
        ModulePageHost::UserContext context;
        context.activeUserId = QStringLiteral("user-42");
        context.locked = true;
        context.isAdmin = false;
        host.updateUserContext(context);

        QObject *model = new QObject(&host);
        QVERIFY(host.load(QUrl::fromLocalFile(path), model, {}));
        QVERIFY(host.hasPage());
        QCOMPARE(host.page()->property("uid").toString(), QStringLiteral("user-42"));
        QCOMPARE(host.page()->property("locked").toBool(), true);
        QCOMPARE(host.page()->property("admin").toBool(), false);
    }

    void unloadDropsPage() {
        const QString path = dir_.filePath("Bare.qml");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("import QtQuick\nItem {}\n");
        file.close();

        ModulePageHost host;
        QObject *model = new QObject(&host);
        QVERIFY(host.load(QUrl::fromLocalFile(path), model, {}));
        QVERIFY(host.hasPage());
        host.unload();
        QVERIFY(!host.hasPage());
    }

    void brokenPageFailsClosed() {
        const QString path = dir_.filePath("Broken.qml");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("import QtQuick\nItem { this is not qml }\n");
        file.close();

        ModulePageHost host;
        QObject *model = new QObject(&host);
        // 加载失败必须返回 false 且不留半成品页面。
        QVERIFY(!host.load(QUrl::fromLocalFile(path), model, {}));
        QVERIFY(!host.hasPage());
    }

    void reloadDoesNotLeakPreviousContext() {
        const QString first = dir_.filePath("First.qml");
        const QString second = dir_.filePath("Second.qml");
        const QVector<QPair<QString, QByteArray>> pages{
            {first, "import QtQuick\nItem { property string tag: \"first\" }\n"},
            {second, "import QtQuick\nItem { property string tag: \"second\" }\n"}};
        for (const auto &pair : pages) {
            QFile file(pair.first);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(pair.second);
        }

        ModulePageHost host;
        QObject *model = new QObject(&host);
        QVERIFY(host.load(QUrl::fromLocalFile(first), model, {}));
        QCOMPARE(host.page()->property("tag").toString(), QStringLiteral("first"));
        // 换页后必须是新页，且旧页已销毁。
        QVERIFY(host.load(QUrl::fromLocalFile(second), model, {}));
        QCOMPARE(host.page()->property("tag").toString(), QStringLiteral("second"));
    }

    // 核心特色：模块自带的自定义 QML 页面必须能真正加载并挂进宿主容器。
    // 这不是"引擎隔离"能覆盖的 —— 隔离成立但页面不可见，等于功能没了。
    void customModulePageLoadsAndBecomesVisible()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // 模块包自带页面 + 同目录兄弟组件（隐式解析，依赖 baseUrl）。
        QFile sibling(dir.filePath("Tile.qml"));
        QVERIFY(sibling.open(QIODevice::WriteOnly));
        sibling.write("import QtQuick\nRectangle { property string label: \"\" ; "
                      "implicitHeight: 40; color: \"#123456\"; Text { text: parent.label } }\n");
        sibling.close();
        QFile page(dir.filePath("Page.qml"));
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write("import QtQuick\nRectangle { property var hostTheme: ({}) ; "
                   "implicitHeight: 120; width: 300\n"
                   "  Tile { label: \"from sibling\"; width: 300; height: 40 }\n}\n");
        page.close();

        QQuickWindow window;
        window.resize(1024, 600);
        auto *container = new QQuickItem(window.contentItem());
        container->setWidth(1024);
        container->setHeight(600);

        ModulePageHost host;
        host.setContainer(container);
        QVariantMap theme{{"background", "#08121f"}, {"scale", 1.0}};
        QObject model;
        QVERIFY2(host.load(QUrl::fromLocalFile(dir.filePath("Page.qml")), &model, theme),
                 qPrintable(host.lastError()));
        host.setContainer(container);

        QVERIFY(host.hasPage());
        auto *item = qobject_cast<QQuickItem *>(host.page());
        QVERIFY(item);
        // 页面必须真的挂进容器并占满尺寸，否则用户看到的是空白。
        QCOMPARE(item->parentItem(), container);
        QCOMPARE(item->width(), 1024.0);
        QCOMPARE(item->height(), 600.0);
        QVERIFY(host.lastError().isEmpty());
    }

    // 页面加载失败必须留下可读错误，不能静默空白。
    void brokenModulePageReportsError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile page(dir.filePath("Broken.qml"));
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write("import QtQuick\nRectangle { this is not valid qml\n");
        page.close();

        ModulePageHost host;
        QObject model;
        QVERIFY(!host.load(QUrl::fromLocalFile(dir.filePath("Broken.qml")), &model, {}));
        QVERIFY(!host.hasPage());
        QVERIFY2(!host.lastError().isEmpty(), "failed load must surface a reason");
    }


    // 热更新路径的 QML 侧契约：页面重建后仍是新对象，旧对象不得复用。
    void reloadReplacesPageObject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile page(dir.filePath("Page.qml"));
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write("import QtQuick\nRectangle { property var hostTheme: ({}) ; implicitHeight: 60; width: 200 }\n");
        page.close();

        ModulePageHost host;
        QObject model;
        QVERIFY(host.load(QUrl::fromLocalFile(dir.filePath("Page.qml")), &model, {}));
        QObject *first = host.page();
        QVERIFY(first);
        // 在旧页面上打标记；重新加载后这个标记必须消失。
        // 不能比较裸指针：delete 后新对象可能复用同一地址。
        first->setProperty("reloadProbe", QStringLiteral("stale"));
        QCOMPARE(first->property("reloadProbe").toString(), QStringLiteral("stale"));

        QVERIFY(host.load(QUrl::fromLocalFile(dir.filePath("Page.qml")), &model, {}));
        QObject *second = host.page();
        QVERIFY(second);
        QVERIFY2(second->property("reloadProbe").toString().isEmpty(),
                 "reload must not reuse the previous page's state");
    }

};

QTEST_MAIN(ModulePageHostTest)
#include "module_page_host_test.moc"
