import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

    // 供页面/测试观察：线路失效弹窗是否在屏上。
    readonly property bool alertPopupOpen: syncAlert.visible

    iconName: "schedule"
    accentColor: "#8f7bff"
    title: I18n.t("时间与报时")
    subtitle: I18n.t("世界时钟校时与整点语音报时")

    // ------------------------------------------------------------ 校时方式

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        CarText {
            Layout.fillWidth: true
            maximumLineCount: 1
            text: I18n.t("校时方式")
        }
        CarButton {
            objectName: "timeSyncModeAuto"
            text: I18n.t("自动")
            checked: settings.timeSync ? settings.timeSync.apiMode === "auto" : true
            onClicked: if (settings.timeSync) settings.timeSync.apiMode = "auto"
        }
        CarButton {
            objectName: "timeSyncModeCustom"
            text: I18n.t("自定义服务器")
            checked: settings.timeSync ? settings.timeSync.apiMode === "custom" : false
            onClicked: if (settings.timeSync) settings.timeSync.apiMode = "custom"
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
    CarText {
        Layout.fillWidth: true
        visible: settings.timeSync ? settings.timeSync.apiMode === "custom" : false
        wrapMode: Text.Wrap
        subdued: true
        text: I18n.t("自定义服务器需兼容 worldtimeapi.org 的返回格式（/api/timezone/<区域> → utc_offset、unixtime），失败不会自动改用公共接口。")
    }

    // ------------------------------------------------------------ 校时动作

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        CarButton {
            objectName: "timeSyncStart"
            accent: true
            text: settings.timeSync && settings.timeSync.syncing
                  ? I18n.t("同步中") + "…" : I18n.t("网络校时")
            enabled: settings.timeSync ? !settings.timeSync.syncing : false
            onClicked: if (settings.timeSync) settings.timeSync.syncTime()
        }
        CarButton {
            objectName: "timeSyncReset"
            visible: settings.timeSync ? settings.timeSync.syncedOnce : false
            text: I18n.t("恢复内置时区")
            onClicked: if (settings.timeSync) settings.timeSync.resetZones()
        }
        Item { Layout.fillWidth: true }
    }

    // 接口队列：名字后面跟一个状态点（绿=可用，红=不可用，灰=本次未测）。
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.gap
        Repeater {
            model: settings.timeSync ? settings.timeSync.providerIds : []
            delegate: RowLayout {
                required property var modelData
                spacing: 6 * Theme.scaleFactor
                CarText {
                    maximumLineCount: 1
                    elide: Text.ElideMiddle
                    text: settings.timeSync ? settings.timeSync.providerLabel(modelData) : ""
                }
                Rectangle {
                    objectName: "timeSyncProviderDot"
                    width: 10 * Theme.scaleFactor
                    height: width
                    radius: width / 2
                    color: {
                        const state = settings.timeSync ? settings.timeSync.providerStateOf(modelData) : 0
                        return state === 1 ? "#5ad59a" : state === -1 ? "#ff6584" : Theme.outlineVariant
                    }
                    Accessible.name: (settings.timeSync ? settings.timeSync.providerLabel(modelData) : "") + " " +
                        (!settings.timeSync ? I18n.t("未测试")
                         : settings.timeSync.providerStateOf(modelData) === 1 ? I18n.t("可用")
                         : settings.timeSync.providerStateOf(modelData) === -1 ? I18n.t("不可用") : I18n.t("未测试"))
                }
            }
        }
        Item { Layout.fillWidth: true }
    }

    CarText {
        objectName: "timeSyncStatus"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: settings.timeSync ? settings.timeSync.status : ""
    }
    RowLayout {
        Layout.fillWidth: true
        visible: settings.timeSync ? settings.timeSync.alertText !== "" : false
        CarButton {
            objectName: "timeSyncAlertDismiss"
            text: I18n.t("知道了")
            onClicked: if (settings.timeSync) settings.timeSync.clearAlert()
        }
        Item { Layout.fillWidth: true }
    }
    CarText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        subdued: true
        text: I18n.t("校时只在你点击时联网；未校时的世界时钟用内置标准时区，不含夏令时规则。")
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
