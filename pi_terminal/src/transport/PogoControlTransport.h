#pragma once

#include "TransportBase.h"
#include "protocol/pbr_aead.h"
#include "protocol/pbr_session.h"

#include <QHash>
#include <QSerialPort>
#include <QTimer>

class PogoControlTransport : public TransportBase {
    Q_OBJECT
public:
    explicit PogoControlTransport(QObject *parent = nullptr);
    bool open(const QString &device, qint32 baud = 115200);
    bool sendControl(const QJsonObject &event) override { return sendCommand(event); }
    void close() override;
    bool sendCommand(const QJsonObject &command, quint32 *sequenceOut = nullptr);
    bool sendRawControl(quint8 command, const QByteArray &payload);

    // 认证密钥（握手完成后由会话注入）。未设置时只接受明文 hello，
    // 收到 AEAD 信封一律拒绝 —— 而不是当作 JSON 扫描后静默丢弃。
    // §6.1：未认证期不接受业务/诊断帧。
    void setSessionKeys(const QByteArray &bridgeToPiKey32,
                        const QByteArray &noncePrefix4,
                        const QByteArray &piKeyId);
    // 已认证状态。只有握手成功后才会置 true。
    bool authenticated() const { return authenticated_; }
    void setAuthenticated(bool value) { authenticated_ = value; }
private slots:
    void readAvailable();
private:
    QSerialPort serial_;
    QByteArray buffer_;
    quint32 sequence_ = 1;
    quint32 lastReceiveSequence_ = 0;
    bool haveReceiveSequence_ = false;
    QTimer reconnect_, frameTimeout_;
    QString device_;
    qint32 baud_ = 115200;
    bool enabled_ = false;

    // §5 / §6.2：按方向维护的接收序号；只在 tag 通过后推进。
    QByteArray bridgeToPiKey_;
    QByteArray noncePrefix_;
    quint8 piKeyId_ = 0;
    bool authenticated_ = false;
    ReplayGuard replay_;
    EventDedup dedup_;
};
