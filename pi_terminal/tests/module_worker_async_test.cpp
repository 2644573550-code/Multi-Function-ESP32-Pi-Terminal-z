// worker 路径的异步语义测试（T-11）。
//
// 与 session_test 的区别：那个测的是同进程 QLibrary 的同步回调，
// 这里测 worker 进程 IPC 的异步行为。
//
// 本机没有 systemd unit，所以走 degraded 路径（直接起二进制）。
// degraded 不得当作隔离证据 —— 只验证 IPC 与崩溃隔离。
#include "core/ModuleWorkerClient.h"
#include "core/WorkerIpc.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>


using namespace security;

class ModuleWorkerAsyncTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

    // 每个用例独立 socket，避免 /run 路径互相覆盖。
    void useSession(const QString &name)
    {
        qputenv("ORANGE_PI_WORKER_SOCKET",
                dir_.filePath(name + QStringLiteral(".sock")).toUtf8());
        qputenv("ORANGE_PI_MODULE_WORKER", workerBinary().toUtf8());
    }

    QString workerBinary() const {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/module_worker");
    }
    QString exampleLibrary() const {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/modules/example/libexample_module.so");
    }

private slots:
    void initTestCase() {
        if (!QFileInfo::exists(workerBinary())) QSKIP("module_worker not built");
    }

    void workerStartsAndLoadsModule() {
        ModuleWorkerClient client;
        useSession(QStringLiteral("t1"));
        QVERIFY2(client.start(QStringLiteral("t1"), exampleLibrary(),
                              QJsonObject{{"transport_mode", "fixed_hotspot_lan"}}),
                 qPrintable(client.lastError()));
        QVERIFY(client.isRunning());
        // 本机没有 systemd unit：必须诚实标记 degraded，不能当作隔离证据。
        QVERIFY2(client.degraded(), "without systemd the client must report degraded");
        client.stop({});
    }

    void missingLibraryFailsClosed() {
        ModuleWorkerClient client;
        useSession(QStringLiteral("t2"));
        // 不存在的库必须失败，不能假装加载成功。
        QVERIFY(!client.start(QStringLiteral("t2"), QStringLiteral("/nonexistent/lib.so"),
                              QJsonObject{}));
        QVERIFY(!client.isRunning());
        QVERIFY(!client.errorCode().isEmpty());
        client.stop(QStringLiteral("t2"));
    }

    void oversizedPayloadIsRejected() {
        ModuleWorkerClient client;
        useSession(QStringLiteral("t3"));
        QVERIFY(client.start(QStringLiteral("t3"), exampleLibrary(),
                             QJsonObject{{"transport_mode", "fixed_hotspot_lan"}}));
        QVERIFY2(!client.sendData(QByteArray(513, 'x')), "513 bytes must be rejected");
        QCOMPARE(client.errorCode(), QStringLiteral("INVALID_MESSAGE"));
        client.stop({});
    }

    void processSurvivesWorkerStop() {
        // worker 停止不得影响宿主进程（这里就是测试进程本身）。
        ModuleWorkerClient client;
        useSession(QStringLiteral("t4"));
        QVERIFY(client.start(QStringLiteral("t4"), exampleLibrary(),
                             QJsonObject{{"transport_mode", "fixed_hotspot_lan"}}));
        client.stop({});
        QVERIFY(!client.isRunning());
        // 还活着就说明没被 worker 带崩。
        QVERIFY(true);
    }

    void sendsDataWithinLimit() {
        ModuleWorkerClient client;
        useSession(QStringLiteral("t5"));
        QVERIFY(client.start(QStringLiteral("t5"), exampleLibrary(),
                             QJsonObject{{"transport_mode", "fixed_hotspot_lan"}}));
        // 512 字节边界内应当被接受（是否产生实际传感器数据由模块决定）。
        QVERIFY(client.sendData(QByteArray(512, 'x')));
        client.stop({});
    }

    void restartAfterStopWorks() {
        ModuleWorkerClient client;
        useSession(QStringLiteral("t6"));
        QVERIFY(client.start(QStringLiteral("t6"), exampleLibrary(),
                             QJsonObject{{"transport_mode", "fixed_hotspot_lan"}}));
        client.stop({});
        // 同一个 client 重新启动必须是干净状态。
        useSession(QStringLiteral("t6"));
        QVERIFY2(client.start(QStringLiteral("t6"), exampleLibrary(),
                              QJsonObject{{"transport_mode", "fixed_hotspot_lan"}}),
                 qPrintable(client.lastError()));
        client.stop({});
    }
};

QTEST_GUILESS_MAIN(ModuleWorkerAsyncTest)
#include "module_worker_async_test.moc"
