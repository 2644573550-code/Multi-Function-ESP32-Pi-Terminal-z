// 模块 worker 入口（T-11）。
//
// 在独立进程里加载模块 .so，通过 stdin/stdout 与宿主交换 p0-worker-v1 消息。
// 宿主不直接 dlopen 模块；宿主侧的 ModuleManager 改为经 ModuleWorkerClient 调用本程序。
//
// 沙箱由 systemd unit 提供（DynamicUser/PrivateNetwork/...），本程序本身不是沙箱。
//
// 协议：u32le 长度 + UTF-8 JSON，见 core/WorkerIpc.h。
#include "core/WorkerIpc.h"
#include "modules/api/pi_module_api.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QTextStream>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>

#include <cstdio>

namespace {

class Worker {
public:
    ~Worker() { shutdown(); }

    bool load(const QString &path) {
        library_.setFileName(path);
        library_.setLoadHints(QLibrary::ResolveAllSymbolsHint);
        if (!library_.load()) return false;

        const auto required = {"pi_module_api_version", "pi_module_manifest_json",
                               "pi_module_create", "pi_module_destroy", "pi_module_on_attach",
                               "pi_module_on_detach", "pi_module_stop", "pi_module_on_data"};
        for (const auto *name : required) {
            if (!library_.resolve(name)) return false;
        }
        create_ = reinterpret_cast<decltype(&pi_module_create)>(library_.resolve("pi_module_create"));
        destroy_ = reinterpret_cast<decltype(&pi_module_destroy)>(library_.resolve("pi_module_destroy"));
        onAttach_ = reinterpret_cast<decltype(&pi_module_on_attach)>(library_.resolve("pi_module_on_attach"));
        onData_ = reinterpret_cast<decltype(&pi_module_on_data)>(library_.resolve("pi_module_on_data"));
        onDetach_ = reinterpret_cast<decltype(&pi_module_on_detach)>(library_.resolve("pi_module_on_detach"));
        stop_ = reinterpret_cast<decltype(&pi_module_stop)>(library_.resolve("pi_module_stop"));
        // 可选入口：没有导出 action 的模块不报错，只是 action 无效。
        onAction_ = reinterpret_cast<decltype(onAction_)>(library_.resolve("pi_module_action"));
        return true;
    }

    bool attach(const QJsonObject &transport) {
        if (!create_ || handle_) return false;
        // services 必须是成员：模块在 create 时保存这个指针，
        // 用栈上局部变量会在 attach 返回后变悬空（实测 SIGSEGV）。
        services_.api_version = 1;
        services_.context = this;
        // worker 内的模块不能直接访问宿主；这些回调只把事件写给宿主。
        services_.publish_event = &Worker::publishEvent;
        services_.publish_action = &Worker::publishAction;
        services_.emit_event = &Worker::emitEvent;
        services_.log = &Worker::logMessage;
        // 模块回传数据与读取自身目录：worker 迁移时漏了这三个，
        // 导致模块的 send_data 恒返回 -1、action 全部失效。
        services_.send_data = &Worker::sendDataCallback;
        services_.send_control = &Worker::sendControlCallback;
        services_.data_directory = &Worker::dataDirectoryCallback;
        handle_ = create_(&services_);
        if (!handle_) return false;
        const QByteArray json = QJsonDocument(transport).toJson(QJsonDocument::Compact);
        return onAttach_ ? onAttach_(handle_, json.constData()) == 0 : true;
    }

    bool data(const QByteArray &payload) {
        // 512 字节上限由宿主侧 LaneGate 先判；这里再兜一层。
        if (!handle_ || !onData_) return false;
        return onData_(handle_, 2, reinterpret_cast<const uint8_t *>(payload.constData()),
                       static_cast<uint32_t>(payload.size())) >= 0;
    }

    bool isLoaded() const { return library_.isLoaded(); }

    // 模块经 pi_host_services.send_data 回传业务数据：转成 reportState 帧
    // 发给宿主（宿主按 event 处理），而不是直接写网络。
    static int sendDataCallback(void *context, uint8_t /*transport*/,
                                const uint8_t *payload, uint32_t length) {
        auto *self = static_cast<Worker *>(context);
        if (!self || !payload || length == 0 || length > 65536) return -1;
        const QByteArray raw(reinterpret_cast<const char *>(payload), int(length));
        emitEventFrame("sample", QString::fromUtf8(raw).toUtf8().constData());
        return int(length);
    }

