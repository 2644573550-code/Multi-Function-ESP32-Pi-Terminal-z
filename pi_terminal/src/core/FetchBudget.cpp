#include "core/FetchBudget.h"

bool FetchBudget::consumeAutoRetry() {
    if (remaining_ <= 0) return false;
    --remaining_;
    emit changed();
    return true;
}

void FetchBudget::userRetry() {
    // 只有用户主动重试才重置预算：这是"不会自己无限重试"的关键。
    remaining_ = maxAutoRetries();
    emit changed();
}

void FetchBudget::setByteBudget(qint64 bytes) {
    const qint64 next = bytes > 0 ? bytes : 0;
    if (byteBudget_ == next) return;
    byteBudget_ = next;
    emit changed();
}

bool FetchBudget::recordBytes(qint64 bytes) {
    if (bytes <= 0) return true;
    // 单次超限直接拒绝，不记账也不截断。
    if (bytes > maxSingleDownloadBytes()) return false;
    if (used_ + bytes > byteBudget_) return false;
    used_ += bytes;
    emit changed();
    return true;
}

void FetchBudget::resetUsage() {
    if (used_ == 0) return;
    used_ = 0;
    emit changed();
}

bool FetchBudget::autoDownloadAllowed(qint64 size, bool networkJustRecovered) const {
    // 网络刚恢复：任何自动下载都不允许，必须等用户主动点击。
    if (networkJustRecovered) return false;
    // 超过单次上限的大包，永远不自动下载。
    if (size > maxSingleDownloadBytes()) return false;
    return remaining_ > 0;
}
