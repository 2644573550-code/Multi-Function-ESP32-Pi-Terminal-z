#include "HotspotLanTransport.h"
#include <QtEndian>
#include "protocol/data_checksum.h"

HotspotLanTransport::HotspotLanTransport(QObject *parent) : TransportBase(parent) {
    timer_.setSingleShot(true); timer_.setInterval(10000);
    connect(&timer_, &QTimer::timeout, this, [this] {
        socket_.abort(); buffer_.clear();
        emit error("TLS partial-frame timeout");
    });

    // 自签证书不在任何 CA 链上，VerifyPeer 必然失败；信任靠握手后比对指纹。
    // 因此这里只关掉链校验，**不关掉**指纹比对（OnEncrypted 里做）。
    socket_.setPeerVerifyMode(QSslSocket::VerifyNone);
    socket_.setProtocol(QSsl::TlsV1_2OrLater);

    connect(&socket_, &QSslSocket::encrypted, this, &HotspotLanTransport::onEncrypted);
    connect(&socket_, &QSslSocket::bytesWritten, this, [this](qint64) { flushWrites(); });
    connect(&socket_, &QSslSocket::disconnected, this, [this] {
        timer_.stop(); buffer_.clear(); pinnedVerified_ = false;
        emit disconnected("TLS disconnected");
    });
    connect(&socket_, &QSslSocket::errorOccurred, this, [this](auto) {
        // 握手阶段出错时，QSslSocket 也会走这里；保持失败关闭。
        emit error(socket_.errorString());
    });
    connect(&socket_, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        // VerifyNone 下只应出现自签/未知 CA 两类；记录但不忽略真正的协议错误。
        for (const QSslError &sslError : errors) {
            if (sslError.error() != QSslError::SelfSignedCertificate &&
                sslError.error() != QSslError::UnableToGetLocalIssuerCertificate &&
                sslError.error() != QSslError::UnableToVerifyFirstCertificate &&
                sslError.error() != QSslError::HostNameMismatch) {
                emit error(QStringLiteral("TLS: ") + sslError.errorString());
                socket_.abort();
                return;
            }
        }
    });
    connect(&socket_, &QSslSocket::readyRead, this, [this] {
        if (!pinnedVerified_) {
            // 未完成身份校验前不得处理任何业务字节。
            socket_.readAll();
            emit error("TLS data before pinning verified");
            close();
            return;
        }
        buffer_ += socket_.readAll();
        if (buffer_.size() > 2 * 1048576) { close(); emit error("TLS receive limit"); return; }
        while (buffer_.size() >= 4) {
            const auto n = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(buffer_.constData()));
            if (n > 1048576 || n < 4) { close(); emit error("TLS invalid length"); return; }
            if (buffer_.size() < n + 4) break;
            const auto block = buffer_.mid(4, n); buffer_.remove(0, n + 4);
            auto payload = block.chopped(4);
            if (dataChecksum(payload) != block.right(4)) { close(); emit error("TLS checksum mismatch"); return; }
            emit payloadReceived(2, payload);
        }
        if (buffer_.isEmpty()) timer_.stop(); else timer_.start();
    });
}

QByteArray HotspotLanTransport::fingerprintOf(const QSslCertificate &certificate) {
    return certificate.digest(QCryptographicHash::Sha256);
}

QString HotspotLanTransport::commonNameOf(const QSslCertificate &certificate) {
    for (const auto &entry : certificate.subjectInfo(QSslCertificate::CommonName)) {
        if (!entry.trimmed().isEmpty()) return entry.trimmed();
    }
    return {};
}

void HotspotLanTransport::onEncrypted() {
    // §0.8.1：先 VerifyNone 完成握手，随后立刻比对指纹。
    const QSslCertificate peer = socket_.peerCertificate();
    if (peer.isNull()) {
        emit identityRejected("peer certificate missing");
        close();
        return;
    }
    if (pinnedFingerprint_.size() != 32) {
        emit identityRejected("no pinned fingerprint configured");
        close();
        return;
    }
    const QByteArray actual = fingerprintOf(peer);
    if (actual.size() != 32 || actual != pinnedFingerprint_) {
        emit identityRejected("certificate fingerprint mismatch");
        close();
        return;
    }
    if (!expectedBridgeId_.isEmpty()) {
        // 证书 CN 必须是 <module_id>；这里复用 bridge_id 字段做设备标识比对。
        const QString cn = commonNameOf(peer);
        if (cn.isEmpty() || cn != expectedBridgeId_) {
            emit identityRejected("certificate CN mismatch");
            close();
            return;
        }
    }
    pinnedVerified_ = true;
    emit handshakeSecured();
    emit connected();
    flushWrites();
}

QString HotspotLanTransport::negotiatedProtocol() const {
    if (!pinnedVerified_) return {};
    switch (socket_.sessionProtocol()) {
    case QSsl::TlsV1_2: return QStringLiteral("TLS1.2");
    case QSsl::TlsV1_3: return QStringLiteral("TLS1.3");
    default: return QStringLiteral("TLS");
    }
}

bool HotspotLanTransport::connectToModule(const QHostAddress &address, quint16 port,
                                          const QByteArray &pinnedFingerprint,
                                          const QString &expectedBridgeId) {
    close();
    if (address.isNull() || port == 0) return false;
    // 没有指纹就不连：空指纹等于不校验，必须失败关闭。
    if (pinnedFingerprint.size() != 32) {
        emit error("LAN requires a pinned certificate fingerprint");
        return false;
    }
    pinnedFingerprint_ = pinnedFingerprint;
    expectedBridgeId_ = expectedBridgeId;
    pinnedVerified_ = false;
    socket_.connectToHostEncrypted(address.toString(), port);
    return true;
}

bool HotspotLanTransport::send(const QByteArray &payload) {
    if (!pinnedVerified_ || socket_.state() != QAbstractSocket::ConnectedState ||
        payload.size() > 1048572 || outgoing_.size() > 2097152) return false;
    QByteArray wire(4, '\0');
    qToBigEndian(quint32(payload.size() + 4), reinterpret_cast<uchar *>(wire.data()));
    wire += payload; wire += dataChecksum(payload);
    if (outgoing_.size() + wire.size() > 2097152) return false;
    outgoing_ += wire;
    flushWrites();
    return true;
}

void HotspotLanTransport::flushWrites() {
    if (outgoing_.isEmpty() || socket_.state() != QAbstractSocket::ConnectedState) return;
    const qint64 written = socket_.write(outgoing_);
    if (written < 0) { emit error(socket_.errorString()); return; }
    if (written > 0) outgoing_.remove(0, int(written));
}

void HotspotLanTransport::close() {
    timer_.stop();
    socket_.abort();
    buffer_.clear();
    outgoing_.clear();
    pinnedVerified_ = false;
}