    // 控制面回传：P0 的 worker 不直接拿控制通道，明确拒绝而不是假装成功。
    static int sendControlCallback(void *, uint8_t, const uint8_t *, uint16_t, uint16_t *) {
        return -1;
    }

    // 模块的私有数据目录：由宿主经环境变量指定；未指定时返回空串，
    // 模块据此走"无持久化"路径，而不是写到任意位置。
    static const char *dataDirectoryCallback() {
        static const QByteArray dir = qEnvironmentVariable("ORANGE_PI_WORKER_DATA_DIR").toUtf8();
        return dir.constData();
    }

    // 调用模块导出的 pi_module_action。动作名必须由宿主按 manifest 白名单
    // 预校验过；这里再挡一次空值与超长，避免把任意字符串传进模块。
    bool invokeAction(const QString &action) {
        if (!handle_ || !onAction_ || action.isEmpty() || action.size() > 64) return false;
        for (const QChar c : action)
            if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-'))
                return false;
        return onAction_(handle_, action.toUtf8().constData()) >= 0;
    }

    void shutdown() {
        if (handle_ && onDetach_) onDetach_(handle_);
        if (handle_ && stop_) stop_(handle_);
        if (handle_ && destroy_) destroy_(handle_);
        handle_ = nullptr;
        if (library_.isLoaded()) library_.unload();
    }

private:
public:
    // 当前宿主连接。模块的 emit_event 回调可能在任意时刻触发，
    // 因此用进程内静态指针指向当次连接；无连接时事件直接丢弃（不缓存）。
    static QLocalSocket *peer;
private:

    static void emitEventFrame(const char *type, const char *json) {
        if (!peer || peer->state() != QLocalSocket::ConnectedState) return;
        const QJsonObject event{{"type", QString::fromUtf8(type ? type : "")},
                                {"json", QString::fromUtf8(json ? json : "")}};
        const QByteArray frame = WorkerIpc::encode(
            WorkerIpc::request(0, QStringLiteral("reportState"), event));
        peer->write(frame);
        peer->flush();
    }
    static void publishEvent(const char *type, const char *json) { emitEventFrame(type, json); }
    static void publishAction(const char *, const char *) {}
    static void emitEvent(void *, const char *type, const char *json) { emitEventFrame(type, json); }
    static void logMessage(int, const char *message) {
        fprintf(stderr, "module: %s\n", message ? message : "");
    }

    QLibrary library_;
    pi_host_services_t services_{};   // 必须早于 handle_ 的有效期
    pi_module_handle_t *handle_ = nullptr;
    // decltype(fn) 是函数类型，必须显式取指针；写 decltype(fn)* 会得到
    // "指向函数的指针" 的二次指针，reinterpret_cast 后调用即 SIGSEGV。
    decltype(&pi_module_create) create_ = nullptr;
    decltype(&pi_module_destroy) destroy_ = nullptr;
    decltype(&pi_module_on_attach) onAttach_ = nullptr;
    decltype(&pi_module_on_data) onData_ = nullptr;
    decltype(&pi_module_on_detach) onDetach_ = nullptr;
    decltype(&pi_module_stop) stop_ = nullptr;
    int (*onAction_)(pi_module_handle_t *, const char *) = nullptr;
};

QLocalSocket *Worker::peer = nullptr;

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Worker worker;

    // 监听单元 RuntimeDirectory 下的 Unix socket。
    // 路径来源：宿主经环境变量传（降级模式），否则按 session 推导。
    // 无论哪种，宿主与 worker 必须用同一套 WorkerIpc::socketPath 规则。
    QString socketPath = qEnvironmentVariable("ORANGE_PI_WORKER_SOCKET");
    if (socketPath.isEmpty()) {
        const QString session = qEnvironmentVariable("ORANGE_PI_WORKER_SESSION");
        if (session.isEmpty()) {
            // 直接运行且无参数时给一条明确错误，不静默等 stdin。
            qCritical("module_worker: ORANGE_PI_WORKER_SOCKET or "
                      "ORANGE_PI_WORKER_SESSION is required");
            return 2;
        }
        socketPath = WorkerIpc::socketPath(session);
    }

