#pragma once
#include <QObject>
#include <QTimer>

// Tracks the USB control connection, independently of module LAN/BLE sessions.
class BridgeConnectionMonitor : public QObject {
    Q_OBJECT
public:
    explicit BridgeConnectionMonitor(QObject *parent = nullptr, int delayMs = 5000) : QObject(parent) {
        timer_.setSingleShot(true); timer_.setInterval(delayMs);
        connect(&timer_, &QTimer::timeout, this, [this] {
            if (enabled_ && !connected_ && !warned_) { warned_ = true; emit unavailable(); }
        });
    }
    bool connected() const { return connected_; }
    bool enabled() const { return enabled_; }
    void enable() { enabled_ = true; setConnected(false); }
    void setConnected(bool value) {
        const bool changed = connected_ != value;
        connected_ = value;
        if (value) { timer_.stop(); warned_ = false; }
        else if (enabled_ && !warned_ && !timer_.isActive()) timer_.start();
        if (changed) emit connectionChanged(value);
    }
signals:
    void connectionChanged(bool connected);
    void unavailable();
private:
    QTimer timer_;
    bool enabled_ = false, connected_ = false, warned_ = false;
};
