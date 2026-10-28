import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material

// 啸天红外围界模块页面（模块自带 QML，由宿主独立引擎加载）。
//
// 可用注入项只有三个（报告 §6.1 / §8.4）：
//   sessionModel  模块实时数据
//   data          当前用户的数据接口（appendSample / historySeries / ...）
//   hostTheme     主题值（background/surface/foreground/accent/scale）
//
// 拿不到宿主的 I18n：文案在本文件内自带中英双语。
// 拿不到宿主的任何服务对象、数据库或密钥。
Rectangle {
    id: page
    property var sessionModel: null
    property real hostUiScale: 1
    property var hostTheme: ({})
    property bool english: false

    property double clock: Date.now()
    property bool diagnosticsOpen: false
    property var historyPoints: []

    readonly property var latest: sessionModel ? sessionModel.data : ({})
    readonly property color background: hostTheme.background || "#08121f"
    readonly property color surface: hostTheme.surface || "#102238"
    readonly property color foreground: hostTheme.foreground || "#ffffff"
    readonly property color accent: hostTheme.accent || "#85deff"
    readonly property real gap: 16 * hostUiScale
    readonly property real hit: Math.max(48, 64 * hostUiScale)

    // 把模块上报的字段喂给状态对象（同目录的 FenceState.qml）。
    function applySample(d) {
        if (!d) return
        if (d.armed !== undefined) fence.moduleArmed = !!d.armed
        if (d.trigger_count !== undefined) fence.triggerCount = Number(d.trigger_count)
        if (d.sensor_online !== undefined) fence.sensorOnline = !!d.sensor_online
        if (d.fault_code !== undefined) fence.faultCode = String(d.fault_code)
        if (d.heartbeat_ms !== undefined) fence.lastHeartbeatMs = Number(d.heartbeat_ms)
        if (d.trigger_ms !== undefined) fence.lastTriggerMs = Number(d.trigger_ms)
        if (d.power_known !== undefined) {
            fence.powerKnown = !!d.power_known
            fence.powerSource = d.power_source || "unknown"
            fence.hasBattery = !!d.has_battery
            fence.batteryPercent = d.battery_percent === undefined ? -1 : Number(d.battery_percent)
        }
        if (Array.isArray(d.segments)) fence.segments = d.segments
    }

    onLatestChanged: applySample(latest)

    // 落库：把当前读数交给宿主的数据接口（不直接碰数据库）。
    function persist() {
        if (typeof data === "undefined" || !data) return
        const t = Number(latest.armed === undefined ? NaN : latest.temperature_c)
        if (!isFinite(t)) return
        data.appendSample({
            "temperature_c": t,
            "source": "device.auto",
            "valid": true
        })
        refreshHistory()
    }

    function refreshHistory() {
        if (typeof data === "undefined" || !data) return
        historyPoints = data.historySeries(7 * 24 * 3600 * 1000, 60 * 60 * 1000)
    }

    FenceState { id: fence }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: page.clock = Date.now()
    }

    Component.onCompleted: refreshHistory()

    Connections {
        target: typeof data !== "undefined" ? data : null
        function onDataChanged() { page.refreshHistory() }
    }

    function label(key) {
        const zh = {
            armed: "已布防", disarmed: "未布防", alarm: "告警",
            offline: "离线", fault: "故障", unknown: "状态未知",
            blocked: "遮挡", clear: "未遮挡", notDetected: "未检测"
        }
        const en = {
            armed: "Armed", disarmed: "Disarmed", alarm: "Alarm",
            offline: "Offline", fault: "Fault", unknown: "Unknown",
            blocked: "Blocked", clear: "Clear", notDetected: "Not detected"
        }
        const table = page.english ? en : zh
        return table[key] || key
    }

    color: page.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: page.gap
        spacing: page.gap

        // ---- 主状态 ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: page.hit * 1.6
            radius: 16 * page.hostUiScale
            color: page.surface
            border.width: 2
            border.color: fence.stateIsWarning(page.clock) ? "#ffdaa0" : "#3b536a"
            Text {
                anchors.centerIn: parent
                text: page.label(fence.state(page.clock))
                color: fence.stateIsWarning(page.clock) ? "#ffdaa0" : page.foreground
                font.pixelSize: 24 * page.hostUiScale
            }
        }

        // ---- 双路遮挡 ----
        Text {
            text: page.english ? "Blockage" : "遮挡状态"
            color: page.foreground
            font.pixelSize: 14 * page.hostUiScale
        }
        Flow {
            Layout.fillWidth: true
            spacing: page.gap / 2
            Repeater {
                model: fence.segments
                delegate: Rectangle {
                    required property var modelData
                    width: 160 * page.hostUiScale
                    height: page.hit
                    radius: 12 * page.hostUiScale
                    color: page.surface
                    border.width: 1
                    border.color: fence.segmentState(modelData) === "blocked" ? "#ffdaa0" : "#3b536a"
                    Text {
                        anchors.centerIn: parent
                        text: (modelData.segment_id || "?") + "  "
                              + page.label(fence.segmentState(modelData))
                        color: page.foreground
                        font.pixelSize: 14 * page.hostUiScale
                    }
                }
            }
        }

        // ---- 计数与新鲜度 ----
        Text {
            text: (page.english ? "Triggers since boot: " : "本次启动后触发: ")
                  + fence.triggerCount
            color: page.foreground
            font.pixelSize: 14 * page.hostUiScale
        }
        Text {
            readonly property double age: fence.dataAgeMs(page.clock)
            text: (page.english ? "Data: " : "数据: ")
                  + (age < 0 ? (page.english ? "no data" : "尚无数据")
                             : age <= fence.heartbeatTimeoutMs
                               ? (page.english ? "fresh" : "新鲜")
                               : (page.english ? "stale" : "已过期"))
            color: "#6e9abb"
            font.pixelSize: 12 * page.hostUiScale
        }
        Text {
            text: (page.english ? "Power: " : "供电: ")
                  + (fence.powerKnown ? fence.powerText() : page.label("notDetected"))
            color: "#6e9abb"
            font.pixelSize: 12 * page.hostUiScale
        }

        // ---- 诊断：默认折叠 ----
        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: page.hit
            text: page.diagnosticsOpen
                  ? (page.english ? "Hide diagnostics" : "收起诊断")
                  : (page.english ? "Show diagnostics" : "显示诊断")
            onClicked: page.diagnosticsOpen = !page.diagnosticsOpen
        }
        Text {
            Layout.fillWidth: true
            visible: page.diagnosticsOpen
            text: JSON.stringify(page.latest, null, 2)
            color: "#6e9abb"
            font.pixelSize: 12 * page.hostUiScale
            wrapMode: Text.WrapAnywhere
        }

        Item { Layout.fillHeight: true }
    }
}
