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
            // Reject duplicate keys and over-deep nesting before Qt's parser
            // silently normalises them (f-day1 §4.2). Only plaintext payloads
            // are scanned here: T-07 must unwrap the AEAD envelope before this
            // point, since a sealed payload is binary and would never pass a
            // JSON scanner.
            if (!PbrControl::payloadAcceptable(payload)) continue;
            auto event = PiBridge::event(payload);
            event["_bridge_sequence"] = double(sequence);
            emit controlEvent(event);
        }
        else if (kind == PiBridge::Error) emit error(QStringLiteral("PBR1 bridge error: ") + QString::fromUtf8(payload).left(512));
    }
    if (buffer_.isEmpty()) frameTimeout_.stop(); else frameTimeout_.start();
}
