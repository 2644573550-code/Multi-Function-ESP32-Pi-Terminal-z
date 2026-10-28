#pragma once
#include "TransportBase.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QTimer>

#include <QByteArray>

// 业务数据面：BLE（§0.8.2）。
//
// 两层认证，缺一不可：
//   1) 链路层必须已配对（BlueZ pairingStatus == Paired），否则不写 feature；
//   2) 应用层挑战-响应：连接后先发 32 字节 challenge，模块回
//      HMAC-SHA256(k, challenge || module_id || boot_id)；**通过前不发送
//      任何业务数据**。密钥 k 由 T-03 配对时经 LAN 下发写入模块 NVS。
//
// 单条业务 payload ≤512 字节（§0.8.4），内部按 20 字节分片，对上层透明。
class BleTransport : public TransportBase {
    Q_OBJECT
public:
    explicit BleTransport(QObject *parent = nullptr);
    void start(const QJsonObject &config);
    bool sendData(const QByteArray &payload) override { return send(payload); }
    void close() override;
    bool send(const QByteArray &payload);

    // 应用层认证是否已通过；未通过前 sendData() 一律返回 false。
    bool authenticated() const { return authenticated_; }
    QString lastAuthError() const { return lastAuthError_; }

    // 供测试注入固定 challenge，避免随机性使断言不稳定。
    void setChallengeForTest(const QByteArray &challenge);

signals:
    // 挑战-响应失败（含 3 次重试上限、未配对、密钥缺失）。
    void authenticationFailed(QString reason);
    void authenticationSucceeded();

private:
    void found(const QBluetoothDeviceInfo &info);
    void receive(const QByteArray &fragment);
    void beginChallenge();
    void failAuth(const QString &reason);
    // 认证帧走同一分片通道，但不受 512 字节业务上限约束。
    bool sendRaw(const QByteArray &payload);
    static QByteArray hmacResponse(const QByteArray &key, const QByteArray &challenge,
                                   const QString &moduleId, const QString &bootId);

    QBluetoothDeviceDiscoveryAgent discovery_;
    QLowEnergyController *controller_ = nullptr;
    QLowEnergyService *service_ = nullptr;
    QLowEnergyCharacteristic tx_, rx_;
    QJsonObject config_;
    QTimer timer_, pump_;
    QTimer discoveryTimeout_;
    QList<QByteArray> pending_;
    QTimer writeTimeout_;
    bool writeInFlight_ = false;
    int writeFailures_ = 0;
    QByteArray assembling_;
    quint16 sendId_ = 0, receiveId_ = 0, next_ = 0, count_ = 0;
    bool ready_ = false;

    // 应用层认证状态
    bool authenticated_ = false;
    int authAttempts_ = 0;
    QByteArray challenge_;
    QByteArray sessionKey_;      // 来自配对下发，只在内存
    QString moduleId_;
    QString bootId_;
    QString lastAuthError_;
    static constexpr int kMaxAuthAttempts = 3;
    static constexpr int kMaxBusinessBytes = 512;
};
