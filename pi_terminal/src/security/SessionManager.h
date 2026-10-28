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
    // QML 只读摘要：{taskId, deviceId}，不含 TaskCapability* 指针。
    Q_INVOKABLE QVariantList pendingTaskSummaries() const;
    // QML 用：结束某个任务。服务端校验归属。
    Q_INVOKABLE bool finishTask(const QString &taskId);
    bool hasUnfinishedTasks() const;
    QVector<TaskCapability *> unfinishedTasks() const;

    // 追加样本：同时校验任务归属、设备与代次。
    bool appendTaskSample(const QString &taskId, const QString &deviceId, qint64 generation);

    // 模块观测入可信存储（报告 §6.1）：QML/模块都拿不到 DB，只有这里能写。
    // 观察值缺失或库未打开时返回 false，不静默吞掉。
    Q_INVOKABLE bool recordObservation(const QString &deviceId, double value);

    // 报告 §6.4：按保留期限清理本用户历史。0 表示永久保留（直接返回 true）。
    Q_INVOKABLE bool applyRetention(int retentionDays);

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

    // 当前打开的用户库（未登录/锁屏前为 nullptr）。
    // 只给 SecurityService 这类可信宿主组件用；不得经 contextProperty 暴露。
    UserDatabase *database() const { return database_; }
    // 打开/关闭用户库。登录后由 TerminalApplication 调用一次。
    bool openDatabase();

signals:
    void changed();
    void usersChanged();
    void messageChanged();
    // 切用户/退出/撤权：D 侧据此断开传输并销毁派生会话密钥。
    void sessionInvalidated();
    // 锁屏/解锁状态变化，QML 用它切换遮罩层。
    void lockStateChanged();

private:
    // 只能在 unlockWithPin() 成功之后内部调用：本函数不验 PIN。
    // 公开它会成为锁屏旁路，因此不导出。
    void unlock(const QString &wantUserId);
    void clear();
    void setMessage(const QString &code, int retryAfterMs = 0);

    UserService *usersService_ = nullptr;
    AuthService *authService_ = nullptr;

    QString activeUserId_;
    QString activeDisplayName_;
    QString tokenHex_;   // 与 begin() 返回值同一形态，避免调用侧还去做编码转换
    QByteArray dbKey_;
    // 可信存储句柄：只在 SessionManager 内部持有，QML 与模块都拿不到。
    UserDatabase *database_ = nullptr;
    bool locked_ = false;
    bool admin_ = false;
    QString message_;
    int retryAfterMs_ = 0;
    QVariantList users_;
    QHash<QString, TaskCapability *> tasks_;
    QHash<QString, qint64> taskGenerations_;
};

}  // namespace security
