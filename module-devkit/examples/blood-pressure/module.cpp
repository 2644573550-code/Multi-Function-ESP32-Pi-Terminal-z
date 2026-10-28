// 血压读数模块（Pi 侧插件）。
//
// 报告 §12：P0 只做单型号适配器，内部映射到稳定的项目记录 schema，
// 保留型号、来源、单位、时间、有效性和错误原因。
// 自动测量与手工录入都支持，UI/存储区分来源，避免重复记录。
//
// 本文件**不做诊断**、不补造数据；读数异常必须标无效并给原因（§12.3）。
//
// 上报字段（module-defined-v1）：
//   source          device.auto / device.manual / user.entry
//   device_model    型号
//   measured_at_ms  设备侧测量时间
//   systolic / diastolic / pulse   各自可缺（null 表示未测）
//   unit            mmHg / kPa
//   valid / invalid_reason
#include "pi_module_api.h"
#include "module_manifest.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <memory>

namespace {

// 边界值：超出即标无效，不裁剪、不补造（§12.3）。
constexpr double kSystolicMin = 40.0, kSystolicMax = 300.0;
constexpr double kDiastolicMin = 20.0, kDiastolicMax = 200.0;
constexpr double kPulseMin = 20.0, kPulseMax = 250.0;

struct PressureModule {
    const pi_host_services_t *host = nullptr;
    uint8_t transport = PI_TRANSPORT_HOTSPOT_LAN;
    bool stopped = false;
    QString deviceModel;
};

bool inRange(const QJsonValue &value, double low, double high, bool *present) {
    // 缺失与越界是两件事：缺失不报无效，越界才报。
    if (!value.isDouble() && !value.isUndefined()) return false;
    if (value.isUndefined() || value.isNull()) { *present = false; return true; }
    *present = true;
    const double v = value.toDouble();
    return std::isfinite(v) && v >= low && v <= high;
}

}  // namespace

struct pi_module_handle {
    const pi_host_services_t *host;
    uint8_t transport = PI_TRANSPORT_HOTSPOT_LAN;
    bool stopped = false;
    std::unique_ptr<PressureModule> pressure;
};

extern "C" uint32_t pi_module_api_version() { return 1; }
extern "C" const char *pi_module_manifest_json() { return MODULE_MANIFEST_JSON; }

extern "C" pi_module_handle_t *pi_module_create(const pi_host_services_t *h) {
    if (!h || h->api_version != 1 || !h->emit_event) return nullptr;
    auto *handle = new pi_module_handle;
    handle->host = h;
    handle->pressure.reset(new PressureModule);
    handle->pressure->host = h;
    return handle;
}

extern "C" int pi_module_on_attach(pi_module_handle_t *m, const char *json) {
    if (!m || !json || !m->pressure) return -1;
    const QJsonObject config = QJsonDocument::fromJson(json).object();
    if (config.value(QStringLiteral("module_instance")).toString().isEmpty()) return -1;
    // 单型号适配器：型号由预置 profile 指定，不接受模块自报（§12.1）。
    m->pressure->deviceModel = config.value(QStringLiteral("device_model")).toString();
    m->pressure->stopped = false;
    return 0;
}

extern "C" void pi_module_on_transport_connected(pi_module_handle_t *m, uint8_t t) {
    if (m) m->transport = t;
}

extern "C" int pi_module_on_data(pi_module_handle_t *m, uint8_t, const uint8_t *p, uint32_t n) {
    if (!m || m->stopped || !m->pressure || !p || n > 65532) return -1;
    const QJsonObject input =
        QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char *>(p), int(n))).object();
    PressureModule &pressure = *m->pressure;

    bool hasSys = false, hasDia = false, hasPulse = false;
    QStringList problems;
    if (!inRange(input.value(QStringLiteral("systolic")), kSystolicMin, kSystolicMax, &hasSys))
        problems.append(QStringLiteral("systolic_out_of_range"));
    if (!inRange(input.value(QStringLiteral("diastolic")), kDiastolicMin, kDiastolicMax, &hasDia))
        problems.append(QStringLiteral("diastolic_out_of_range"));
    if (!inRange(input.value(QStringLiteral("pulse")), kPulseMin, kPulseMax, &hasPulse))
        problems.append(QStringLiteral("pulse_out_of_range"));

    // 收缩压必须高于舒张压：不成立即标无效，不修正数值。
    if (hasSys && hasDia
        && input.value(QStringLiteral("systolic")).toDouble()
               <= input.value(QStringLiteral("diastolic")).toDouble()) {
        problems.append(QStringLiteral("systolic_not_above_diastolic"));
    }
    // 什么都没测到：明确报缺字段，不能凭空造一条记录。
    if (!hasSys && !hasDia && !hasPulse) problems.append(QStringLiteral("no_measurement"));

    const QString source = input.value(QStringLiteral("source")).toString();
    QJsonObject output{
        {QStringLiteral("schema"), QStringLiteral("p0-data-v1")},
        {QStringLiteral("source"),
         (source == QLatin1String("device.manual") || source == QLatin1String("user.entry"))
             ? source : QStringLiteral("device.auto")},
        {QStringLiteral("device_model"), pressure.deviceModel},
        {QStringLiteral("measured_at_ms"), input.value(QStringLiteral("measured_at_ms"))},
        {QStringLiteral("unit"),
         input.value(QStringLiteral("unit")).toString().isEmpty()
             ? QStringLiteral("mmHg") : input.value(QStringLiteral("unit")).toString()},
        {QStringLiteral("received_at"), double(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("valid"), problems.isEmpty()},
        {QStringLiteral("invalid_reason"), problems.join(QLatin1Char(','))},
    };
    // 缺失值不写进 JSON：QML 侧用 undefined 判断"未检测"。
    if (hasSys) output.insert(QStringLiteral("systolic"), input.value(QStringLiteral("systolic")));
    if (hasDia) output.insert(QStringLiteral("diastolic"), input.value(QStringLiteral("diastolic")));
    if (hasPulse) output.insert(QStringLiteral("pulse"), input.value(QStringLiteral("pulse")));

    const QByteArray data = QJsonDocument(output).toJson(QJsonDocument::Compact);
    m->host->emit_event(m->host->context, "sample", data.constData());
    return 0;
}

extern "C" int pi_module_action(pi_module_handle_t *m, const char *a) {
    if (!m || m->stopped || !a) return -1;
    const QString action = QString::fromUtf8(a);
    // 只声明了 request_disconnect；其余有界拒绝。
    if (action != QLatin1String("request_disconnect")) return -1;
    const QByteArray data("{\"command\":\"request_disconnect\"}");
    return m->host->send_data(m->host->context, m->transport,
                              reinterpret_cast<const uint8_t *>(data.constData()),
                              uint32_t(data.size()));
}

extern "C" void pi_module_on_disconnect_request(pi_module_handle_t *, const char *) {}
extern "C" void pi_module_stop(pi_module_handle_t *m) {
    if (!m || m->stopped) return;
    m->stopped = true;
}
extern "C" void pi_module_on_detach(pi_module_handle_t *m) { pi_module_stop(m); }
extern "C" void pi_module_destroy(pi_module_handle_t *m) {
    if (!m) return;
    pi_module_stop(m);
    delete m;
}
