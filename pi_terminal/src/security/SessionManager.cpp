#include "security/SessionManager.h"

#include <QRandomGenerator>

SessionManager::SessionManager(QObject *parent) : QObject(parent) {}

QString SessionManager::begin(const QString &userId, const QByteArray &dbKey, bool admin) {
    if (userId.isEmpty() || dbKey.isEmpty()) return {};
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
    // 前台能力立即失效；任务追加能力由可信侧按原用户继续，不在本类表达。
    locked_ = true;
    emit changed();
}

void SessionManager::unlock(const QString &wantUserId) {
    // 锁屏后必须重输 PIN；调用方只有在 verifyPin 成功后才应走到这里。
    if (!locked_ || wantUserId != activeUserId_) return;
    locked_ = false;
    emit changed();
}

void SessionManager::logout() {
    if (activeUserId_.isEmpty()) return;
    clear();
    emit sessionInvalidated();
    emit changed();
}

void SessionManager::clear() {
    activeUserId_.clear();
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
