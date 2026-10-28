#pragma once

#include <QObject>
#include <QString>

// 有限网络策略（报告 §9.3 第 5、6 条）。
//
// 报告要求：采用有限超时、有上限重试、用户主动重试、流量/缓存上限，
// 且**不因网络恢复自动下载大包**。
//
// 本类只做策略计算，不发请求：
//   - 给调用方一个超时值与重试预算
//   - 记录已用流量，超出预算时拒绝新的下载
//   - 区分"自动"与"用户主动"：自动重试有严格上限，主动重试重置预算
class FetchBudget : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int remainingRetries READ remainingRetries NOTIFY changed)
    Q_PROPERTY(qint64 usedBytes READ usedBytes NOTIFY changed)
    Q_PROPERTY(qint64 byteBudget READ byteBudget NOTIFY changed)

public:
    explicit FetchBudget(QObject *parent = nullptr) : QObject(parent) {}

    // ---- 超时与重试 ----
    // 单次请求超时（毫秒）。报告未给数值，取 15s：够慢网握手，又不至于挂死。
    int timeoutMs() const { return 15000; }
    // 自动重试上限（次数）。超过后必须由用户主动触发。
    int maxAutoRetries() const { return 2; }
    int remainingRetries() const { return remaining_; }

    // 消耗一次自动重试。返回 false 表示预算已用尽，调用方必须停下来
    // 等用户主动重试，不能自己继续循环。
    bool consumeAutoRetry();

    // 用户主动重试：重置自动重试预算（这是唯一的重置入口）。
    void userRetry();

    // ---- 流量/缓存上限 ----
    // 单次下载的字节上限。超过即拒绝，不截断。
    qint64 maxSingleDownloadBytes() const { return 32LL * 1024 * 1024; }
    // 累计预算：用于缓存/更新场景，避免反复拉大包。
    qint64 byteBudget() const { return byteBudget_; }
    void setByteBudget(qint64 bytes);
    qint64 usedBytes() const { return used_; }

    // 记账。返回 false 表示超出预算，调用方应中止。
    bool recordBytes(qint64 bytes);
    void resetUsage();

    // ---- 自动 vs 主动 ----
    // 报告 §9.3 第 6 条：网络恢复**不得**自动开始大包下载。
    // 调用方在下大包前必须问这个；返回 false 时只能等用户点击。
    bool autoDownloadAllowed(qint64 size, bool networkJustRecovered) const;

signals:
    void changed();

private:
    int remaining_ = 2;      // 与 maxAutoRetries 一致，构造时赋值见下
    qint64 byteBudget_ = 128LL * 1024 * 1024;
    qint64 used_ = 0;
};
