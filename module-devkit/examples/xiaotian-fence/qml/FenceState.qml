import QtQuick

// 啸天红外围界模块的受控状态模型（报告 §11.5）。
//
// 报告要求页面清楚显示：已布防 / 未布防 / 告警 / 离线 / 故障 / 状态未知。
// 六种状态互斥，且**过期心跳必须转未知**，不能保留最后一次好状态。
//
// 本单例只做状态推导，不持有任何传感器数据；数据由模块经业务数据面
// 送进 SessionModel，这里只判断"还能不能相信它"。
QtObject {
    id: root

    // 与报告 §11.5 的状态枚举一一对应。
    readonly property string armed: "armed"
    readonly property string disarmed: "disarmed"
    readonly property string alarm: "alarm"
    readonly property string offline: "offline"
    readonly property string fault: "fault"
    readonly property string unknown: "unknown"

    // 心跳超过该阈值即视为过期，转 unknown。
    // 报告原文"过期心跳后转未知"，未给具体毫秒数，取 3 倍采样周期。
    property int heartbeatTimeoutMs: 9000

    // 由模块上报的原始状态；不含任何推导。
    property bool moduleArmed: false
    property int triggerCount: 0
    property double lastHeartbeatMs: 0
    property double lastTriggerMs: 0
    property string faultCode: ""
    property bool sensorOnline: false
    // 供电与数据新鲜度（报告 §11.5：显示供电/数据新鲜度）。
    property string powerSource: "unknown"
    property bool powerKnown: false
    property bool hasBattery: false
    property int batteryPercent: -1

    // 双路状态（P0 只要求两路独立真实遮挡/恢复）。
    // 每项 {segmentId, blocked, sampleMs, valid}
    property var segments: []

    function reset() {
        moduleArmed = false
        triggerCount = 0
        lastHeartbeatMs = 0
        lastTriggerMs = 0
        faultCode = ""
        sensorOnline = false
        powerSource = "unknown"
        powerKnown = false
        hasBattery = false
        batteryPercent = -1
        segments = []
    }

    // 心跳是否仍然新鲜。nowMs 由调用方给（便于测试注入时钟）。
    function heartbeatFresh(nowMs) {
        if (lastHeartbeatMs <= 0) return false
        return (nowMs - lastHeartbeatMs) <= heartbeatTimeoutMs
    }

    // 推导当前状态。**顺序即优先级**：
    //   故障 > 离线/未上线 > 告警 > 已布防 > 未布防 > 未知
    function state(nowMs) {
        if (faultCode !== "") return fault
        if (!sensorOnline) return offline
        if (!heartbeatFresh(nowMs)) return unknown
        // 告警保持：最近一次触发在超时窗口内仍显示"告警"。
        if (lastTriggerMs > 0 && (nowMs - lastTriggerMs) <= heartbeatTimeoutMs) return alarm
        return moduleArmed ? armed : disarmed
    }

    // 模块页拿不到宿主的 I18n：只回状态键，文案由页面映射。
    function stateLabel(nowMs) {
        return state(nowMs)
    }

    function stateIsWarning(nowMs) {
        const s = state(nowMs)
        return s === alarm || s === offline || s === fault
    }

    // 数据新鲜度：返回毫秒年龄，负数/0 表示尚无数据；页面自己格式化。
    function dataAgeMs(nowMs) {
        if (lastHeartbeatMs <= 0) return -1
        // 宿主时钟与页面时钟不同源：page.clock 每秒才刷新一次，
        // 可能短暂落后于刚收到的心跳。负数一律按 0（刚收到）处理。
        return Math.max(0, nowMs - lastHeartbeatMs)
    }

    // 供电：不知时为 unknown，页面显示"未检测"，不推算（§11.5 + §13）。
    function powerText() {
        if (!powerKnown) return "unknown"
        if (!hasBattery || batteryPercent < 0) return powerSource
        return powerSource + " " + batteryPercent + "%"
    }

    // 段状态键：未知段不能被当成"未遮挡"。
    function segmentState(segment) {
        if (!segment || segment.valid === false) return "unknown"
        return segment.blocked ? "blocked" : "clear"
    }
}
