#include "core/NetworkGate.h"

void NetworkGate::clearReason() {
    if (reason_.isEmpty()) return;
    reason_.clear();
    emit changed();
}

void NetworkGate::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    reason_.clear();
    // §9.3：关闭时取消进行中的外网请求。
    if (!enabled_) inFlight_ = 0;
    emit changed();
}

void NetworkGate::setBrowserAllowed(bool allowed) {
    if (browserAllowed_ == allowed) return;
    browserAllowed_ = allowed;
    emit changed();
}

void NetworkGate::setMusicAllowed(bool allowed) {
    if (musicAllowed_ == allowed) return;
    musicAllowed_ = allowed;
    emit changed();
}

void NetworkGate::setUpdatesAllowed(bool allowed) {
    if (updatesAllowed_ == allowed) return;
    updatesAllowed_ = allowed;
    emit changed();
}

void NetworkGate::setInterfaceReady(bool ready) {
    if (interfaceReady_ == ready) return;
    interfaceReady_ = ready;
    emit stateChanged();
}

void NetworkGate::setModuleLinkReady(bool ready) {
    if (moduleLinkReady_ == ready) return;
    moduleLinkReady_ = ready;
    emit stateChanged();
}

void NetworkGate::setInternetReachable(bool reachable) {
    if (internetReachable_ == reachable) return;
    internetReachable_ = reachable;
    emit stateChanged();
}

bool NetworkGate::allow(Purpose purpose) {
    // 总开关关闭：一律拒绝，不分用途。
    if (!enabled_) {
        static const char *names[] = {"browser", "music", "updates", "timesync", "other"};
        reason_ = QStringLiteral("network disabled: ") + QLatin1String(names[int(purpose)]);
        emit changed();
        return false;
    }
    // 总开关打开后按分功能开关判定。
    bool allowed = true;
    QString name;
    switch (purpose) {
    case Purpose::Browser: allowed = browserAllowed_; name = QStringLiteral("browser"); break;
    case Purpose::Music: allowed = musicAllowed_; name = QStringLiteral("music"); break;
    case Purpose::Updates: allowed = updatesAllowed_; name = QStringLiteral("updates"); break;
    case Purpose::TimeSync: allowed = true; name = QStringLiteral("timesync"); break;
    case Purpose::Other: allowed = true; name = QStringLiteral("other"); break;
    }
    if (!allowed) {
        reason_ = QStringLiteral("feature disabled: ") + name;
        emit changed();
        return false;
    }
    clearReason();
    return true;
}

bool NetworkGate::allow(const QString &purpose) {
    // 字符串入口：旧调用方与 QML 用。未知用途按 Other 处理。
    if (purpose.compare(QLatin1String("browser"), Qt::CaseInsensitive) == 0)
        return allow(Purpose::Browser);
    if (purpose.compare(QLatin1String("music"), Qt::CaseInsensitive) == 0)
        return allow(Purpose::Music);
    if (purpose.compare(QLatin1String("updates"), Qt::CaseInsensitive) == 0)
        return allow(Purpose::Updates);
    if (purpose.compare(QLatin1String("timesync"), Qt::CaseInsensitive) == 0)
        return allow(Purpose::TimeSync);
    return allow(Purpose::Other);
}

void NetworkGate::beginRequest() {
    ++inFlight_;
    emit changed();
}

void NetworkGate::endRequest() {
    if (inFlight_ > 0) --inFlight_;
    emit changed();
}
