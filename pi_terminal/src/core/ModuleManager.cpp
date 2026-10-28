#include "ModuleManager.h"

#include "core/ModuleWorkerClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUrl>

bool ModuleManager::inProcessMode() {
    // 只认显式的 "1"：默认走 worker，避免"忘了配"就悄悄退回非隔离路径。
    return qEnvironmentVariable("ORANGE_PI_MODULE_INPROCESS") == QLatin1String("1");
}

ModuleManager::~ModuleManager() {
    for (const auto &key : instances_.keys()) unload(key);
}

namespace {

// manifest 校验对两条路径都一样：路径逃逸、ABI、架构都必须先挡住。
// 宿主支持的模块页面 schema 上限。新增不兼容字段时递增，
// 旧模块继续用旧版本号运行（宿主只拒绝更高的版本）。
constexpr int kSupportedQmlSchema = 1;

bool verifyManifest(const QJsonObject &m, int id, const QString &directory, QString *error) {
    auto fail = [error](const QString &s) { if (error) *error = s; return false; };
    if (m.value("module_id").toDouble(-1) != id || id < 1 || id > 255
        || m.value("id").toString().isEmpty() || m.value("abi_version").toDouble(-1) != 1
        || !m.value("host_arch").toArray().contains(QSysInfo::currentCpuArchitecture())
        || m.value("qt_major").toInt() != 6
        || m.value("unload_contract").toString() != QLatin1String("synchronous-stop-v1"))
        return fail(QStringLiteral("manifest incompatible"));
    // 页面 schema 版本：宿主在加载前就要拒绝不兼容页面，
    // 而不是等 QML 运行时报一堆绑定错误。缺省视为 1（存量模块兼容）。
    const int pageSchema = m.value("qml_schema_version").toInt(1);
    if (pageSchema < 1 || pageSchema > kSupportedQmlSchema) {
        return fail(QStringLiteral("module page schema %1 unsupported (host supports <= %2)")
                        .arg(pageSchema).arg(kSupportedQmlSchema));
    }
    // 页面文件必须真实存在：manifest 写了但包里没有，是最常见的包构建错误。
    const QString pageRelative = m.value("qml_page").toString();
    if (!QFileInfo(directory + '/' + pageRelative).isFile()) {
        return fail(QStringLiteral("module page missing from package: ") + pageRelative);
    }

    for (const auto *key : {"library", "qml_page"}) {
        const QString relative = m.value(QLatin1String(key)).toString();
        const QString path = QFileInfo(directory + '/' + relative).canonicalFilePath();
        if (relative.isEmpty() || QDir::isAbsolutePath(relative)
            || !path.startsWith(QDir(directory).canonicalPath() + '/'))
            return fail(QStringLiteral("plugin path escapes package"));
    }
    return true;
}

}  // namespace

