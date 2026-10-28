#include "core/LaneGate.h"

#include <QDebug>

LaneGate::LaneGate(QObject *parent) : QObject(parent) {}

void LaneGate::beginAttach(const QString &userId, const QString &instance) {
    // 新 attach 重置切换计数；旧的认证状态一律作废。
    userId_ = userId;
    instance_ = instance;
    current_ = Lane::None;
    authenticated_ = Lane::None;
    switchCount_ = 0;
    lastReason_.clear();
}

void LaneGate::markAuthenticated(Lane lane) {
    if (lane == Lane::None) return;
    authenticated_ = lane;
}

void LaneGate::dropLane(Lane lane) {
    if (lane == Lane::None) return;
    // 断线即撤认证：下一次必须重新认证，不得搬用旧会话密钥。
    if (authenticated_ == lane) authenticated_ = Lane::None;
    if (current_ == lane) current_ = Lane::None;
}

LaneGate::Verdict LaneGate::requestSwitch(Lane lane) {
    if (!attached()) {
        lastReason_ = QStringLiteral("no active session");
        return Verdict::RejectedNoSession;
    }
    if (lane == Lane::None) {
        lastReason_ = QStringLiteral("no target lane");
        return Verdict::RejectedUnbound;
    }
    if (authenticated_ != lane) {
        // 关键：切过去之前必须重新认证。搬用旧密钥不算。
        lastReason_ = QStringLiteral("target lane not re-authenticated");
        return Verdict::RejectedNotAuthed;
    }
    if (current_ == lane) return Verdict::Accepted;      // 同 lane 不算切换
    if (switchCount_ >= kMaxSwitches) {
        lastReason_ = QStringLiteral("attach switch limit reached");
        return Verdict::RejectedTooMany;
    }
    ++switchCount_;
    current_ = lane;
    // 未完成分片不重传：由传输层在收到本信号后丢弃。
    emit switchRequired(static_cast<int>(lane));
    qInfo() << "business lane switched to" << (lane == Lane::Ble ? "ble" : "hotspot_lan")
            << "count" << switchCount_;
    return Verdict::Accepted;
}

void LaneGate::invalidateAll(const QString &reason) {
    authenticated_ = Lane::None;
    current_ = Lane::None;
    lastReason_ = reason;
    qInfo().noquote() << "business lanes invalidated:" << reason;
    emit invalidated(reason);
}
