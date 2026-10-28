#include "PogoControlTransport.h"
#include "../protocol/pibridge_protocol.h"
#include "../protocol/control_event.h"
#include <QJsonDocument>

PogoControlTransport::PogoControlTransport(QObject *parent) : TransportBase(parent) {
    reconnect_.setInterval(1000);
    connect(&reconnect_, &QTimer::timeout, this, [this] { if (enabled_ && !serial_.isOpen()) open(device_, baud_); });
    frameTimeout_.setInterval(1000); frameTimeout_.setSingleShot(true);
    connect(&frameTimeout_, &QTimer::timeout, this, [this] { buffer_.clear(); emit error("PBR1 partial-frame timeout"); });
    connect(&serial_, &QSerialPort::readyRead, this, &PogoControlTransport::readAvailable);
    connect(&serial_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e != QSerialPort::NoError) {
            emit error(serial_.errorString());
            if (e == QSerialPort::ResourceError || e == QSerialPort::DeviceNotFoundError || e == QSerialPort::ReadError) {
                // Qt can emit errorOccurred synchronously from open(). Closing
                // inside that callback corrupts the ongoing open attempt.
                QTimer::singleShot(0, this, [this] {
                    if (serial_.isOpen()) serial_.close();
                    frameTimeout_.stop(); buffer_.clear();
                    emit disconnected("control bridge lost");
                });
            }
        }
    });
}

bool PogoControlTransport::open(const QString &device, qint32 baud) {
    device_ = device; baud_ = baud; enabled_ = true; reconnect_.start();
    if (serial_.isOpen()) serial_.close();
    // Drop a previous failed-open error before configuring/reopening; do not
    // synchronously close the port from Qt's open() error callbacks.
    serial_.clearError(); frameTimeout_.stop();
    buffer_.clear(); haveReceiveSequence_ = false; lastReceiveSequence_ = 0; serial_.setReadBufferSize(8192);
    serial_.setPortName(device);
    serial_.setBaudRate(baud);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);
    if (!serial_.open(QIODevice::ReadWrite)) {
        qWarning().noquote() << "PBR1 serial open failed:" << device << serial_.errorString();
        emit error(serial_.errorString()); return false;
    }
    emit connected();
    return true;
}

void PogoControlTransport::close() {
    enabled_ = false; reconnect_.stop(); frameTimeout_.stop(); buffer_.clear();
    if (serial_.isOpen()) serial_.close();
    emit disconnected(QStringLiteral("serial closed"));
}

bool PogoControlTransport::sendCommand(const QJsonObject &command, quint32 *sequenceOut) {
    if (!serial_.isOpen()) return false;
    const QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
    const quint32 sequence = sequence_++;
    const auto wire = PiBridge::encode(0, PiBridge::ControlCommand, sequence, payload);
    const bool sent = !wire.isEmpty() && serial_.bytesToWrite() < 8192 && serial_.write(wire) == wire.size();
    if (sent && sequenceOut) *sequenceOut = sequence;
    return sent;
}

bool PogoControlTransport::sendRawControl(quint8 command, const QByteArray &payload) {
    QJsonObject object{{"command", int(command)}, {"payload_hex", QString::fromLatin1(payload.toHex())}};
    return sendCommand(object);
}

void PogoControlTransport::setSessionKeys(const QByteArray &bridgeToPiKey32,
                                          const QByteArray &noncePrefix4,
                                          const QByteArray &piKeyId) {
    bridgeToPiKey_ = bridgeToPiKey32;
    noncePrefix_ = noncePrefix4;
    piKeyId_ = piKeyId.isEmpty() ? 0 : quint8(piKeyId.at(0));
    replay_.reset();
    dedup_.clear();
}

