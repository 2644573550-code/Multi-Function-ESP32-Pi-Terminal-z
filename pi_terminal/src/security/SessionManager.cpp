#include "security/SessionManager.h"

#include "security/AuthService.h"
#include "security/UserService.h"

#include <QRandomGenerator>

namespace security {


SessionManager::SessionManager(UserService *users, AuthService *auth, QObject *parent)
    : QObject(parent), usersService_(users), authService_(auth) {
    refreshUsers();
}

SessionManager::~SessionManager() {
    clear();
}

QString SessionManager::begin(const QString &userId, const QByteArray &dbKey, bool admin) {
    if (userId.isEmpty() || dbKey.isEmpty()) return {};
    // 切用户前必须收尾：有未完成任务时拒绝新会话。
    if (hasUnfinishedTasks()) return {};
    clear();
    activeUserId_ = userId;
    dbKey_ = dbKey;
    admin_ = admin;
    locked_ = false;
    // 生成 32 字节随机 token，对外一律以 hex 字符串出现。
    QByteArray token(32, Qt::Uninitialized);
    for (int i = 0; i < token.size(); ++i) token[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    tokenHex_ = QString::fromLatin1(token.toHex());
    token.fill('\0');
    emit changed();
    return tokenHex_;
}

void SessionManager::lock() {
    if (activeUserId_.isEmpty() || locked_) return;
    // 前台能力立即失效；任务能力保留（仍可向原用户追加）。
    locked_ = true;
    emit changed();
    emit lockStateChanged();
}

void SessionManager::unlock(const QString &wantUserId) {
    // 锁屏后必须重输 PIN；调用方只有在 verifyPin 成功后才应走到这里。
    if (!locked_ || wantUserId != activeUserId_) return;
    locked_ = false;
    setMessage(QString());
    emit changed();
    emit lockStateChanged();
}

void SessionManager::logout() {
    if (activeUserId_.isEmpty()) return;
    // 退出时所有任务一并撤销，迟到回调凭代次被拒。
    clear();
    emit sessionInvalidated();
    emit changed();
    emit lockStateChanged();
}

// ---- QML 入口 ----

void SessionManager::refreshUsers() {
    users_.clear();
    if (!usersService_) { emit usersChanged(); return; }
    // 只暴露低敏字段：不含 salt/verifier/wrapped key。
    for (const auto &user : usersService_->listUsers()) {
        if (user.disabled) continue;   // 停用用户不出现在选择列表
        users_.append(QVariantMap{{"userId", user.userId},
                                  {"displayName", user.displayName},
                                  {"admin", user.admin}});
    }
    emit usersChanged();
    emit changed();   // hasUsers 依赖它
}

QString SessionManager::login(const QString &userId, const QString &pin) {
    if (!authService_) return QStringLiteral("STORAGE_UNAVAILABLE");
    // 已有会话时先要求收尾任务，再由调用方显式退出。
    if (hasUnfinishedTasks()) {
        setMessage(QStringLiteral("BUSY"));
        return QStringLiteral("BUSY");
    }
    const AuthService::Result result = authService_->verifyPin(userId, pin);
    if (result.code != QLatin1String(AuthResult::Ok)) {
        // 只回传 code 与可公开的剩余等待时间，不回显账号是否存在。
        setMessage(result.code, result.retryAfterMs);
        return result.code;
    }
    // 认证成功：建立唯一会话。
    if (begin(result.userId, result.dbKey, result.admin).isEmpty()) {
        setMessage(QStringLiteral("BUSY"));
        return QStringLiteral("BUSY");
    }
    for (const auto &user : usersService_ ? usersService_->listUsers()
                                          : QList<UserService::PublicUser>{}) {
        if (user.userId == result.userId) activeDisplayName_ = user.displayName;
    }
    setMessage(QString());
    emit changed();
    emit lockStateChanged();
    return QString();
}

QString SessionManager::unlockWithPin(const QString &pin) {
    if (!locked_) return QString();
    if (!authService_ || activeUserId_.isEmpty()) return QStringLiteral("SESSION_EXPIRED");
    const AuthService::Result result = authService_->verifyPin(activeUserId_, pin);
    if (result.code != QLatin1String(AuthResult::Ok)) {
        setMessage(result.code, result.retryAfterMs);
        return result.code;
    }
    // DB key 必须重新解封成功才允许解锁：PIN 对了但库打不开说明状态异常。
    if (result.dbKey != dbKey_) {
        setMessage(QStringLiteral("STORAGE_UNAVAILABLE"));
        return QStringLiteral("STORAGE_UNAVAILABLE");
    }
    locked_ = false;
    setMessage(QString());
    emit changed();
    emit lockStateChanged();
    return QString();
}

void SessionManager::lockNow() {
    lock();
}

void SessionManager::signOut() {
    logout();
    refreshUsers();
}

void SessionManager::setMessage(const QString &code, int retryAfterMs) {
    message_ = code;
    retryAfterMs_ = retryAfterMs;
    emit messageChanged();
}

QString SessionManager::startTask(const QString &deviceId) {
    if (activeUserId_.isEmpty() || deviceId.isEmpty()) return {};
    QByteArray raw(8, Qt::Uninitialized);
    for (int i = 0; i < raw.size(); ++i) raw[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    const QString taskId = QString::fromLatin1(raw.toHex());
    const qint64 generation = taskGenerations_.value(taskId, 0);
    tasks_.insert(taskId, new TaskCapability(TaskCapability::Kind::Task, activeUserId_, taskId, deviceId));
    taskGenerations_.insert(taskId, generation);
    return taskId;
}

bool SessionManager::finishTask(const QString &taskId) {
    TaskCapability *task = tasks_.take(taskId);
    if (!task) return false;
    task->revoke();
    // 推进代次：此后任何仍持有旧代次的迟到回调都不得写入。
    taskGenerations_[taskId] = task->generation() + 1;
    delete task;
    return true;
}

QVariantList SessionManager::pendingTaskSummaries() const {
    QVariantList list;
    for (TaskCapability *task : tasks_) {
        list.append(QVariantMap{{"taskId", task->taskId()}, {"deviceId", task->deviceId()}});
    }
    return list;
}

bool SessionManager::hasUnfinishedTasks() const { return !tasks_.isEmpty(); }

QVector<TaskCapability *> SessionManager::unfinishedTasks() const {
    QVector<TaskCapability *> result;
    for (TaskCapability *task : tasks_) result.append(task);
    return result;
}

bool SessionManager::appendTaskSample(const QString &taskId, const QString &deviceId,
                                       qint64 generation) {
    TaskCapability *task = tasks_.value(taskId);
    if (!task) return false;
    // 归属、设备、代次三者都要对上；锁屏不影响追加。
    return task->canAppend(deviceId, task->originUserId(), generation)
        && generation == taskGenerations_.value(taskId, 0);
}

void SessionManager::clear() {
    for (TaskCapability *task : tasks_) {
        task->revoke();
        delete task;
    }
    tasks_.clear();
    taskGenerations_.clear();
    activeUserId_.clear();
    activeDisplayName_.clear();
    admin_ = false;
    locked_ = false;
    tokenHex_.fill(QLatin1Char('\0'));
    tokenHex_.clear();
    dbKey_.fill('\0');   // 显式擦除，不只让容器失效
    dbKey_.clear();
}

bool SessionManager::validate(const QString &token) const {
    if (locked_ || tokenHex_.isEmpty() || token.size() != tokenHex_.size()) return false;
    // 常量时间比较，避免用比较耗时泄露前缀。
    unsigned char diff = 0;
    const QByteArray candidate = token.toLatin1();
    const QByteArray expected = tokenHex_.toLatin1();
    if (candidate.size() != expected.size()) return false;
    for (int i = 0; i < expected.size(); ++i) {
        diff |= static_cast<unsigned char>(candidate[i] ^ expected[i]);
    }
    return diff == 0;
}

QByteArray SessionManager::dbKey() const { return locked_ ? QByteArray{} : dbKey_; }

}  // namespace security
