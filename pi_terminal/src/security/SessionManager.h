#pragma once
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include "security/TaskCapability.h"

namespace security {


class AuthService;
class UserService;
class UserDatabase;
class CardIndex;

// 单一 activeUserId；切用户/退出立即销毁旧会话能力（服务端执行，不只是隐藏 UI）。
//
// 前台能力与任务能力分开（f-day1 §5）：
//   - 锁屏撤销前台读写，但已启动任务仍可向原用户追加样本
//   - 切用户前必须所有任务收尾，否则拒绝
//   - 迟到事件凭代次拒绝，不得挂到新用户
//
// QML 通过本对象的 Q_INVOKABLE 方法调用，不持有 PIN/KEK/DB key。
// 所有操作在服务端再次校验，不依赖 UI 隐藏控件。
class SessionManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString activeUserId READ activeUserId NOTIFY changed)
    Q_PROPERTY(QString activeDisplayName READ activeDisplayName NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY changed)
    Q_PROPERTY(bool isAdmin READ isAdmin NOTIFY changed)
    Q_PROPERTY(bool hasUsers READ hasUsers NOTIFY changed)
    Q_PROPERTY(QVariantList users READ users NOTIFY usersChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(int retryAfterMs READ retryAfterMs NOTIFY messageChanged)

public:
    SessionManager(UserService *users, AuthService *auth, QObject *parent = nullptr);
    ~SessionManager() override;

    // 认证成功后建立唯一会话。已有会话会先被撤销。
    QString begin(const QString &userId, const QByteArray &dbKey, bool admin);

    void lock();
    void unlock(const QString &wantUserId);
    void logout();

    // ---- QML 入口：全部经过服务端校验 ----

    // 低敏用户列表（只有 userId/displayName/admin/disabled）。
    Q_INVOKABLE void refreshUsers();
    // 用 PIN 登录。成功返回空，失败返回错误码（QML 只显示本地化提示）。
    Q_INVOKABLE QString login(const QString &userId, const QString &pin);
    // 锁屏后解锁：必须重输 PIN，不得刷卡解锁。
    Q_INVOKABLE QString unlockWithPin(const QString &pin);
    Q_INVOKABLE void lockNow();
    Q_INVOKABLE void signOut();

    // 任务能力：由可信侧在 measurement 开始时创建，绑定当前用户与设备。
    QString startTask(const QString &deviceId);
    bool finishTask(const QString &taskId);
    bool hasUnfinishedTasks() const;
    QVector<TaskCapability *> unfinishedTasks() const;

    // 追加样本：同时校验任务归属、设备与代次。
    bool appendTaskSample(const QString &taskId, const QString &deviceId, qint64 generation);

    QString activeUserId() const { return activeUserId_; }
    QString activeDisplayName() const { return activeDisplayName_; }
    bool locked() const { return locked_; }
    bool loggedIn() const { return !activeUserId_.isEmpty(); }
    bool hasUsers() const { return !users_.isEmpty(); }
    QVariantList users() const { return users_; }
    QString message() const { return message_; }
    int retryAfterMs() const { return retryAfterMs_; }

    // 只有当前 token 能通过；旧 token 一律拒绝。
    bool validate(const QString &token) const;
    bool isAdmin() const { return admin_; }

    // 可信侧短期持有；不暴露给 QML。
    QByteArray dbKey() const;

signals:
    void changed();
    void usersChanged();
    void messageChanged();
    // 切用户/退出/撤权：D 侧据此断开传输并销毁派生会话密钥。
    void sessionInvalidated();
    // 锁屏/解锁状态变化，QML 用它切换遮罩层。
    void lockStateChanged();

private:
    void clear();
    void setMessage(const QString &code, int retryAfterMs = 0);

    UserService *usersService_ = nullptr;
    AuthService *authService_ = nullptr;

    QString activeUserId_;
    QString activeDisplayName_;
    QString tokenHex_;   // 与 begin() 返回值同一形态，避免调用侧还去做编码转换
    QByteArray dbKey_;
    bool locked_ = false;
    bool admin_ = false;
    QString message_;
    int retryAfterMs_ = 0;
    QVariantList users_;
    QHash<QString, TaskCapability *> tasks_;
    QHash<QString, qint64> taskGenerations_;
};

}  // namespace security
