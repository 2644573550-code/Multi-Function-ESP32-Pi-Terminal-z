#include "core/NetworkGate.h"

void NetworkGate::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    // 开关状态变化时清掉上一次的拒绝原因，避免显示过期文案。
    reason_.clear();
    emit changed();
}

bool NetworkGate::allow(const QString &purpose) {
    if (enabled_) {
        if (!reason_.isEmpty()) { reason_.clear(); emit changed(); }
        return true;
    }
    const QString next = QStringLiteral("network disabled: ") + purpose;
    if (reason_ != next) { reason_ = next; emit changed(); }
    return false;
}
