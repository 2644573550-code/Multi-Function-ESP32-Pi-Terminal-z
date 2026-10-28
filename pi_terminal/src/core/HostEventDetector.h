#pragma once
#include <QObject>
#include <QJsonObject>
#include <QRegularExpression>
#include <QHash>

#include "protocol/control_event.h"

// Physical debounce belongs to ESP32. This layer validates bridge identity only.
class HostEventDetector : public QObject {
    Q_OBJECT
public:
    explicit HostEventDetector(QObject *parent = nullptr) : QObject(parent) {}
    static bool valid(const QJsonObject &event) {
        // p0-control-v1 events are validated field by field; the pre-P0
        // schema-less attach/detach stays accepted until T-05 replaces it.
        if (PbrControl::validate(event).verdict == PbrControl::Verdict::Rejected) return false;
        const auto kind = event.value("event").toString();

        // 报告 §5.1 / T-08：RC522 是**桥板级**事件，不带 module_id /
        // module_instance，只带 UID。不能按模块事件的字段要求去校验它，
        // 也不能给它伪造 slot/device 上下文。
        if (kind == QLatin1String("rfid_card")) {
            const QString uid = event.value("uid").toString();
            const int uidLen = event.value("uid_len").toInt(-1);
            if (uidLen != 4 && uidLen != 7 && uidLen != 10) return false;
            return QRegularExpression("^[0-9a-fA-F]+$").match(uid).hasMatch()
                && uid.size() == uidLen * 2;
        }

        const double id = event.value("module_id").toDouble(-1);
        return (kind == "attach" || kind == "detach") && id >= 1 && id <= 255 && id == int(id)
            && QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(event.value("module_instance").toString()).hasMatch();
    }

public slots:
    void reset() { last_.clear(); }
    void consume(const QJsonObject &event) {
        if (!valid(event)) return;
        // RFID 是桥板级事件：直接上抛，不参与按 instance 的序号窗口。
        if (event.value("event").toString() == QLatin1String("rfid_card")) {
            emit detected(event);
            return;
        }
        const auto instance = event.value("module_instance").toString();
        if (event.value("_bridge_sequence").isDouble()) {
            const double number = event.value("_bridge_sequence").toDouble(-1);
            if (!(number >= 1 && number <= 4294967295.0)) return;
            const quint32 sequence = quint32(number);
            if (number != sequence || (last_.contains(instance) &&
                (sequence == last_.value(instance) || sequence - last_.value(instance) >= 0x80000000u))) return;
            last_[instance] = sequence;
        }
        emit detected(event);
    }
signals:
    void detected(QJsonObject event);
private:
    QHash<QString, quint32> last_;
};
