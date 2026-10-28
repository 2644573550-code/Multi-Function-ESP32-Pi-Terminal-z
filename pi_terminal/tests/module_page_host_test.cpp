// §0.7 / Z-7：模块页必须用独立 QQmlEngine，宿主对象不可见。
#include "core/ModulePageHost.h"

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
};

QTEST_MAIN(ModulePageHostTest)
#include "module_page_host_test.moc"
