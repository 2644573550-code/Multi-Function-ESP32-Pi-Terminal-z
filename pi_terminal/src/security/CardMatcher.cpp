#include "security/CardMatcher.h"

#include "security/UserDatabase.h"

namespace security {


namespace {
// D 未交付前的默认派生：直接判失败，绝不退回明文 UID 比较。
// 调用方必须注入真实 deriveUidIndex；注入前整条 RFID 路径失败关闭。
QByteArray noDerive(const QByteArray &) { return {}; }
}  // namespace

CardIndex::CardIndex(UserDatabase *database) : database_(database) {}

CardMatch CardIndex::findOwner(const QByteArray &rawUid,
                                const QVector<QPair<QString, QByteArray>> &keys,
                                Derive derive) {
    CardMatch result;
    // 长度非法或密钥不可用 → 失败关闭，不退回明文比较（§0.6）。
    if (rawUid.size() != 4 && rawUid.size() != 7 && rawUid.size() != 10) return result;

    const QByteArray index = (derive ? derive : noDerive)(rawUid);
    if (index.size() != 32) return result;   // 派生失败保持 IndexRejected

    if (!database_) { result.verdict = CardMatch::Verdict::NoMatch; return result; }

    // 逐用户试开：正确 key 才能读到 card_index。
    // 错误 key 返回 26 是这里「不是这个用户」的正常信号，不是异常。
    for (const auto &entry : keys) {
        if (entry.second.size() != 32) continue;
        if (!database_->openForSession(entry.first, entry.second)) continue;
        const bool owns = database_->hasCard(index);
        database_->close();
        if (owns) {
            result.verdict = CardMatch::Verdict::Matched;
            result.userId = entry.first;
            return result;
        }
    }
    result.verdict = CardMatch::Verdict::NoMatch;
    return result;
}

bool CardIndex::bind(const QString &userId, const QByteArray &dbKey, const QByteArray &rawUid,
                     Derive derive) {
    if (rawUid.size() != 4 && rawUid.size() != 7 && rawUid.size() != 10) return false;
    const QByteArray index = (derive ? derive : noDerive)(rawUid);
    if (index.size() != 32 || !database_) return false;
    if (!database_->openForSession(userId, dbKey)) return false;
    const bool ok = database_->bindCard(index);
    database_->close();
    return ok;
}

bool CardIndex::revoke(const QString &userId, const QByteArray &dbKey, const QByteArray &rawUid,
                       Derive derive) {
    if (rawUid.size() != 4 && rawUid.size() != 7 && rawUid.size() != 10) return false;
    const QByteArray index = (derive ? derive : noDerive)(rawUid);
    if (index.size() != 32 || !database_) return false;
    if (!database_->openForSession(userId, dbKey)) return false;
    const bool ok = database_->revokeCard(index);
    database_->close();
    return ok;
}

// ---- CardBindingWindow ----

void CardBindingWindow::open(qint64 nowMs) { openedAtMs_ = nowMs; }

bool CardBindingWindow::isOpen(qint64 nowMs) const {
    if (openedAtMs_ < 0) return false;
    return nowMs - openedAtMs_ < kWindowMs;
}

bool CardBindingWindow::consume(qint64 nowMs) {
    // 单次：即使窗口未到期，用过一次也要关，防止连绑多张。
    if (!isOpen(nowMs)) return false;
    openedAtMs_ = -1;
    return true;
}

}  // namespace security
