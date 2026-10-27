#include "security/AuthService.h"

#include "security/KeyWrap.h"
#include "security/PinPolicy.h"
#include "security/UserService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QString defaultLockPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/auth_locks.json");
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

}  // namespace

AuthService::AuthService(UserService &users, QString lockStatePath)
    : users_(users)
    , lockStatePath_(lockStatePath.isEmpty() ? defaultLockPath() : std::move(lockStatePath)) {}

AuthService::LockState AuthService::lockFor(const QString &userId) const {
    QFile file(lockStatePath_);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject entry = root.value("locks").toObject().value(userId).toObject();
    LockState state;
    state.failures = entry.value("failures").toInt();
    state.lockedUntilMs = static_cast<qint64>(entry.value("locked_until_ms").toDouble());
    return state;
}

void AuthService::storeLock(const QString &userId, const LockState &state) {
    QJsonObject root;
    QFile file(lockStatePath_);
    if (file.open(QIODevice::ReadOnly)) root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QJsonObject locks = root.value("locks").toObject();
    if (state.failures == 0 && state.lockedUntilMs == 0) locks.remove(userId);
    else locks[userId] = QJsonObject{{"failures", state.failures},
                                     {"locked_until_ms", static_cast<double>(state.lockedUntilMs)}};
    const QJsonDocument document(QJsonObject{{"schema", "p0-auth-locks-v1"}, {"locks", locks}});

    QDir().mkpath(QFileInfo(lockStatePath_).absolutePath());
    QSaveFile out(lockStatePath_);
    if (!out.open(QIODevice::WriteOnly)) return;
    if (!out.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return;
    if (out.write(document.toJson(QJsonDocument::Compact)) < 0) { out.cancelWriting(); return; }
    out.commit();
    QFile::setPermissions(lockStatePath_, QFile::ReadOwner | QFile::WriteOwner);
}

bool AuthService::terminalRateLimited(qint64 now) const {
    return lastAttemptMs_ != 0 && now - lastAttemptMs_ < PinPolicy::kTerminalMinIntervalMs;
}

AuthService::Result AuthService::verifyPin(const QString &userId, const QString &pin) {
    const qint64 now = nowMs();
    // 终端级限速：未知账号也走这里，所以不能用它判断账号是否存在。
    if (terminalRateLimited(now)) {
        return {AuthResult::RateLimited, PinPolicy::kTerminalMinIntervalMs, {}, {}, false};
    }
    lastAttemptMs_ = now;

    UserRecord record;
    const bool found = users_.lookup(userId, &record);
    LockState state = found ? lockFor(userId) : LockState{};
    if (found && state.lockedUntilMs > now) {
        return {AuthResult::RateLimited, static_cast<int>(state.lockedUntilMs - now), {}, {}, false};
    }

    // 账号不存在与 PIN 错误返回同一个 code，不泄漏存在性。
    if (!found || record.disabled || !PinPolicy::validPin(pin)) {
        if (found) {
            ++state.failures;
            if (state.failures >= PinPolicy::kMaxAccountFailures)
                state.lockedUntilMs = now + PinPolicy::kAccountLockMs;
            storeLock(userId, state);
        }
        return {AuthResult::AuthFailed, 0, {}, {}, false};
    }

    const QByteArray kek = PinPolicy::deriveKek(pin, record.salt, record.iterations);
    if (!PinPolicy::verifyMatches(record.verifier, kek)) {
        ++state.failures;
        if (state.failures >= PinPolicy::kMaxAccountFailures)
            state.lockedUntilMs = now + PinPolicy::kAccountLockMs;
        storeLock(userId, state);
        return {AuthResult::AuthFailed, 0, {}, {}, false};
    }

    const QByteArray dbKey = KeyWrap::open(kek, record.wrappedDbKey, record.dbKeyNonce);
    if (dbKey.isEmpty()) return {AuthResult::AuthFailed, 0, {}, {}, false};

    storeLock(userId, {});   // 正确认证才清除该账号失败计数
    return {AuthResult::Ok, 0, dbKey, record.userId, record.admin};
}

bool AuthService::verifyAdminPin(const QString &userId, const QString &pin) {
    UserRecord record;
    if (!users_.lookup(userId, &record) || !record.admin || record.disabled) return false;
    if (!PinPolicy::validPin(pin)) return false;
    return PinPolicy::verifyMatches(record.verifier,
                                    PinPolicy::deriveKek(pin, record.salt, record.iterations));
}

bool AuthService::changePin(const QString &userId, const QString &oldPin, const QString &newPin) {
    UserRecord record;
    if (!users_.lookup(userId, &record) || record.disabled) return false;
    if (!PinPolicy::validPin(newPin)) return false;

    const QByteArray oldKek = PinPolicy::deriveKek(oldPin, record.salt, record.iterations);
    if (!PinPolicy::verifyMatches(record.verifier, oldKek)) return false;

    // 改 PIN 只重封装 DB key，不重加密整库（f-day1 §5）。
    const QByteArray dbKey = KeyWrap::open(oldKek, record.wrappedDbKey, record.dbKeyNonce);
    if (dbKey.isEmpty()) return false;

    UserRecord updated = record;
    updated.salt = PinPolicy::salt16();
    const QByteArray newKek = PinPolicy::deriveKek(newPin, updated.salt, updated.iterations);
    if (newKek.isEmpty()) return false;
    updated.verifier = PinPolicy::verifyValue(newKek);
    updated.wrappedDbKey = KeyWrap::seal(newKek, dbKey, &updated.dbKeyNonce);
    if (updated.wrappedDbKey.isEmpty()) return false;

    // 失败时保留旧封装（文档要求"失败保留可用旧封装"）。
    return users_.replaceRecord(updated);
}
