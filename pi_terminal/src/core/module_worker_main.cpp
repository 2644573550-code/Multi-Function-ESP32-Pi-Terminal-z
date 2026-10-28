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

#include <cstdio>
#include <unistd.h>

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

    void shutdown() {
        if (handle_ && onDetach_) onDetach_(handle_);
        if (handle_ && stop_) stop_(handle_);
        if (handle_ && destroy_) destroy_(handle_);
        handle_ = nullptr;
        if (library_.isLoaded()) library_.unload();
    }

private:
    static void emitEventFrame(const char *type, const char *json) {
        const QJsonObject event{{"type", QString::fromUtf8(type ? type : "")},
                                {"json", QString::fromUtf8(json ? json : "")}};
        const QByteArray frame = WorkerIpc::encode(
            WorkerIpc::request(0, QStringLiteral("reportState"), event));
        ::write(STDOUT_FILENO, frame.constData(), frame.size());
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
};

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    Worker worker;

    // 通过 stdin 接收宿主请求；stdout 只用于回消息。
    // 简单同步循环：读一条、处理一条、回一条。worker 不需要并发。
    // 用 read(2) 而不是 fread：Qt 初始化后 stdio 可能带缓冲，导致宿主
    // 已写入的数据在 worker 侧迟迟不可见。
    QByteArray buffer;
    char chunk[4096];
    while (true) {
        const ssize_t got = ::read(STDIN_FILENO, chunk, sizeof(chunk));
        if (got <= 0) break;
        buffer.append(chunk, static_cast<int>(got));

        while (buffer.size() >= 4) {
            const quint32 declared = quint32(quint8(buffer[0])) | (quint32(quint8(buffer[1])) << 8)
                | (quint32(quint8(buffer[2])) << 16) | (quint32(quint8(buffer[3])) << 24);
            if (declared > quint32(WorkerIpc::kMaxMessageBytes)) return 2;   // 超长立即退出
            if (quint32(buffer.size()) < declared + 4) break;              // 等更多字节
            const QByteArray frame = buffer.left(int(declared) + 4);
            buffer.remove(0, int(declared) + 4);

            const QJsonObject request = WorkerIpc::decode(frame);
            if (request.isEmpty()) continue;   // 非法消息直接丢，不猜测解析
            const QString op = request.value("op").toString();
            if (!WorkerIpc::opAllowed(op)) continue;

            const QJsonObject body = request.value("body").toObject();
            QString code = QStringLiteral("OK");
            if (op == QLatin1String("publishSample")) {
                if (!worker.load(body.value("library").toString())) code = QStringLiteral("INVALID_MESSAGE");
                else if (!worker.attach(body.value("transport").toObject())) code = QStringLiteral("INVALID_MESSAGE");
            } else if (op == QLatin1String("readOwnRecords")) {
                if (!worker.data(QByteArray::fromBase64(body.value("payload").toString().toLatin1())))
                    code = QStringLiteral("INVALID_MESSAGE");
            } else if (op == QLatin1String("stop")) {
                worker.shutdown();
            }

            const QByteArray response = WorkerIpc::encode(
                WorkerIpc::response(static_cast<quint64>(request.value("request_id").toDouble()),
                                    code, {}));
            ::write(STDOUT_FILENO, response.constData(), response.size());
        }
    }
    return 0;
}
