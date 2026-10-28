#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>

namespace security {

// 宿主侧的 worker 客户端（T-11）。
//
// 宿主不再 QLibrary 加载模块 .so，而是：
//   1. 经 systemctl 起 orange-pi-worker@<session>.service（DynamicUser=yes）
//   2. 通过该 unit 的 stdin/stdout 发 p0-worker-v1 请求
//
// 为什么必须走 systemd 而不是裸 QProcess：
// unit 才提供 DynamicUser/PrivateNetwork/ProtectHome 等隔离；
// 裸 QProcess 起的进程与宿主同 UID、同网络，等于没隔离。
//
// shortcut: 本机无 systemd user session 时降级为直接起 worker 二进制并标记 degraded，
//           Pi 上必须用 systemctl 路径；degraded 状态不得写入验收证据
class ModuleWorkerClient : public QObject {
    Q_OBJECT
public:
    explicit ModuleWorkerClient(QObject *parent = nullptr);
    ~ModuleWorkerClient() override;

    // 启动 worker 并加载模块库。失败返回 false，errorCode 给出原因。
    bool start(const QString &session, const QString &libraryPath, const QJsonObject &transport);
    void stop(const QString &session);
    bool isRunning() const;

    // 业务数据（≤512 字节，由调用方 LaneGate 先判）。
    bool sendData(const QByteArray &payload);

    QString errorCode() const { return errorCode_; }
    QString lastError() const { return lastError_; }
    // true 表示没走 systemctl（本机无 systemd 时的降级），不得当作隔离证据。
    bool degraded() const { return degraded_; }

signals:
    // worker 主动上报的事件（reportState）
    void eventReceived(QString type, QString json);
    // worker 崩溃：宿主必须存活，只报告。
    void workerExited(int code);

private:
    bool request(const QString &op, const QJsonObject &body, QJsonObject *response);

    QProcess *process_ = nullptr;
    QByteArray buffer_;
    QString errorCode_;
    QString lastError_;
    bool degraded_ = false;
    quint64 nextRequestId_ = 1;
};

}  // namespace security