void PogoControlTransport::readAvailable() {
    buffer_.append(serial_.readAll());
    if (buffer_.size() > 16384) { buffer_.clear(); emit error("PBR1 receive limit"); return; }
    quint8 direction = 0; PiBridge::Kind kind{}; quint32 sequence = 0; QByteArray payload;
    while (PiBridge::decode(buffer_, direction, kind, sequence, payload)) {
        if (direction != 1) continue;
        if (haveReceiveSequence_) {
            const quint32 distance = sequence - lastReceiveSequence_;
            if (distance == 0 || distance >= 0x80000000u) continue;
        }
        lastReceiveSequence_ = sequence; haveReceiveSequence_ = true;
        if (kind == PiBridge::ControlEvent) {
            // §6.2 的拒绝顺序在这里落地：
            //   1. 帧头合法性     — PiBridge::decode 已完成
            //   2. CRC16          — PiBridge::decode 已完成
            //   3. env_ver/key_id — parseEnvelope
            //   4. tag            — **先验**，通过后序号才可信
            //   5. auth_seq 单调性
            //   6. JSON + schema 校验
            //   7. event_id 去重 + TTL
            // 顺序不能调换：先查序号会让未认证的伪造帧推高窗口（DoS）。
            QByteArray plaintext = payload;

            if (authenticated_) {
                if (bridgeToPiKey_.size() != 32) continue;

                PbrAead::Envelope envelope;
                PbrAead::Aad aad;
                PbrAead::Result aeadResult = PbrAead::Result::Malformed;
                if (!PbrAead::parseEnvelope(payload, &envelope, nullptr, &aad, &aeadResult)) {
                    // 第 3 步失败：整帧丢弃、不回应（不给攻击者 oracle）。
                    continue;
                }
                aad.kind = quint8(kind);
                aad.dir = quint8(direction);
                // 第 4 步：tag 必须先验，且用信封自己声明的 auth_seq。
                plaintext = PbrAead::open(payload, bridgeToPiKey_, aad, &aeadResult);
                if (aeadResult != PbrAead::Result::Ok) continue;

                // 第 5 步：tag 通过后序号才可信。
                const auto seqVerdict = replay_.check(envelope.authSeq);
                if (seqVerdict != ReplayGuard::Verdict::Accepted) {
                    // 已认证对端才回 REPLAY；未认证期不回。
                    emit controlEvent(QJsonObject{{"event", "replay"},
                                                  {"auth_seq", double(envelope.authSeq)}});
                    continue;
                }
                replay_.advance(envelope.authSeq);
                if (replay_.needsRehandshake())
                    emit error(QStringLiteral("PBR auth_seq nearing wrap; rehandshake required"));
            }
            // 未认证期（authenticated_ == false）只允许握手帧；业务事件
            // 一律不解析、不分派，避免留一条未认证的业务入口（§6.1）。

            // 未认证期只允许握手/接入类明文帧通过；业务与诊断事件一律拒绝。
            // §6.1 的原文是"只接受 hello_req"，P0 的接入链路用明文 attach
            // 完成设备发现（T-05），因此这里放行 attach/detach/hello，
            // 但绝不放行 rfid_card / ack / 传感器数据这些需要认证的内容。
            if (!authenticated_) {
                static const QSet<QString> kHandshakeOnly{
                    QStringLiteral("hello_req"), QStringLiteral("hello_resp"),
                    QStringLiteral("hello_finish"), QStringLiteral("hello_ok"),
                    QStringLiteral("attach"), QStringLiteral("detach"),
                    QStringLiteral("status")};
                const QString probe = QString::fromUtf8(plaintext.left(256));
                bool handshakeFrame = false;
                for (const QString &allowed : kHandshakeOnly) {
                    if (probe.contains(QLatin1Char('"') + allowed + QLatin1Char('"'))) {
                        handshakeFrame = true;
                        break;
                    }
                }
                // 认证后才允许业务事件（rfid_card/ack/传感器）。
                if (!handshakeFrame) continue;
            }

            // 第 6 步：认证通过后才解析 JSON。
            if (!PbrControl::payloadAcceptable(plaintext)) continue;
            auto event = PiBridge::event(plaintext);
            if (event.isEmpty()) continue;

            // 第 7 步：event_id 去重（键含 bridge/boot/session/event）。
            const QString dedupKey = QStringLiteral("%1|%2|%3|%4")
                .arg(event.value("bridge_id").toString(),
                     event.value("boot_id").toString(),
                     event.value("session_id").toString(),
                     QString::number(event.value("event_id").toDouble(), 'g', 20));
            if (!event.value("event_id").isUndefined()) {
                const auto verdict = dedup_.check(dedupKey);
                if (verdict == EventDedup::Verdict::Duplicate) {
                    // 重复事件：回 duplicate，不重复分派。
                    emit controlEvent(QJsonObject{{"event", "ack"},
                                                  {"ack_event_id", event.value("event_id")},
                                                  {"result", "duplicate"}});
                    continue;
                }
                if (verdict == EventDedup::Verdict::TableFull) {
                    emit error(QStringLiteral("event dedup table full; session must be re-established"));
                    continue;
                }
            }

            event["_bridge_sequence"] = double(sequence);
            emit controlEvent(event);
        }
        else if (kind == PiBridge::Error) emit error(QStringLiteral("PBR1 bridge error: ") + QString::fromUtf8(payload).left(512));
    }
    if (buffer_.isEmpty()) frameTimeout_.stop(); else frameTimeout_.start();
}
