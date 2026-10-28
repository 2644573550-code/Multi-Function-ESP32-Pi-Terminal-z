// 啸天红外围界模块（Pi 侧插件）。
//
// 报告 §11.3：P0 只验两路独立真实遮挡/恢复、去抖、未知状态、安全传输与页面。
// 本文件只做协议适配与状态机，不碰 GPIO —— 真实传感器读数是 ESP 固件的事（L）。
//
// 上报字段（module-defined-v1）：
//   armed            布防中
//   trigger_count    本次启动后的触发次数
//   trigger_ms       最近触发时间（设备单调时钟）
//   heartbeat_ms     最近心跳（设备单调时钟）
//   sensor_online    传感器是否在线
//   fault_code       故障码，空串表示无故障
//   power_known / power_source / has_battery / battery_percent
//   segments[]       [{segment_id, blocked, sample_ms, valid}]
//
// 关键约束：**没有防拆/断线硬件时不能声称已检测**（§11.3），
// 因此 sensor_online=false 与 fault_code 为空是两种不同状态。
#include "pi_module_api.h"
#include "module_manifest.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

namespace {

// 去抖：同一段连续 N 次同值才认账（§11.3「输入去抖」）。
constexpr int kDebounceSamples = 2;
// 告警保持时间：让用户看得见（§11.3「告警保持一段时间」）。
constexpr qint64 kAlarmHoldMs = 3000;

struct SegmentState {
    QString id;
    bool rawBlocked = false;
    bool stableBlocked = false;
    int sameCount = 0;
};

struct FenceModule {
    const pi_host_services_t *host = nullptr;
    uint8_t transport = PI_TRANSPORT_HOTSPOT_LAN;
    bool stopped = false;
    bool armed = false;
    int triggerCount = 0;
    qint64 lastTriggerMs = 0;
    qint64 alarmUntilMs = 0;
    qint64 bootMs = 0;
    QVector<SegmentState> segments;
};

QString segmentKey(const QJsonObject &o) {
    return o.value(QStringLiteral("segment_id")).toString();
}

}  // namespace

struct pi_module_handle {
    const pi_host_services_t *host;
    uint8_t transport = PI_TRANSPORT_HOTSPOT_LAN;
    bool stopped = false;
    std::unique_ptr<FenceModule> fence;
};

extern "C" uint32_t pi_module_api_version() { return 1; }
extern "C" const char *pi_module_manifest_json() { return MODULE_MANIFEST_JSON; }

extern "C" pi_module_handle_t *pi_module_create(const pi_host_services_t *h) {
    if (!h || h->api_version != 1 || !h->emit_event) return nullptr;
    auto *handle = new pi_module_handle;
    handle->host = h;
    handle->fence.reset(new FenceModule);
    handle->fence->host = h;
    handle->fence->bootMs = QDateTime::currentMSecsSinceEpoch();
    return handle;
}

extern "C" int pi_module_on_attach(pi_module_handle_t *m, const char *json) {
    if (!m || !json || !m->fence) return -1;
    const QJsonObject config = QJsonDocument::fromJson(json).object();
    if (config.value(QStringLiteral("module_instance")).toString().isEmpty()) return -1;
    // 双路是 P0 的最小范围；段数由固件声明。
    const QJsonArray declared = config.value(QStringLiteral("segments")).toArray();
    m->fence->segments.clear();
    if (declared.isEmpty()) {
        for (const char *id : {"A", "B"}) m->fence->segments.append({QString::fromLatin1(id), false, false, 0});
    } else {
        for (const auto &value : declared)
            m->fence->segments.append({value.toString(), false, false, 0});
    }
    m->fence->stopped = false;
    m->fence->bootMs = QDateTime::currentMSecsSinceEpoch();
    return 0;
}

extern "C" void pi_module_on_transport_connected(pi_module_handle_t *m, uint8_t t) {
    if (m) m->transport = t;
}

