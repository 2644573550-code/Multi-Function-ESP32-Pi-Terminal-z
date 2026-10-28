#include "BleTransport.h"
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QtEndian>
#include "protocol/data_checksum.h"

BleTransport::BleTransport(QObject *parent) : TransportBase(parent), discovery_(this) {
    timer_.setSingleShot(true); timer_.setInterval(10000);
    connect(&timer_, &QTimer::timeout, this, [this] { assembling_.clear(); next_ = count_ = 0; });
    discoveryTimeout_.setSingleShot(true); discoveryTimeout_.setInterval(10000);
    connect(&discoveryTimeout_, &QTimer::timeout, this, [this] { if (discovery_.isActive()) discovery_.stop(); emit error("BLE device discovery timeout"); emit disconnected("BLE device not found"); });
    connect(&discovery_, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &BleTransport::found);
    connect(&discovery_, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](auto) { emit error(discovery_.errorString()); });
    pump_.setInterval(20);
    writeTimeout_.setSingleShot(true); writeTimeout_.setInterval(3000);
    connect(&writeTimeout_, &QTimer::timeout, this, [this] {
        writeInFlight_ = false; emit error("BLE write acknowledgement timeout");
        if (++writeFailures_ >= 3) { close(); emit disconnected("BLE write failed"); }
        else pump_.start();
    });
    connect(&pump_, &QTimer::timeout, this, [this] {
        if (!ready_ || pending_.isEmpty() || writeInFlight_) return;
        const auto fragment = pending_.first();
        // The ESP module exposes both Write and Write Without Response. Prefer
        // no-response writes so bidirectional notification bursts (especially
        // BLE package_get/package_chunk) cannot stall the ATT write-ack timer.
        // The pump still sends fragments in order with a 20 ms inter-fragment
        // gap; peers that lack this property retain Write With Response.
        const bool response = !tx_.properties().testFlag(QLowEnergyCharacteristic::WriteNoResponse);
        if (!service_) return;
        service_->writeCharacteristic(tx_, fragment, response ? QLowEnergyService::WriteWithResponse : QLowEnergyService::WriteWithoutResponse);
        if (!response) { pending_.removeFirst(); pump_.start(); }
        else { writeInFlight_ = true; writeTimeout_.start(); pump_.stop(); }
    });
}
void BleTransport::start(const QJsonObject &config) {
    close(); config_ = config;
    for (const auto &key : {"service_uuid", "write_uuid", "notify_uuid"})
        if (QBluetoothUuid(config.value(key).toString()).isNull()) { emit error("BLE UUID missing"); return; }
    if (config.value("device_name").toString().isEmpty()) { emit error("BLE name missing"); return; }
    if (config.value("address").toString().isEmpty()) { emit error("BLE address binding missing"); return; }

    // §0.8.2 应用层认证参数。密钥由 T-03 配对时经 LAN 下发、模块写 NVS；
    // 这里只从 profile 读 base64，不落盘、不记录。
    authenticated_ = false;
    authAttempts_ = 0;
    lastAuthError_.clear();
    moduleId_ = QString::number(config.value("module_id").toInt(-1));
    bootId_ = config.value("boot_id").toString();
    sessionKey_ = QByteArray::fromBase64(config.value("session_key").toString().toLatin1());
    if (sessionKey_.size() != 32) {
        // 没有 32 字节会话密钥 = 无法做应用层认证 → 失败关闭，不进业务状态。
        sessionKey_.clear();
        lastAuthError_ = QStringLiteral("TRANSPORT_SECURITY_REQUIRED");
        emit error("BLE session key missing (32 bytes required)");
        emit authenticationFailed(lastAuthError_);
        return;
    }

    discoveryTimeout_.start(); discovery_.start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}
