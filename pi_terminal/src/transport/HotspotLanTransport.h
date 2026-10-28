#pragma once
#include "TransportBase.h"

#include <QSslSocket>
#include <QSslError>
#include <QHostAddress>
#include <QTimer>

// LAN 业务数据面（§0.8.1）。
//
// 方向固定：模块监听、Pi 主动连。模块用自签证书，Pi 先以 VerifyNone 完成
// 握手，**随后立刻**比对 peerCertificate().digest(Sha256) 与 TrustStore 里
// 登记的指纹；不一致立即断开。握手失败或指纹不符一律失败关闭，
// **不存在明文 TCP 回退**。
class HotspotLanTransport : public TransportBase {
    Q_OBJECT
public:
    explicit HotspotLanTransport(QObject *parent = nullptr);

    // pinnedFingerprint：SHA-256(证书 DER) 的 32 字节；为空则拒绝连接
    // （不能"没配指纹就连"，那等于不校验）。
    // expectedModuleId：非空时还要比对证书 CN 与 module_id（§0.8.1）。
    bool connectToModule(const QHostAddress &address, quint16 port,
                         const QByteArray &pinnedFingerprint,
                         const QString &expectedModuleId = QString());

    bool send(const QByteArray &payload);
    bool sendData(const QByteArray &payload) override { return send(payload); }
    void close() override;

    // 最近一次握手是否已完成指纹校验（测试与诊断用）。
    bool pinnedVerified() const { return pinnedVerified_; }
    // 已建立的 TLS 版本，便于日志与验收取证。
    QString negotiatedProtocol() const;

signals:
    // 握手完成但指纹不符：此时必须断开，绝不允许继续收发业务数据。
    void identityRejected(QString reason);
    void handshakeSecured();

private:
    void onEncrypted();
    void flushWrites();
    static QByteArray fingerprintOf(const QSslCertificate &certificate);
    static QString commonNameOf(const QSslCertificate &certificate);

    QSslSocket socket_;
    QByteArray buffer_;
    QByteArray outgoing_;
    QTimer timer_;
    QByteArray pinnedFingerprint_;
    QString expectedModuleId_;
    bool pinnedVerified_ = false;
};
