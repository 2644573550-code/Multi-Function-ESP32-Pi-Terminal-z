import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "TimeMath.js" as TimeMath

// 设置页的「时间与报时」分区：世界时钟校时 + 整点报时。
//
// 边界（与作者约定一致）：
// - 只做设置界面；校时状态归 TimeSync.qml，报时状态归 AlarmEngine.qml，两者都在
//   Main.qml 实例化一次，因此这里改完，时间页立刻是同一份状态。
// - 只有点「网络校时」才联网；失败如实显示，自定义模式不自动回退到公共接口。
// - 控件全部复用项目既有组件（CarButton / CarField / Switch / IconButton）与 Theme
//   token，热区不小于 Theme.hit（内部有 48px 下限），不引入新依赖。
SettingsGroup {
    id: settings

    property var timeSync: null
    property var alarmEngine: null
    // 指针表盘配色：auto / light / dark，由 Main 持有并持久化。
    property string clockFace: "auto"
    signal clockFaceRequested(string mode)
    // 时间格式：24 / 12（上午·下午），同样由 Main 持有。
    property string timeFormat: "24"
    signal timeFormatRequested(string mode)

    property string nowText: ""
    property int localOffsetMinutes: 0
    readonly property string statusText: {
        if (!timeSync) return ""
        if (timeSync.syncing) return I18n.t("正在校时") + "…"
        if (timeSync.lastAttempt === "") return I18n.t("尚未校时，世界时钟按内置标准时区显示")
        return (timeSync.lastAttemptOk ? I18n.t("上次校时 ") : I18n.t("上次校时未成功 "))
               + timeSync.lastAttempt
               + (timeSync.lastAttemptOk ? "" : I18n.t("，仍显示内置时区"))
    }

    function refreshNow() {
        const parts = TimeMath.localParts(Date.now())
        localOffsetMinutes = TimeMath.localOffsetMinutes(Date.now())
        const date = parts.year + I18n.t("年") + parts.month + I18n.t("月") + parts.day + I18n.t("日")
        const time = settings.timeFormat === "12"
                     ? TimeMath.formatHm12(parts) + " " +
                       (TimeMath.isAfternoon(parts.hour) ? I18n.t("下午") : I18n.t("上午"))
                     : TimeMath.formatHms(parts, false)
        nowText = date + "  " + time
    }

    Component.onCompleted: refreshNow()

    // 供页面/测试观察：线路失效弹窗是否在屏上。
    readonly property bool alertPopupOpen: syncAlert.visible

    iconName: "schedule"
    accentColor: "#8f7bff"
    title: I18n.t("时间与报时")
    subtitle: I18n.t("世界时钟校时与整点语音报时")

    // ------------------------------------------------------------ 表盘配色

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        CarText {
            Layout.fillWidth: true
            maximumLineCount: 1
            text: I18n.t("指针表盘配色")
        }
        CarButton {
            objectName: "clockFaceLight"
            text: I18n.t("白")
            checked: settings.clockFace === "light"
            onClicked: settings.clockFaceRequested("light")
        }
        CarButton {
            objectName: "clockFaceDark"
            text: I18n.t("黑")
            checked: settings.clockFace === "dark"
            onClicked: settings.clockFaceRequested("dark")
        }
        CarButton {
            objectName: "clockFaceAuto"
            text: I18n.t("跟随白天夜晚")
            checked: settings.clockFace === "auto"
            onClicked: settings.clockFaceRequested("auto")
        }
    }
    CarText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        subdued: true
        text: I18n.t("自动模式：06:00–17:59 用浅色盘面，其余时间用深色盘面；城市钟按各自城市的时间判断。")
    }

    // ------------------------------------------------------------ 日期与时间（自动校时）

    // 仿系统「日期和时间」设置：开关 + 只读值，用户不需要点「校时」。
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            CarText {
                Layout.fillWidth: true
                maximumLineCount: 1
                text: I18n.t("自动设置日期和时间")
            }
            CarText {
                Layout.fillWidth: true
                maximumLineCount: 1
                elide: Text.ElideRight
                subdued: true
                fontPixelSize: Theme.fontCaption
                text: I18n.t("需要联网；启动后静默校时一次，之后每 6 小时一次")
            }
        }
        Switch {
            id: autoTimeSwitch
            objectName: "timeAutoSyncSwitch"
            implicitHeight: Theme.hit
            Accessible.name: I18n.t("自动设置日期和时间")
            Binding {
                target: autoTimeSwitch
                property: "checked"
                value: settings.timeSync ? settings.timeSync.autoSync : true
            }
            onClicked: if (settings.timeSync) settings.timeSync.setAutoSync(!settings.timeSync.autoSync)
        }
    }

    // 只读的当前日期与时间
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        CarText {
            Layout.fillWidth: true
            maximumLineCount: 1
            text: I18n.t("日期和时间")
        }
        CarText {
            objectName: "timeNowValue"
            maximumLineCount: 1
            subdued: true
            text: settings.nowText
        }
    }
    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: settings.refreshNow()
    }

    // 自动选线路：开 = 线路一回退线路二，关 = 只用自定义服务器
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        CarText {
            Layout.fillWidth: true
            maximumLineCount: 1
            text: I18n.t("自动选择校时线路")
        }
        Switch {
            id: autoRouteSwitch
            objectName: "timeAutoRouteSwitch"
            implicitHeight: Theme.hit
            Accessible.name: I18n.t("自动选择校时线路")
            Binding {
                target: autoRouteSwitch
                property: "checked"
                value: settings.timeSync ? settings.timeSync.apiMode === "auto" : true
            }
            onClicked: if (settings.timeSync)
                           settings.timeSync.apiMode =
                               settings.timeSync.apiMode === "auto" ? "custom" : "auto"
        }
    }
    CarField {
        objectName: "timeSyncHostField"
        Layout.fillWidth: true
        visible: settings.timeSync ? settings.timeSync.apiMode === "custom" : false
        text: settings.timeSync ? settings.timeSync.customHost : ""
        placeholderText: "https://time.example.com"
        onTextChanged: if (settings.timeSync) settings.timeSync.customHost = text
    }

    // 时区（只读）：终端不修改系统时区，这里显示本机偏移；城市钟各按城市时区
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        CarText {
            Layout.fillWidth: true
            maximumLineCount: 1
            text: I18n.t("时区")
        }
        CarText {
            objectName: "timeZoneValue"
            maximumLineCount: 1
            subdued: true
            text: TimeMath.formatOffset(settings.localOffsetMinutes) + " · " + I18n.t("本机")
        }
    }

    // 时间格式：24 小时 / 上午·下午
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        CarText {
            Layout.fillWidth: true
            maximumLineCount: 1
            text: I18n.t("时间格式")
        }
        CarButton {
            objectName: "timeFormat24"
            text: I18n.t("24 小时")
            checked: settings.timeFormat === "24"
            onClicked: settings.timeFormatRequested("24")
        }
        CarButton {
            objectName: "timeFormat12"
            text: I18n.t("上午/下午")
            checked: settings.timeFormat === "12"
            onClicked: settings.timeFormatRequested("12")
        }
    }

    // 上次校时结果 + 手动入口（自动挡下平时不用点）
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        CarText {
            objectName: "timeSyncStatus"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            subdued: true
            text: settings.statusText
        }
        CarButton {
            objectName: "timeSyncReset"
            visible: settings.timeSync ? settings.timeSync.syncedOnce : false
            text: I18n.t("恢复内置时区")
            onClicked: if (settings.timeSync) settings.timeSync.resetZones()
        }
        CarButton {
            objectName: "timeSyncStart"
            visible: settings.timeSync ? !settings.timeSync.syncing : false
            text: I18n.t("立即校时")
            onClicked: if (settings.timeSync) settings.timeSync.syncTime()
        }
    }
    CarText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        subdued: true
        fontPixelSize: Theme.fontCaption
        text: I18n.t("自动校时失败不会打断使用；只有手动点「立即校时」失败时才弹窗说明原因。")
    }

    // ------------------------------------------------------------ 整点报时

    Switch {
        id: chimeSwitch
        objectName: "chimeSwitch"
        Layout.fillWidth: true
        text: I18n.t("整点报时")
        font.pointSize: Theme.body
        implicitHeight: Theme.hit
        enabled: settings.alarmEngine !== null
        Binding {
            target: chimeSwitch
            property: "checked"
            value: settings.alarmEngine ? settings.alarmEngine.chimeEnabled : false
        }
        onClicked: if (settings.alarmEngine)
                       settings.alarmEngine.setChime(!settings.alarmEngine.chimeEnabled,
                                                     settings.alarmEngine.chimeFromHour,
                                                     settings.alarmEngine.chimeToHour)
    }
    RowLayout {
        Layout.fillWidth: true
        visible: settings.alarmEngine ? settings.alarmEngine.chimeEnabled : false
        spacing: Theme.gap / 2
        HourStepper {
            objectName: "chimeFromStepper"
            caption: I18n.t("起")
            value: settings.alarmEngine ? settings.alarmEngine.chimeFromHour : 8
            onAdjusted: function(delta) {
                if (settings.alarmEngine)
                    settings.alarmEngine.chimeFromHour =
                        (settings.alarmEngine.chimeFromHour + delta + 24) % 24
            }
        }
        Item { Layout.fillWidth: true }
        HourStepper {
            objectName: "chimeToStepper"
            caption: I18n.t("止")
            value: settings.alarmEngine ? settings.alarmEngine.chimeToHour : 22
            onAdjusted: function(delta) {
                if (settings.alarmEngine)
                    settings.alarmEngine.chimeToHour =
                        (settings.alarmEngine.chimeToHour + delta + 24) % 24
            }
        }
    }
    CarText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        subdued: true
        text: I18n.t("起止按小时且含两端；起晚于止表示跨午夜。只在整点播报语音，闹钟响铃或再响等待期间跳过。")
    }
    CarText {
        Layout.fillWidth: true
        visible: settings.alarmEngine === null
        wrapMode: Text.Wrap
        text: I18n.t("闹钟引擎未连接，整点报时暂不可用。")
    }

    // 线路失效/校时失败必须弹出来看清：与闹钟响铃弹窗同一套 Popup 机制（挂在
    // Overlay.overlay 上，不新建 Window），换线路时也会弹，不会只在状态行里悄悄换。
    Popup {
        id: syncAlert
        objectName: "timeSyncAlert"
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape
        // 声明式：有 alertText 就弹，清了就收（不再靠信号里手动 open/close）。
        visible: settings.timeSync ? settings.timeSync.alertText !== "" : false
        padding: Theme.gap
        width: Math.min(520 * Theme.scaleFactor, (parent ? parent.width : 800) - Theme.gap * 4)
        background: Rectangle {
            radius: Theme.shapeLarge
            color: Theme.surface
            border.color: Theme.outline
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: Theme.gap / 2
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                MaterialIcon {
                    name: "link_off"
                    Layout.preferredWidth: 28 * Theme.scaleFactor
                    Layout.preferredHeight: 28 * Theme.scaleFactor
                }
                CarText {
                    heading: true
                    Layout.fillWidth: true
                    maximumLineCount: 1
                    text: I18n.t("校时接口不可用")
                }
            }
            CarText {
                objectName: "timeSyncAlertText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: settings.timeSync ? settings.timeSync.alertText : ""
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                CarButton {
                    objectName: "timeSyncAlertClose"
                    accent: true
                    text: I18n.t("知道了")
                    onClicked: {
                        if (settings.timeSync) settings.timeSync.clearAlert()
                        syncAlert.close()
                    }
                }
            }
        }
    }
    // 小时步进器：复用 IconButton 的 48px 热区，只调小时（报时本身只在 :00 触发）。
    component HourStepper: RowLayout {
        id: stepper
        property string caption: ""
        property int value: 0
        signal adjusted(int delta)
        spacing: Theme.gap / 2
        CarText {
            text: stepper.caption
            maximumLineCount: 1
        }
        IconButton {
            iconName: "remove"
            label: I18n.t("减少") + " " + stepper.caption
            onClicked: stepper.adjusted(-1)
        }
        CarText {
            objectName: "chimeHourValue"
            heading: true
            maximumLineCount: 1
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: 76 * Theme.scaleFactor
            text: (stepper.value < 10 ? "0" : "") + stepper.value + ":00"
            Accessible.name: stepper.caption + " " + stepper.value + I18n.t(" 点")
        }
        IconButton {
            iconName: "add"
            label: I18n.t("增加") + " " + stepper.caption
            onClicked: stepper.adjusted(1)
        }
    }
}
