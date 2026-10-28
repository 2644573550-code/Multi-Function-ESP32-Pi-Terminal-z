#pragma once

#include <QObject>
#include <QString>

// 电池/供电状态（报告 §13 + §3.2「失联即未知」）。
//
// P0 没有桥板遥测来源，因此这里**只做占位与语义**：
//   - 供电来源默认 unknown，绝不猜成"电池"或"外接"
//   - 电量未知时不得自动关机，也不显示假的百分比
//   - 桥板上报接口就位后，由 ESP 侧填 chargePercent / source
//
// 报告原文：电池电量计不能替代 USB/外部供电检测；无可靠检测接线时，
// 供电来源应保持 unknown。
namespace Power {

enum class Source {
    Unknown,      // 默认：没有可靠检测
    External,     // 明确检测到外部供电
    Battery,      // 明确检测到仅电池
};

enum class ChargeState {
    Unknown,
    Charging,
    Discharging,
    Full,
};

struct Status {
    Source source = Source::Unknown;
    ChargeState charge = ChargeState::Unknown;
    bool hasPercent = false;   // false 时 percent 无意义
    int percent = 0;

    static QString sourceName(Source value);
    static QString chargeName(ChargeState value);
    // 是否允许基于电量触发关机。未知时一律 false。
    bool mayAutoShutdown() const;
};

// Pi 侧只读视图。数据来源未接入时全部保持 unknown。
class PowerStatus : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ sourceName NOTIFY changed)
    Q_PROPERTY(QString charge READ chargeName NOTIFY changed)
    Q_PROPERTY(bool known READ known NOTIFY changed)
    Q_PROPERTY(int percent READ percent NOTIFY changed)

public:
    explicit PowerStatus(QObject *parent = nullptr) : QObject(parent) {}

    const Status &status() const { return status_; }
    // 遥测未来接入时由 ESP 侧数据填；现在只允许置为 unknown。
    void update(const Status &next);

    QString sourceName() const { return Status::sourceName(status_.source); }
    QString chargeName() const { return Status::chargeName(status_.charge); }
    bool known() const { return status_.source != Source::Unknown; }
    int percent() const { return status_.hasPercent ? status_.percent : -1; }

signals:
    void changed();

private:
    Status status_;
};

}  // namespace Power
