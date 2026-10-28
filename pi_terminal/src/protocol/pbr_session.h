#pragma once

#include <QHash>
#include <QSet>
#include <QString>

// PBR secure v1 的序号与事件去重（§5.2 / §6.2 第 5、7 步）。
//
// 关键约束：**tag 必须先验，序号才可信**。本类只提供状态，
// 调用顺序由调用方保证：parseEnvelope → 验 tag → checkAndAdvanceSeq。
// 若顺序写反，攻击者无需密钥就能用大序号推高窗口（未认证 DoS）。
class ReplayGuard
{
public:
    enum class Verdict {
        Accepted,
        ZeroSequence,    // auth_seq == 0，规范禁止
        Replayed,        // auth_seq <= 上次，或环形耗尽
        TooFarAhead,     // 跳跃过大，按异常处理（默认上限 1<<20）
    };

    // 每个方向各一个实例：Pi→桥、桥→Pi 独立计数。
    explicit ReplayGuard(quint64 maxAdvance = 1u << 20);

    // 单向检查：先查不改。tag 通过后再调 advance()。
    Verdict check(quint64 authSeq) const;

    // 记录已接受的序号。只有 check() 返回 Accepted 时才应调用。
    void advance(quint64 authSeq);

    quint64 lastAccepted() const { return last_; }
    // 规范要求「即将到达 2^64-1 时主动重新握手，禁止环绕」。
    bool needsRehandshake() const;
    void reset();

private:
    quint64 last_ = 0;
    quint64 maxAdvance_;
    static constexpr quint64 kRehandshakeMargin = 1u << 20;
};

// 事件去重（§6.2 第 7 步 / §6.4）。
//
// 去重键 = bridge_id + boot_id + session_id + event_id（f-day1 §4.2）。
// 表满时必须重建会话而不是静默丢事件 —— 静默丢会让用户看到"数据不再更新"。
class EventDedup
{
public:
    enum class Verdict {
        FirstTime,   // 首次出现，交给业务分派
        Duplicate,   // 已处理过，回 result=duplicate，不重复分派
        TableFull,   // 达到容量上限，调用方必须重建会话
    };

    explicit EventDedup(int capacity = 4096);

    Verdict check(const QString &dedupKey);
    int size() const { return seen_.size(); }
    int capacity() const { return capacity_; }
    void clear();

private:
    QSet<QString> seen_;
    // 用插入顺序队列做淘汰比 QSet 直接 clear 更贴近"有限窗口"，
    // 但 P0 的语义是"满了就重建会话"，所以这里只计数不淘汰。
    int capacity_;
};

// 事件 TTL（§6.4）：ttl_ms 必须等于 3000，且按对端时钟判定未过期。
namespace EventTtl {
constexpr int kExpectedTtlMs = 3000;
// 允许的时钟偏差上限；超过判 EVENT_EXPIRED。
constexpr int kMaxAgeMs = 3000;

enum class Verdict { Ok, BadTtlField, Expired, MissingClock };

Verdict evaluate(qint64 ttlMs, qint64 senderUptimeMs, qint64 receiverUptimeMs,
                 bool hasBothClocks);
}  // namespace EventTtl