    QLocalServer server;
    // 上一次异常退出可能留下 socket 文件，先清掉（同属主才能删）。
    QFile::remove(socketPath);
    QDir().mkpath(QFileInfo(socketPath).absolutePath());
    if (!server.listen(socketPath)) {
        qCritical("module_worker: cannot listen on %s: %s",
                  qPrintable(socketPath), qPrintable(server.errorString()));
        return 3;
    }
    // 宿主是普通用户、worker 是 DynamicUser：socket 必须对其他用户可读写，
    // 否则宿主连不上。目录权限由 unit 的 RuntimeDirectoryMode 控制。
    QFile::setPermissions(socketPath, QFile::ReadOwner | QFile::WriteOwner
                                       | QFile::ReadGroup | QFile::WriteGroup
                                       | QFile::ReadOther | QFile::WriteOther);

    qInfo("module_worker: listening on %s", qPrintable(socketPath));

    // 单连接、同步循环：读一条、处理一条、回一条。worker 不需要并发。
    QByteArray buffer;
    while (true) {
        while (!server.hasPendingConnections()) {
            server.waitForNewConnection(1000);
            if (!server.hasPendingConnections()) {
                // 宿主长时间不连：继续等，但给心跳日志便于诊断。
                continue;
            }
        }
        QLocalSocket *connection = server.nextPendingConnection();
        if (!connection) continue;
        Worker::peer = connection;

        while (connection->waitForReadyRead(-1) || connection->bytesAvailable() > 0) {
            buffer.append(connection->readAll());
            if (buffer.isEmpty() && connection->state() != QLocalSocket::ConnectedState) break;

            while (buffer.size() >= 4) {
                const quint32 declared = quint32(quint8(buffer[0])) | (quint32(quint8(buffer[1])) << 8)
                    | (quint32(quint8(buffer[2])) << 16) | (quint32(quint8(buffer[3])) << 24);
                if (declared > quint32(WorkerIpc::kMaxMessageBytes)) {
                    // 超长立即断开并退出，不尝试恢复。
                    connection->disconnectFromServer();
                    delete connection;
                    Worker::peer = nullptr;
                    return 2;
                }
                if (quint32(buffer.size()) < declared + 4) break;
                const QByteArray frame = buffer.left(int(declared) + 4);
                buffer.remove(0, int(declared) + 4);

                const QJsonObject request = WorkerIpc::decode(frame);
                if (request.isEmpty()) continue;
                const QString op = request.value("op").toString();
                if (!WorkerIpc::opAllowed(op)) continue;

                const QJsonObject body = request.value("body").toObject();
                QString code = QStringLiteral("OK");
                if (op == QLatin1String("publishSample")) {
                    // 第一次 publishSample 带 library：加载并 attach。
                    // 之后的 publishSample 是业务数据（宿主 sendData 走这个 op）。
                    if (!body.value("library").toString().isEmpty()) {
                        // 库加载失败与 attach 失败用不同错误码：
                        // 前者是部署/路径问题，后者是模块自身拒绝。
                        if (!worker.load(body.value("library").toString()))
                            code = QStringLiteral("LIBRARY_UNAVAILABLE");
                        else if (!worker.attach(body.value("transport").toObject()))
                            code = QStringLiteral("ATTACH_REJECTED");
                    } else if (!worker.data(
                                   QByteArray::fromBase64(body.value("payload").toString().toLatin1()))) {
                        code = QStringLiteral("INVALID_MESSAGE");
                    }
                } else if (op == QLatin1String("readOwnRecords")) {
                    if (!worker.data(QByteArray::fromBase64(body.value("payload").toString().toLatin1())))
                        code = QStringLiteral("INVALID_MESSAGE");
                } else if (op == QLatin1String("invokeAction")) {
                    const QString action = body.value("action").toString();
                    if (!worker.invokeAction(action))
                        code = QStringLiteral("INVALID_MESSAGE");
                } else if (op == QLatin1String("stop")) {
                    worker.shutdown();
                }

                const QByteArray response = WorkerIpc::encode(
                    WorkerIpc::response(static_cast<quint64>(request.value("request_id").toDouble()),
                                        code, {}));
                connection->write(response);
                connection->flush();
            }
        }
        Worker::peer = nullptr;
        connection->disconnectFromServer();
        delete connection;
        // stop 之后宿主会关连接；若是 stop 请求则退出进程。
        if (!worker.isLoaded()) break;
    }

    QFile::remove(socketPath);
    return 0;
}
