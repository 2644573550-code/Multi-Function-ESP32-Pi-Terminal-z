#pragma once
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include "security/TaskCapability.h"

class AuthService;

// 单一 activeUserId；切用户/退出立即销毁旧会话能力（服务端执行，不只是隐藏 UI）。
//
// 前台能力与任务能力分开（f-day1 §5）：
//   - 锁屏撤销前台读写，但已启动任务仍可向原用户追加样本
//   - 切用户前必须所有任务收尾，否则拒绝
//   - 迟到事件凭代次拒绝，不得挂到新用户
class SessionManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString activeUserId READ activeUserId NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)

public:
    explicit SessionManager(QObject *parent = nullptr);
    ~SessionManager() override;

    // 认证成功后建立唯一会话。已有会话会先被撤销。
    QString begin(const QString &userId, const QByteArray &dbKey, bool admin);

    void lock();
    void unlock(const QString &wantUserId);
    void logout();

    // 任务能力：由可信侧在 measurement 开始时创建，绑定当前用户与设备。
    // 返回任务 ID；无活动会话时返回空。
    QString startTask(const QString &deviceId);
    // 收尾任务（完成/取消/明确停止）。收尾后才允许切用户。
    bool finishTask(const QString &taskId);
    bool hasUnfinishedTasks() const;
    QVector<TaskCapability *> unfinishedTasks() const;

    // 追加样本：同时校验任务归属、设备与代次。
    bool appendTaskSample(const QString &taskId, const QString &deviceId, qint64 generation);

    QString activeUserId() const { return activeUserId_; }
    bool locked() const { return locked_; }

    // 只有当前 token 能通过；旧 token 一律拒绝。
    bool validate(const QString &token) const;
    bool isAdmin() const { return admin_; }

    // 可信侧短期持有；不暴露给 QML。
    QByteArray dbKey() const;

signals:
    void changed();
    // 切用户/退出/撤权：D 侧据此断开传输并销毁派生会话密钥。
    void sessionInvalidated();

private:
    void clear();

    QString activeUserId_;
    QString tokenHex_;   // 与 begin() 返回值同一形态，避免调用侧还去做编码转换
    QByteArray dbKey_;
    bool locked_ = false;
    bool admin_ = false;
    QHash<QString, TaskCapability *> tasks_;
    QHash<QString, qint64> taskGenerations_;
};
