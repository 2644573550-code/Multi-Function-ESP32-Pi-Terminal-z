#pragma once
#include <QByteArray>
#include <QString>

class UserService;

// 认证结果。错误提示不区分「账号不存在」与「PIN 错误」（f-day1 §2）。
namespace AuthResult {
constexpr auto Ok = "OK";
constexpr auto AuthFailed = "AUTH_FAILED";
constexpr auto RateLimited = "RATE_LIMITED";
constexpr auto NotAuthorized = "NOT_AUTHORIZED";
}  // namespace AuthResult

// 认证与限速。锁定状态落盘，重启不清除（§2 要求）。
class AuthService {
public:
    struct Result {
        QString code;
        int retryAfterMs = 0;      // 仅 RATE_LIMITED 时非零，可公开的剩余等待时间
        QByteArray dbKey;          // 仅 Ok 时非空；只在可信侧短期持有
        QString userId;
        bool admin = false;
    };

    explicit AuthService(UserService &users, QString lockStatePath = {});

    // 未知账号同样计入终端级 1 次/秒上限，避免用它探测账号是否存在。
    Result verifyPin(const QString &userId, const QString &pin);

    // 成功后清除该账号失败计数；失败按连错累计。
    bool changePin(const QString &userId, const QString &oldPin, const QString &newPin);

    // 仅测试/管理员重验用：不消耗一次尝试。
    bool verifyAdminPin(const QString &userId, const QString &pin);

private:
    struct LockState {
        int failures = 0;
        qint64 lockedUntilMs = 0;   // 单调时钟不可跨重启，故用 epoch 毫秒
    };
    LockState lockFor(const QString &userId) const;
    void storeLock(const QString &userId, const LockState &state);
    bool terminalRateLimited(qint64 nowMs) const;

    UserService &users_;
    QString lockStatePath_;
    mutable qint64 lastAttemptMs_ = 0;
};
