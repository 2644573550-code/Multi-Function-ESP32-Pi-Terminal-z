#include "pbr_session.h"

ReplayGuard::ReplayGuard(quint64 maxAdvance) : maxAdvance_(maxAdvance) {}

ReplayGuard::Verdict ReplayGuard::check(quint64 authSeq) const {
    // §5.2：auth_seq 从 1 开始，0 一律 REPLAY。
    if (authSeq == 0) return Verdict::ZeroSequence;
    if (authSeq <= last_) return Verdict::Replayed;
    // 一次跳跃过大：不是正常帧序，按异常处理（防止构造超大序号把窗口推高）。
    if (authSeq - last_ > maxAdvance_) return Verdict::TooFarAhead;
    return Verdict::Accepted;
}

void ReplayGuard::advance(quint64 authSeq) {
    if (authSeq > last_) last_ = authSeq;
}

bool ReplayGuard::needsRehandshake() const {
    // 禁止环绕：余量不足时主动重新握手。
    return last_ >= (0xffffffffffffffffULL - kRehandshakeMargin);
}

void ReplayGuard::reset() { last_ = 0; }

EventDedup::EventDedup(int capacity) : capacity_(capacity > 0 ? capacity : 4096) {}

EventDedup::Verdict EventDedup::check(const QString &dedupKey) {
    if (dedupKey.isEmpty()) return Verdict::FirstTime;
    if (seen_.contains(dedupKey)) return Verdict::Duplicate;
    // 表满时不淘汰旧条目：静默遗忘会让重放的旧事件重新变成"首次"。
    // 规范要求此时重建会话（§6.2 第 7 步）。
    if (seen_.size() >= capacity_) return Verdict::TableFull;
    seen_.insert(dedupKey);
    return Verdict::FirstTime;
}

void EventDedup::clear() { seen_.clear(); }

namespace EventTtl {

Verdict evaluate(qint64 ttlMs, qint64 senderUptimeMs, qint64 receiverUptimeMs,
                 bool hasBothClocks) {
    if (ttlMs != kExpectedTtlMs) return Verdict::BadTtlField;
    if (!hasBothClocks) return Verdict::MissingClock;
    // 用对端 uptime 与本地 uptime 的差估算事件年龄。
    // 双方 uptime 起点不同，因此只在同一会话内比较增量，不做绝对对齐。
    const qint64 age = receiverUptimeMs - senderUptimeMs;
    if (age > kMaxAgeMs) return Verdict::Expired;
    return Verdict::Ok;
}

}  // namespace EventTtl
