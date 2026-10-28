#pragma once
#include <QObject>
#include <QJsonObject>
#include <QHash>
#include <QLibrary>
#include <QSharedPointer>
#include <functional>
#include "modules/api/pi_module_api.h"

namespace security { class ModuleWorkerClient; }

// 模块不再在宿主进程里 QLibrary 加载（T-11）。
// 每个 session 一个 worker 进程，模块代码在独立 UID 下运行。
//
// 宿主只保留 manifest 校验与 QML 页面路径，业务回调经 worker IPC 转发。
// 详见 core/ModuleWorkerClient.h 与 packaging/orange-pi-worker@.service。
class ModuleManager : public QObject {
    Q_OBJECT
public:
    explicit ModuleManager(QObject *parent = nullptr) : QObject(parent) {}
    ~ModuleManager() override;
    bool load(const QString &session, int id, const QString &directory, const QJsonObject &transport);
    void unload(const QString &session);
    void data(const QString &session, quint8 transport, const QByteArray &payload);
    void connected(const QString &session, quint8 transport);
    void action(const QString &session, const QString &action);
    void disconnectRequest(const QString &session, const QJsonObject &request);
    QJsonObject manifest(const QString &session) const;
    QString page(const QString &session) const;
    // 已加载模块的包版本（manifest 的 package_version 或 version）。
    QString packageVersion(const QString &session) const;
    // 页面的 schema 版本；模块未声明时为 1。
    int pageSchemaVersion(const QString &session) const;
    std::function<bool(const QString &, quint8, const QByteArray &)> sender;
    std::function<bool(const QString &, quint8, const QByteArray &, quint16 *)> controlSender;

    // 回退开关：ORANGE_PI_MODULE_INPROCESS=1 时回到 QLibrary 同进程加载。
    // 只用于既有测试与开发调试 —— 那条路径没有 worker 隔离，
    // 不得作为 T-11 的验收证据（§0.4 要求证明 unit 实际生效）。
    static bool inProcessMode();
signals:
    void error(QString session, QString message);
    void event(QString session, QString type, QString json);
private:
    struct Instance {
        ModuleManager *owner;
        QString session, directory;
        QJsonObject manifest;
        security::ModuleWorkerClient *worker = nullptr;
        // 只有在回退模式下才用到；正常情况下为空。
        QLibrary *library = nullptr;
        pi_module_handle_t *handle = nullptr;
        pi_host_services_t services{};
        bool stopping = false;
    };
    static int sendControl(void *, uint8_t, const uint8_t *, uint16_t, uint16_t *);
    static int sendData(void *, uint8_t, const uint8_t *, uint32_t);
    static void publishAction(const char *, const char *);
    static void publishEvent(const char *, const char *);
    static const char *dataDirectory();
    static void emitEvent(void *, const char *, const char *);
    QHash<QString, QSharedPointer<Instance>> instances_;
};
