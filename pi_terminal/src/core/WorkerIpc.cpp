#include "core/WorkerIpc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>

namespace WorkerIpc {

namespace {
int jsonDepth(const QJsonValue &value) {
    if (value.isObject()) {
        int worst = 0;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) worst = qMax(worst, jsonDepth(it.value()));
        return worst + 1;
    }
    if (value.isArray()) {
        int worst = 0;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) worst = qMax(worst, jsonDepth(item));
        return worst + 1;
    }
    return 0;
}
}  // namespace

QByteArray encode(const QJsonObject &message) {
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame(4, Qt::Uninitialized);
    qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), frame.data());
    frame.append(payload);
    return frame;
}

QJsonObject decode(const QByteArray &frame) {
    // 超长立即拒绝，不做截断重试。
    if (frame.size() < 4 || frame.size() > kMaxMessageBytes + 4) return {};
    const quint32 declared = qFromLittleEndian<quint32>(frame.constData());
    if (static_cast<int>(declared) != frame.size() - 4) return {};
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(frame.mid(4), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
    const QJsonObject object = document.object();
    if (object.value("schema").toString() != QLatin1String(Schema)) return {};
    if (jsonDepth(object) > kMaxDepth) return {};
    return object;
}

bool opAllowed(const QString &op) {
    // 白名单固定四条；任何扩展都要改这里并走 PR 审核。
    return op == QLatin1String("publishSample") || op == QLatin1String("readOwnRecords")
        || op == QLatin1String("reportState") || op == QLatin1String("stop");
}

QJsonObject request(quint64 requestId, const QString &op, const QJsonObject &body) {
    return {{"schema", QLatin1String(Schema)},
            {"request_id", static_cast<double>(requestId)},
            {"op", op},
            {"body", body}};
}

QJsonObject response(quint64 requestId, const QString &code, const QJsonObject &body) {
    return {{"schema", QLatin1String(Schema)},
            {"request_id", static_cast<double>(requestId)},
            {"code", code},
            {"body", body}};
}

}  // namespace WorkerIpc