extern "C" int pi_module_on_data(pi_module_handle_t *m, uint8_t, const uint8_t *p, uint32_t n) {
    if (!m || m->stopped || !m->fence || !p || n > 65532) return -1;
    const QJsonObject input =
        QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char *>(p), int(n))).object();
    FenceModule &fence = *m->fence;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 去抖：连续 kDebounceSamples 次同值才更新稳定状态。
    for (const QJsonValue &value : input.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject segment = value.toObject();
        const QString id = segmentKey(segment);
        for (SegmentState &state : fence.segments) {
            if (state.id != id) continue;
            const bool blocked = segment.value(QStringLiteral("blocked")).toBool();
            if (blocked == state.rawBlocked) {
                ++state.sameCount;
            } else {
                state.rawBlocked = blocked;
                state.sameCount = 1;
            }
            if (state.sameCount >= kDebounceSamples && state.stableBlocked != blocked) {
                state.stableBlocked = blocked;
                // 只有布防中且由未遮挡转为遮挡才算一次触发。
                if (blocked && fence.armed) {
                    ++fence.triggerCount;
                    fence.lastTriggerMs = now;
                    fence.alarmUntilMs = now + kAlarmHoldMs;
                }
            }
        }
    }

    QJsonArray outSegments;
    for (const SegmentState &state : fence.segments) {
        outSegments.append(QJsonObject{
            {QStringLiteral("segment_id"), state.id},
            {QStringLiteral("blocked"), state.stableBlocked},
            {QStringLiteral("sample_ms"), double(now - fence.bootMs)},
            // 去抖未收敛时明确标无效，不冒充"未遮挡"（§11.3）。
            {QStringLiteral("valid"), state.sameCount >= kDebounceSamples}});
    }

    QJsonObject output{
        {QStringLiteral("schema"), QStringLiteral("p0-data-v1")},
        {QStringLiteral("armed"), fence.armed},
        {QStringLiteral("trigger_count"), fence.triggerCount},
        {QStringLiteral("trigger_ms"), double(fence.lastTriggerMs)},
        {QStringLiteral("heartbeat_ms"), double(now)},
        {QStringLiteral("alarm_active"), now < fence.alarmUntilMs},
        {QStringLiteral("segments"), outSegments},
    };
    // 供电与故障：没有硬件就如实上报"未知"，不伪造（§11.3 / §13）。
    if (input.contains(QStringLiteral("power_known"))) {
        output.insert(QStringLiteral("power_known"), input.value(QStringLiteral("power_known")));
        output.insert(QStringLiteral("power_source"), input.value(QStringLiteral("power_source")));
        output.insert(QStringLiteral("has_battery"), input.value(QStringLiteral("has_battery")));
        output.insert(QStringLiteral("battery_percent"), input.value(QStringLiteral("battery_percent")));
    } else {
        output.insert(QStringLiteral("power_known"), false);
    }
    output.insert(QStringLiteral("sensor_online"),
                  input.value(QStringLiteral("sensor_online")).toBool(false));
    output.insert(QStringLiteral("fault_code"),
                  input.value(QStringLiteral("fault_code")).toString());

    const QByteArray data = QJsonDocument(output).toJson(QJsonDocument::Compact);
    m->host->emit_event(m->host->context, "sample", data.constData());
    return 0;
}

extern "C" int pi_module_action(pi_module_handle_t *m, const char *a) {
    if (!m || m->stopped || !m->fence || !a) return -1;
    const QString action = QString::fromUtf8(a);
    // 动作白名单：只接受 manifest 里声明过的（§8.4）。
    if (action == QLatin1String("arm")) m->fence->armed = true;
    else if (action == QLatin1String("disarm")) { m->fence->armed = false; m->fence->alarmUntilMs = 0; }
    else if (action == QLatin1String("request_disconnect")) {
        const QByteArray data("{\"command\":\"request_disconnect\"}");
        return m->host->send_data(m->host->context, m->transport,
                                  reinterpret_cast<const uint8_t *>(data.constData()),
                                  uint32_t(data.size()));
    } else {
        return -1;   // 未知动作有界拒绝，不重启模块（§11.4）
    }
    return 0;
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
