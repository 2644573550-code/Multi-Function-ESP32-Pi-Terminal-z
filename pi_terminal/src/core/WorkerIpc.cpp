#include "core/WorkerIpc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <QtGlobal>

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

QString safeToken(const QString &session) {
    // session 是内部标识，仍需过滤成 systemd 允许的单元名字符集。
    QString safe;
    for (const QChar c : session)
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) safe.append(c);
    if (safe.isEmpty()) safe = QStringLiteral("default");
    return safe;
}

QString unitName(const QString &session) {
    return QStringLiteral("orange-pi-worker@%1.service").arg(safeToken(session));
}

QString socketPath(const QString &session) {
    // 显式覆盖优先（测试与开发构建用；生产不给这个变量）。
    const QString override = qEnvironmentVariable("ORANGE_PI_WORKER_SOCKET");
    if (!override.isEmpty()) return override;
    // systemd RuntimeDirectory=<name> 落在 /run/<name>。
    // 必须与 unit 里的 RuntimeDirectory=orange-pi-worker-%i 完全一致。
    return QStringLiteral("/run/orange-pi-worker-%1/").arg(safeToken(session)) + QLatin1String(SocketName);
}

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
    // 白名单固定五条；任何扩展都要改这里并走 PR 审核。
    // invokeAction：模块自定义动作（模块页按钮）。参数只允许白名单 ID，
    // 不做任意字符串透传（§8.4 动作采用白名单 ID）。
    return op == QLatin1String("publishSample") || op == QLatin1String("readOwnRecords")
        || op == QLatin1String("reportState") || op == QLatin1String("stop")
        || op == QLatin1String("invokeAction");
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
