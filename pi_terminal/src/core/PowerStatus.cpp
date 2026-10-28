#include "core/PowerStatus.h"

namespace Power {

QString Status::sourceName(Source value) {
    switch (value) {
    case Source::External: return QStringLiteral("external");
    case Source::Battery: return QStringLiteral("battery");
    case Source::Unknown: break;
    }
    return QStringLiteral("unknown");
}

QString Status::chargeName(ChargeState value) {
    switch (value) {
    case ChargeState::Charging: return QStringLiteral("charging");
    case ChargeState::Discharging: return QStringLiteral("discharging");
    case ChargeState::Full: return QStringLiteral("full");
    case ChargeState::Unknown: break;
    }
    return QStringLiteral("unknown");
}

bool Status::mayAutoShutdown() const {
    // §3.2：失联即未知，不能因未知电量自动关机。
    // 即使知道电量，也只有明确的电池供电且百分比可信时才允许。
    if (source != Source::Battery) return false;
    if (!hasPercent) return false;
    if (percent < 0 || percent > 100) return false;
    return true;
}

void PowerStatus::update(const Status &next) {
    // 没有真实来源时拒绝把 unknown 变成具体值：那等于伪造遥测。
    if (next.source != Source::Unknown && next.source == status_.source
        && next.percent == status_.percent && next.charge == status_.charge)
        return;

    status_ = next;
    // 百分比越界视为无效，不显示假数字。
    if (status_.hasPercent && (status_.percent < 0 || status_.percent > 100))
        status_.hasPercent = false;
    emit changed();
}

}  // namespace Power
