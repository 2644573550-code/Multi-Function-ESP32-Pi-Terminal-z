#pragma once

#include <QObject>

// 联网总开关（报告 §9.3「联网总开关与有限网络」）。
//
// P0 的产品栈里模块 worker 已经是 PrivateNetwork=yes（无公网），
// 因此本开关在 P0 的作用是**主机侧**的总闸：
//   - Off：终端自身也不主动发起外部请求（音乐 API、校时、浏览器除外）
//   - On ：允许受控联网能力
//
// 关键约束（§3.2「拒绝即关闭」）：
//   - 默认 Off，且必须持久化；重启后保持用户的关闭决定
//   - 关闭时必须能解释"为什么某个请求被拒"，而不是静默失败
class NetworkGate : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)

public:
    explicit NetworkGate(QObject *parent = nullptr) : QObject(parent) {}

    bool enabled() const { return enabled_; }
    QString reason() const { return reason_; }

    // 只有显式开启才允许；默认关闭。
    void setEnabled(bool enabled);
    // QML 入口：属性本身只读，写操作经这个方法（便于将来加审计/二次确认）。
    Q_INVOKABLE void setEnabledFromUi(bool enabled) { setEnabled(enabled); }

    // 请求放行一次。返回 false 时 reason() 给出可读原因。
    // purpose 是调用方标识（如 "music.api"/"timesync"），只用于原因文案。
    bool allow(const QString &purpose);

signals:
    void changed();

private:
    bool enabled_ = false;
    QString reason_;
};