bool ModuleManager::load(const QString &session, int id, const QString &directory,
                         const QJsonObject &transport) {
    auto fail = [this, &session](const QString &s) { emit error(session, s); return false; };
    if (instances_.contains(session)) return fail(QStringLiteral("duplicate session"));

    QFile file(directory + "/manifest.json");
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QJsonParseError parse;
    const auto m = QJsonDocument::fromJson(file.readAll(), &parse).object();
    if (parse.error != QJsonParseError::NoError) return fail(QStringLiteral("manifest unreadable"));

    QString manifestError;
    if (!verifyManifest(m, id, directory, &manifestError)) return fail(manifestError);

    auto i = QSharedPointer<Instance>::create();
    i->owner = this;
    i->session = session;
    i->directory = directory;
    i->manifest = m;

    if (inProcessMode()) {
        // 回退路径：同进程 QLibrary。保留是为了既有测试与开发调试，
        // 不构成 T-11 的 worker 隔离证据。
        i->library = new QLibrary(directory + '/' + m.value("library").toString());
        i->library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
        if (!i->library->load()) {
            const QString reason = i->library->errorString();
            delete i->library;
            return fail(reason);
        }
        for (const auto *name : {"pi_module_api_version", "pi_module_manifest_json", "pi_module_create",
                                 "pi_module_destroy", "pi_module_on_attach", "pi_module_on_detach",
                                 "pi_module_stop", "pi_module_on_data"}) {
            if (!i->library->resolve(name)) {
                i->library->unload();
                delete i->library;
                return fail(QStringLiteral("missing entry: ") + QLatin1String(name));
            }
        }
        auto version = reinterpret_cast<uint32_t (*)()>(i->library->resolve("pi_module_api_version"));
        auto embedded = reinterpret_cast<const char *(*)()>(i->library->resolve("pi_module_manifest_json"));
        if (version() != 1 || QJsonDocument::fromJson(embedded()).object() != m) {
            i->library->unload();
            delete i->library;
            return fail(QStringLiteral("embedded manifest/ABI differs"));
        }
        i->services = {1, i.data(), sendControl, sendData, publishAction, publishEvent, dataDirectory,
                       [](int, const char *s) { qInfo("plugin: %s", s ? s : ""); }, emitEvent};
        auto create = reinterpret_cast<pi_module_handle_t *(*)(const pi_host_services_t *)>(
            i->library->resolve("pi_module_create"));
        i->handle = create(&i->services);
        if (!i->handle) {
            i->library->unload();
            delete i->library;
            return fail(QStringLiteral("create returned null"));
        }
        instances_.insert(session, i);
        auto attach = reinterpret_cast<int (*)(pi_module_handle_t *, const char *)>(
            i->library->resolve("pi_module_on_attach"));
        if (attach(i->handle, QJsonDocument(transport).toJson(QJsonDocument::Compact).constData()) != 0) {
            unload(session);
            return fail(QStringLiteral("attach rejected"));
        }
        return true;
    }

    // 正常路径：独立 worker 进程，由 systemd unit 提供隔离。
    i->worker = new security::ModuleWorkerClient(this);
    connect(i->worker, &security::ModuleWorkerClient::eventReceived, this,
            [this, session](const QString &type, const QString &json) { emit event(session, type, json); });
    connect(i->worker, &security::ModuleWorkerClient::workerExited, this,
            [this, session](int code) {
        // worker 崩溃不得带崩宿主：只报告，由上层决定是否重连。
        emit error(session, QStringLiteral("worker exited with code %1").arg(code));
    });

    const QString libraryPath = QFileInfo(directory + '/' + m.value("library").toString()).canonicalFilePath();
    if (!i->worker->start(session, libraryPath, transport)) {
        const QString reason = i->worker->lastError();
        delete i->worker;
        return fail(QStringLiteral("worker start failed: %1").arg(reason));
    }
    instances_.insert(session, i);
    return true;
}

void ModuleManager::unload(const QString &session) {
    auto i = instances_.value(session);
    if (!i || i->stopping) return;
    i->stopping = true;
    if (i->worker) {
        i->worker->stop(session);
        delete i->worker;
        i->worker = nullptr;
    }
    if (i->library && i->handle) {
        // resolve 可能返回空（库已卸载或符号缺失）；空指针调用会 SIGSEGV。
        for (const auto *name : {"pi_module_on_detach", "pi_module_stop", "pi_module_destroy"}) {
            if (auto *fn = reinterpret_cast<void (*)(pi_module_handle_t *)>(i->library->resolve(name)))
                fn(i->handle);
        }
        i->handle = nullptr;
    }
    if (i->library) {
        i->library->unload();
        delete i->library;
        i->library = nullptr;
    }
    instances_.remove(session);
}

void ModuleManager::data(const QString &session, quint8 t, const QByteArray &payload) {
    auto i = instances_.value(session);
    if (!i || i->stopping) return;
    if (i->worker) {
        if (!i->worker->sendData(payload))
            emit error(session, QStringLiteral("worker rejected data: %1").arg(i->worker->errorCode()));
        return;
    }
    auto *onData = reinterpret_cast<int (*)(pi_module_handle_t *, uint8_t, const uint8_t *, uint32_t)>(
        i->library ? i->library->resolve("pi_module_on_data") : nullptr);
    if (!onData) return;
    onData(i->handle, t, reinterpret_cast<const uint8_t *>(payload.constData()), uint32_t(payload.size()));
}

