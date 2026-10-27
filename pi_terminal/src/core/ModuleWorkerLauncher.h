#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

// 把模块代码从宿主进程移出去（T-11）。
//
// 走 systemd：orange-pi-worker@<session>.service，带 DynamicUser/PrivateNetwork。
// QProcess 本身不是沙箱 —— 只有 unit 真正生效才算隔离，故本类强制经 systemctl 启动。
class ModuleWorkerLauncher : public QObject {
    Q_OBJECT
public:
    explicit ModuleWorkerLauncher(QObject *parent = nullptr);

    // 绑定 device/slot/session；这些不由 worker 自报。
    bool start(const QString &session, const QString &deviceId);
    void stop(const QString &session);
    bool isRunning(const QString &session) const;

    // 测试用：只生成 unit 名与启动命令，不真的执行。
    static QString unitName(const QString &session);
    static QStringList startCommand(const QString &session);

    // 崩溃隔离：worker 挂了宿主必须还活着，这里只报告不传播。
    QString lastError() const { return lastError_; }

signals:
    void workerExited(QString session, int code);

private:
    QString lastError_;
};
