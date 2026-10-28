#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

// 业务 lane 的热切换门控（§0.8.5，Z 负责状态机，传输层归 D）。
//
// 规则：
//   - LAN 断开后允许 BLE 接手，反之亦然
//   - 每次切换必须重新认证，不得搬用旧会话密钥
//   - 单次 attach 内最多切换 3 次，超出 attach 失败
//   - 切换时丢弃未完成分片，不重传
//   - 切换事件记入日志（不含密钥）
//
// 本类不碰 BleTransport/HotspotLanTransport —— 那两个文件属 D。
class LaneGate : public QObject {
    Q_OBJECT
public:
    enum class Lane { None, Ble, Lan };
    enum class Verdict {
        Accepted,           // 可切换，且已完成重新认证
        RejectedNoSession,  // 没有活动会话，不接受业务数据
        RejectedUnbound,    // 会话未绑定用户/能力
        RejectedTooMany,    // 超出单次 attach 的 3 次上限
        RejectedNotAuthed,  // 目标 lane 尚未完成认证
    };

    explicit LaneGate(QObject *parent = nullptr);

    // attach 开始：重置切换计数。绑定用户后才有资格收发业务数据。
    void beginAttach(const QString &userId, const QString &instance);

    Lane current() const { return current_; }
    int switchCount() const { return switchCount_; }
    bool attached() const { return !userId_.isEmpty(); }

    // lane 声称已认证（指纹比对通过 / 挑战-响应通过）。
    // 认证结果本身由传输层给，本类只记状态。
    void markAuthenticated(Lane lane);
    // lane 失效（断线/撤权）。旧认证立即作废，必须重新认证。
    void dropLane(Lane lane);

    // 请求切到 lane。必须：attached + 目标 lane 已重新认证 + 未超 3 次。
    Verdict requestSwitch(Lane lane);

    // 撤权/切用户：所有 lane 认证作废，业务数据进不来。
    void invalidateAll(const QString &reason);

    // 单条业务消息上限 512 字节（§0.8.4）。分片由 BleTransport 内部做。
    static constexpr int kMaxBusinessPayload = 512;
    static bool payloadAcceptable(int bytes) {
        return bytes >= 0 && bytes <= kMaxBusinessPayload;
    }

    QString lastReason() const { return lastReason_; }

signals:
    // D 侧据此断开连接、销毁派生会话密钥。
    void switchRequired(int lane);
    void invalidated(QString reason);

private:
    Lane current_ = Lane::None;
    Lane authenticated_ = Lane::None;
    QString userId_;
    QString instance_;
    QString lastReason_;
    int switchCount_ = 0;
    static constexpr int kMaxSwitches = 3;
};