void ModuleManager::connected(const QString &session, quint8 t) {
    auto i = instances_.value(session);
    if (!i || i->stopping) return;
    if (i->worker) {
        // worker 的 transport 在 load 时传入；连接状态变化由 sendData 通道体现。
        return;
    }
    auto *onConnected = reinterpret_cast<void (*)(pi_module_handle_t *, uint8_t)>(
        i->library ? i->library->resolve("pi_module_on_transport_connected") : nullptr);
    if (onConnected) onConnected(i->handle, t);
}

void ModuleManager::action(const QString &session, const QString &action) {
    auto i = instances_.value(session);
    if (!i || i->stopping) return;
    if (!i->manifest.value("actions").toArray().contains(action)) return;
    if (i->worker) {
        // §8.4：动作名已在上面按 manifest 的 actions 白名单校验过；
        // 这里只做转发，不把任意字符串传进模块。
        i->worker->invokeAction(action);
        return;
    }
    auto *onAction = reinterpret_cast<int (*)(pi_module_handle_t *, const char *)>(
        i->library ? i->library->resolve("pi_module_action") : nullptr);
    if (onAction) onAction(i->handle, action.toUtf8().constData());
}

void ModuleManager::disconnectRequest(const QString &session, const QJsonObject &request) {
    auto i = instances_.value(session);
    if (!i || i->stopping) return;
    if (i->worker) return;   // 同上：action 转发不在 P0 IPC 白名单内
    auto *onDisconnect = reinterpret_cast<void (*)(pi_module_handle_t *, const char *)>(
        i->library ? i->library->resolve("pi_module_on_disconnect_request") : nullptr);
    if (onDisconnect)
        onDisconnect(i->handle, QJsonDocument(request).toJson(QJsonDocument::Compact).constData());
}

QJsonObject ModuleManager::manifest(const QString &session) const {
    auto i = instances_.value(session);
    return i ? i->manifest : QJsonObject{};
}

QString ModuleManager::packageVersion(const QString &session) const {
    auto i = instances_.value(session);
    if (!i) return {};
    const QString v = i->manifest.value("package_version").toString();
    return v.isEmpty() ? i->manifest.value("version").toString() : v;
}

int ModuleManager::pageSchemaVersion(const QString &session) const {
    auto i = instances_.value(session);
    return i ? i->manifest.value("qml_schema_version").toInt(1) : 0;
}

QString ModuleManager::page(const QString &session) const {
    auto i = instances_.value(session);
    return i ? QUrl::fromLocalFile(i->directory + '/' + i->manifest.value("qml_page").toString()).toString()
             : QString{};
}

int ModuleManager::sendControl(void *c, uint8_t cmd, const uint8_t *p, uint16_t n, uint16_t *seq) {
    auto *i = static_cast<Instance *>(c);
    if (!i || i->stopping || (!p && n) || QThread::currentThread() != i->owner->thread()
        || !i->owner->controlSender)
        return -1;
    return i->owner->controlSender(i->session, cmd, QByteArray(reinterpret_cast<const char *>(p), n), seq)
        ? 0 : -1;
}

int ModuleManager::sendData(void *c, uint8_t t, const uint8_t *p, uint32_t n) {
    auto *i = static_cast<Instance *>(c);
    if (!i || i->stopping || (!p && n) || n > 65536
        || QThread::currentThread() != i->owner->thread() || !i->owner->sender)
        return -1;
    return i->owner->sender(i->session, t, QByteArray(reinterpret_cast<const char *>(p), int(n))) ? 0 : -1;
}

void ModuleManager::publishAction(const char *action, const char *text) {
    qInfo("plugin action: %s (%s)", action ? action : "", text ? text : "");
}

void ModuleManager::publishEvent(const char *type, const char *json) {
    qInfo("plugin event: %s %s", type ? type : "", json ? json : "");
}

const char *ModuleManager::dataDirectory() {
    static const QByteArray path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toUtf8();
    QDir().mkpath(QString::fromUtf8(path));
    return path.constData();
}

void ModuleManager::emitEvent(void *c, const char *type, const char *json) {
    auto *i = static_cast<Instance *>(c);
    if (i && !i->stopping && type && json && QThread::currentThread() == i->owner->thread())
        emit i->owner->event(i->session, QString::fromUtf8(type), QString::fromUtf8(json));
}