void BleTransport::found(const QBluetoothDeviceInfo &info) {
    if (controller_ || info.name() != config_.value("device_name").toString()) return;
    const QString address = config_.value("address").toString();
    if (!address.isEmpty() && info.address().toString().compare(address, Qt::CaseInsensitive)) return;
    discovery_.stop(); discoveryTimeout_.stop(); controller_ = QLowEnergyController::createCentral(info, this);
    connect(controller_, &QLowEnergyController::connected, controller_, &QLowEnergyController::discoverServices);
    connect(controller_, &QLowEnergyController::errorOccurred, this, [this](auto) { emit error(controller_->errorString()); });
    connect(controller_, &QLowEnergyController::disconnected, this, [this] { ready_ = false; pump_.stop(); emit disconnected("BLE disconnected"); });
    connect(controller_, &QLowEnergyController::discoveryFinished, this, [this] {
        service_ = controller_->createServiceObject(QBluetoothUuid(config_.value("service_uuid").toString()), this);
        if (!service_) { emit error("BLE service not found"); return; }
        connect(service_, &QLowEnergyService::errorOccurred, this, [this](auto) {
            writeTimeout_.stop(); writeInFlight_ = false; emit error("BLE service operation failed");
            if (++writeFailures_ >= 3) { close(); emit disconnected("BLE write failed"); } else pump_.start();
        });
        connect(service_, &QLowEnergyService::characteristicWritten, this, [this](auto c, auto) {
            if (c != tx_) return;
            writeTimeout_.stop(); writeInFlight_ = false; writeFailures_ = 0;
            if (!pending_.isEmpty()) pending_.removeFirst();
            pump_.start();
        });
        connect(service_, &QLowEnergyService::characteristicChanged, this, [this](const auto &c, const auto &value) { if (c == rx_) receive(value); });
        connect(service_, &QLowEnergyService::descriptorWritten, this, [this](const auto &descriptor, const QByteArray &v) {
            const auto cccd = rx_.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (!ready_ && descriptor == cccd && (v == QByteArray::fromHex("0100") || v == QByteArray::fromHex("0200"))) {
                ready_ = true;
                // 链路就绪后先做应用层挑战-响应；通过前不 emit connected()
                // 也不发任何业务数据（§0.8.2）。
                beginChallenge();
            }
        });
        connect(service_, &QLowEnergyService::stateChanged, this, [this](auto state) {
            if (state != QLowEnergyService::RemoteServiceDiscovered) return;
            tx_ = service_->characteristic(QBluetoothUuid(config_.value("write_uuid").toString()));
            rx_ = service_->characteristic(QBluetoothUuid(config_.value("notify_uuid").toString()));
            const auto cccd = rx_.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (!tx_.isValid() || !rx_.isValid() || !cccd.isValid() ||
                !(rx_.properties() & (QLowEnergyCharacteristic::Notify | QLowEnergyCharacteristic::Indicate)) ||
                !(tx_.properties() & (QLowEnergyCharacteristic::Write | QLowEnergyCharacteristic::WriteNoResponse))) {
                emit error("BLE characteristics invalid"); return;
            }
            service_->writeDescriptor(cccd, rx_.properties().testFlag(QLowEnergyCharacteristic::Notify) ? QByteArray::fromHex("0100") : QByteArray::fromHex("0200"));
        });
        service_->discoverDetails();
    });
    controller_->connectToDevice();
}
QByteArray BleTransport::hmacResponse(const QByteArray &key, const QByteArray &challenge,
                                      const QString &moduleId, const QString &bootId) {
    // response = HMAC-SHA256(k, challenge || module_id || boot_id)
    QByteArray message = challenge;
    message += moduleId.toUtf8();
    message += bootId.toUtf8();
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

void BleTransport::setChallengeForTest(const QByteArray &challenge) {
    challenge_ = challenge;
}

void BleTransport::failAuth(const QString &reason) {
    lastAuthError_ = reason;
    authenticated_ = false;
    emit authenticationFailed(reason);
    // 3 次即断开；不得降级明文或跳过认证。
    if (++authAttempts_ >= kMaxAuthAttempts) {
        close();
        emit disconnected("BLE application authentication failed");
    }
}

void BleTransport::beginChallenge() {
    authenticated_ = false;
    if (sessionKey_.size() != 32) { failAuth(QStringLiteral("TRANSPORT_SECURITY_REQUIRED")); return; }
    if (challenge_.size() != 32) {
        challenge_ = QByteArray(32, Qt::Uninitialized);
        for (int i = 0; i < challenge_.size(); ++i)
            challenge_[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    // 首帧就是用 challenge 作为业务载荷发给模块；模块用 NVS 密钥回 HMAC。
    // 这一段走同一个分片通道，但 payload 以 0xB1 前缀标记为认证帧。
    QByteArray frame;
    frame.append(char(0xB1));
    frame.append(challenge_);
    if (!sendRaw(frame)) { failAuth(QStringLiteral("challenge send failed")); }
}

bool BleTransport::sendRaw(const QByteArray &payload) {
    if (!ready_ || pending_.size() + (payload.size() + 17) / 14 > 6000) return false;
    const QByteArray data = payload + dataChecksum(payload);
    const quint16 chunks = quint16((data.size() + 13) / 14); ++sendId_;
    for (quint16 i = 0; i < chunks; ++i) {
        QByteArray f(6, '\0'); auto *p = reinterpret_cast<uchar *>(f.data());
        qToLittleEndian(sendId_, p); qToLittleEndian(i, p + 2); qToLittleEndian(chunks, p + 4);
        pending_.append(f + data.mid(i * 14, 14));
    }
    pump_.start();
    return true;
}

bool BleTransport::send(const QByteArray &payload) {
    // §0.8.4：单条业务 payload ≤512 字节；超出返回失败，交给上层 INVALID_MESSAGE。
    if (payload.size() > kMaxBusinessBytes) return false;
    // §0.8.2：应用层认证未通过前不得发送任何业务数据。
    if (!authenticated_) return false;
    const QByteArray data = payload + dataChecksum(payload);
    const quint16 chunks = quint16((data.size() + 13) / 14); ++sendId_;
    for (quint16 i = 0; i < chunks; ++i) {
        QByteArray f(6, '\0'); auto *p = reinterpret_cast<uchar *>(f.data());
        qToLittleEndian(sendId_, p); qToLittleEndian(i, p + 2); qToLittleEndian(chunks, p + 4);
        pending_.append(f + data.mid(i * 14, 14));
    }
    return true;
}
void BleTransport::receive(const QByteArray &f) {
    if (f.size() < 6 || f.size() > 20) return;
    const auto *p = reinterpret_cast<const uchar *>(f.constData());
    const auto id = qFromLittleEndian<quint16>(p), index = qFromLittleEndian<quint16>(p + 2), n = qFromLittleEndian<quint16>(p + 4);
    if (!n || n > 4682 || index >= n) return;
    if (index == 0) { assembling_.clear(); receiveId_ = id; next_ = 0; count_ = n; }
    if (id != receiveId_ || index != next_ || n != count_) { assembling_.clear(); next_ = count_ = 0; return; }
    assembling_ += f.mid(6); ++next_; timer_.start();
    if (assembling_.size() > 65536) { assembling_.clear(); next_ = count_ = 0; timer_.stop(); return; }
    if (next_ == count_) {
        auto data = assembling_.chopped(4);
        if (assembling_.size() >= 4 && dataChecksum(data) == assembling_.right(4)) {
            // 认证帧：0xB1 前缀 + 32 字节 HMAC。通过后才允许业务数据。
            if (data.size() == 33 && quint8(data.at(0)) == 0xB1) {
                const QByteArray expected = hmacResponse(sessionKey_, challenge_, moduleId_, bootId_);
                const QByteArray actual = data.mid(1);
                unsigned char diff = 0;
                for (int i = 0; i < 32; ++i) diff |= static_cast<unsigned char>(expected[i] ^ actual[i]);
                if (diff != 0) {
                    failAuth(QStringLiteral("challenge response mismatch"));
                } else {
                    authenticated_ = true;
                    authAttempts_ = 0;
                    lastAuthError_.clear();
                    emit authenticationSucceeded();
                    emit connected();
                    pump_.start();
                }
            } else if (!authenticated_) {
                // 认证未通过时收到业务数据：丢弃并计一次失败。
                failAuth(QStringLiteral("business data before authentication"));
            } else {
                emit payloadReceived(1, data);
            }
        }
        timer_.stop(); assembling_.clear(); next_ = count_ = 0;
    }
}
void BleTransport::close() {
    discovery_.stop(); discoveryTimeout_.stop(); timer_.stop(); pump_.stop(); writeTimeout_.stop(); pending_.clear(); assembling_.clear(); writeInFlight_ = false; writeFailures_ = 0; ready_ = false;
    if (service_) { disconnect(service_, nullptr, this, nullptr); delete service_; service_ = nullptr; }
    if (controller_) { disconnect(controller_, nullptr, this, nullptr); delete controller_; controller_ = nullptr; }
    authenticated_ = false;
    authAttempts_ = 0;
    challenge_.clear();
    sessionKey_.fill('\0');
    sessionKey_.clear();
}
