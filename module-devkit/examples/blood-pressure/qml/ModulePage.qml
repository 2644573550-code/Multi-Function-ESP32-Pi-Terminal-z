import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Material

// 血压读数模块页面（模块自带 QML，由宿主独立引擎加载）。
//
// 可用注入项：sessionModel / moduleData / hostTheme。
// moduleData 是宿主注入的**当前用户**数据接口；本页不持有数据库、密钥或 SQL。
Rectangle {
    id: page
    property bool english: false

    // 宿主经 context 注入 hostTheme（含 scale）；页面不声明同名属性，
    // 否则会**遮蔽** context property（模块页拿到的永远是自己的默认值）。
    readonly property var theme: (typeof hostTheme !== "undefined" && hostTheme) ? hostTheme : ({})
    readonly property real uiScale: (theme.scale > 0) ? theme.scale : 1

    // 一次测量三个指标（报告 §12.1）：分开录入但存成一条记录。
    property string entrySys: ""
    property string entryDia: ""
    property string entryPulse: ""
    property string entryUnit: "mmHg"
    property string message: ""
    property var rows: []

    readonly property color background: theme.background || "#08121f"
    readonly property color surface: theme.surface || "#102238"
    readonly property color foreground: theme.foreground || "#ffffff"
    readonly property real gap: 16 * uiScale
    readonly property real hit: Math.max(48, 64 * uiScale)

    function reload() {
        if (typeof moduleData === "undefined" || !moduleData) { rows = []; return }
        rows = moduleData.recentMeasurements(100)
    }

    function sourceLabel(source) {
        const zh = { "user.entry": "手工录入", "device.auto": "设备自动",
                     "device.manual": "设备手动" }
        const en = { "user.entry": "Manual", "device.auto": "Device auto",
                     "device.manual": "Device manual" }
        const table = page.english ? en : zh
        return table[source] || (page.english ? "Unknown" : "来源未知")
    }

    function submit() {
        if (typeof moduleData === "undefined" || !moduleData) return
        // 至少填一项才允许保存；空表单不造记录。
        const sys = entrySys.trim(), dia = entryDia.trim(), pulse = entryPulse.trim()
        if (sys.length === 0 && dia.length === 0 && pulse.length === 0) {
            message = page.english ? "Enter at least one value" : "至少填写一项"
            return
        }
        // 只把填了的项交给宿主：缺失项保持 undefined，不补 0。
        const payload = { "unit": entryUnit, "source": "user.entry", "valid": true }
        if (sys.length > 0) payload.systolic = Number(sys)
        if (dia.length > 0) payload.diastolic = Number(dia)
        if (pulse.length > 0) payload.pulse = Number(pulse)
        // 收缩压必须高于舒张压：不成立的组合由模块侧判无效，这里先拦一道。
        if (payload.systolic !== undefined && payload.diastolic !== undefined
                && payload.systolic <= payload.diastolic) {
            message = page.english
                      ? "Systolic must be above diastolic" : "收缩压必须高于舒张压"
            return
        }
        const ok = moduleData.appendMeasurement(payload)
        if (ok) {
            entrySys = ""; entryDia = ""; entryPulse = ""
            message = page.english ? "Saved" : "已保存"
            reload()
        } else {
            message = page.english ? "Storage unavailable" : "数据存储不可用"
        }
    }

    Component.onCompleted: reload()

    Connections {
        target: typeof moduleData !== "undefined" ? moduleData : null
        function onDataChanged() { page.reload() }
    }

    color: page.background

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: page.gap
        spacing: page.gap

        Text {
            Layout.fillWidth: true
            text: page.english
                  ? "Daily reference only; not for diagnosis"
                  : "家庭日常参考，不作诊断依据"
            color: "#6e9abb"
            font.pixelSize: 12 * page.uiScale
            wrapMode: Text.Wrap
        }

        // ---- 手工录入 ----
        Text {
            text: page.english ? "Manual entry" : "手工录入"
            color: page.foreground
            font.pixelSize: 14 * page.uiScale
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: page.gap / 2
            TextField {
                id: sysField
                Layout.fillWidth: true
                Layout.preferredHeight: page.hit
                placeholderText: page.english ? "Systolic" : "收缩压"
                text: page.entrySys
                inputMethodHints: Qt.ImhDigitsOnly
                onTextChanged: page.entrySys = text
            }
            TextField {
                id: diaField
                Layout.fillWidth: true
                Layout.preferredHeight: page.hit
                placeholderText: page.english ? "Diastolic" : "舒张压"
                text: page.entryDia
                inputMethodHints: Qt.ImhDigitsOnly
                onTextChanged: page.entryDia = text
            }
            TextField {
                id: pulseField
                Layout.fillWidth: true
                Layout.preferredHeight: page.hit
                placeholderText: page.english ? "Pulse" : "脉搏"
                text: page.entryPulse
                inputMethodHints: Qt.ImhDigitsOnly
                onTextChanged: page.entryPulse = text
            }
            Button {
                Layout.preferredHeight: page.hit
                text: page.entryUnit
                onClicked: page.entryUnit = (page.entryUnit === "mmHg") ? "kPa" : "mmHg"
            }
            Button {
                Layout.preferredHeight: page.hit
                text: page.english ? "Save" : "保存"
                enabled: page.entrySys.length > 0 || page.entryDia.length > 0
                         || page.entryPulse.length > 0
                onClicked: page.submit()
            }
        }
        Text {
            Layout.fillWidth: true
            visible: page.message.length > 0
            text: page.message
            color: "#ffdaa0"
            font.pixelSize: 12 * page.uiScale
            wrapMode: Text.Wrap
        }

        // ---- 读数列表 ----
        Text {
            text: page.english ? "Recent readings" : "最近读数"
            color: "#6e9abb"
            font.pixelSize: 14 * page.uiScale
        }
        Text {
            Layout.fillWidth: true
            visible: page.rows.length === 0
            text: page.english ? "No readings yet" : "还没有读数"
            color: "#6e9abb"
            font.pixelSize: 12 * page.uiScale
        }
        Repeater {
            model: page.rows
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: page.hit
                radius: 12 * page.uiScale
                color: page.surface
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: page.gap / 2
                    Text {
                        Layout.fillWidth: true
                        // 每个指标独立判断：缺失显示"—"，不写 0。
                        readonly property string unit: modelData.unit || ""
                        text: (modelData.systolic !== undefined ? String(modelData.systolic) : "—")
                              + "/" + (modelData.diastolic !== undefined ? String(modelData.diastolic) : "—")
                              + (modelData.pulse !== undefined ? "  ♥" + modelData.pulse : "")
                              + (unit.length > 0 ? "  " + unit : "")
                        color: modelData.valid ? page.foreground : "#ffdaa0"
                        font.pixelSize: 14 * page.uiScale
                    }
                    Text {
                        text: page.sourceLabel(modelData.source)
                        color: "#6e9abb"
                        font.pixelSize: 12 * page.uiScale
                    }
                    Text {
                        visible: !modelData.valid
                        text: page.english ? "Invalid" : "无效"
                        color: "#ffdaa0"
                        font.pixelSize: 12 * page.uiScale
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
