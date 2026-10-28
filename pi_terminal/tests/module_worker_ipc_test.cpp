// T-11：宿主 ↔ worker 的真实 IPC 端到端测试。
//
// 这里不 mock IPC：真的连 Unix socket、真的发 p0-worker-v1 帧、
// 真的加载 example_module.so 并让模块回业务数据。
// 用 degraded 模式（直接起 worker 二进制）跑，因为测试机不必装 systemd unit；
// 隔离声明本身由 worker_isolation_test 静态校验。
#include "core/ModuleWorkerClient.h"
#include "core/WorkerIpc.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class ModuleWorkerIpcTest : public QObject {
    Q_OBJECT

private:
    QString workerBinary() const
    {
        // 优先用构建树里的 worker（ctest 的工作目录就是 build 根）。
        const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/module_worker");
        if (QFile::exists(local)) return local;
        return QStringLiteral("/usr/libexec/orange-pi-terminal/module_worker");
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(QFile::exists(workerBinary()), qPrintable(workerBinary()));
    }

    // socket 路径与 unit 名必须由同一套规则推导，否则宿主永远连不上。
    void socketPathAndUnitNameAgree()
    {
        QCOMPARE(WorkerIpc::unitName(QStringLiteral("s1")),
                 QStringLiteral("orange-pi-worker@s1.service"));
        QCOMPARE(WorkerIpc::socketPath(QStringLiteral("s1")),
                 QStringLiteral("/run/orange-pi-worker-s1/ipc.sock"));
        // 非法字符被过滤，两处保持一致。
        QCOMPARE(WorkerIpc::safeToken(QStringLiteral("a/b..c")), QStringLiteral("abc"));
        QVERIFY(WorkerIpc::socketPath(QStringLiteral("../evil")).contains(QStringLiteral("..")) == false);
    }

    // 真实 IPC：起 worker、加载 example 模块、发业务数据、拿回 OK。
    void workerLoadsModuleAndAcceptsBusinessData()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString socketPath = dir.filePath(QStringLiteral("ipc.sock"));

        // example 模块的 .so 在构建树里（modules/example/），源码树只有 qml。
        const QString moduleDir = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("modules/example"));
        QVERIFY2(QDir(moduleDir).exists(), qPrintable(moduleDir));
        const QString libraryPath = moduleDir + QStringLiteral("/libexample_module.so");
        QVERIFY2(QFile::exists(libraryPath), qPrintable(libraryPath));

        // 宿主与 worker 共用同一 socket 路径：显式覆盖到临时目录。
        qputenv("ORANGE_PI_WORKER_SOCKET", socketPath.toUtf8());
        qputenv("ORANGE_PI_MODULE_WORKER", workerBinary().toUtf8());

        security::ModuleWorkerClient client;

        const QJsonObject transport{{"transport_mode", "fixed_hotspot_lan"},
                                    {"token", "test-token"}};
        const bool started = client.start(QStringLiteral("ipc-test"), libraryPath, transport);
        QVERIFY2(started, qPrintable(client.errorCode() + QLatin1Char(' ') + client.lastError()));
        QVERIFY(client.isRunning());
        QVERIFY2(client.degraded(), "tests must not require an installed systemd unit");

        // 业务数据：example 模块的 on_data 接受任意 payload 并回报。
        QVERIFY2(client.sendData(QByteArrayLiteral("{\"probe\":1}")),
                 qPrintable(client.errorCode()));

        // 超 512 字节必须在客户端就拒绝，不进 IPC。
        QVERIFY(!client.sendData(QByteArray(513, 'x')));
        QCOMPARE(client.errorCode(), QStringLiteral("INVALID_MESSAGE"));

        client.stop(QStringLiteral("ipc-test"));
        QVERIFY(!client.isRunning());
    }

    // 模块库缺失时 start() 必须返回 false（而不是"连上了就算成功"），
    // 否则宿主会在没有任何可用模块的情况下宣告 ready。
    void missingLibraryFailsClosed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("ORANGE_PI_WORKER_SOCKET", dir.filePath(QStringLiteral("ipc.sock")).toUtf8());
        qputenv("ORANGE_PI_MODULE_WORKER", workerBinary().toUtf8());

        security::ModuleWorkerClient client;
        QVERIFY2(!client.start(QStringLiteral("ipc-missing"),
                               dir.filePath(QStringLiteral("libmissing.so")), {}),
                 "start must fail when the module library cannot be loaded");
        QVERIFY(!client.errorCode().isEmpty());
        client.stop(QStringLiteral("ipc-missing"));
    }
};

QTEST_GUILESS_MAIN(ModuleWorkerIpcTest)
#include "module_worker_ipc_test.moc"
