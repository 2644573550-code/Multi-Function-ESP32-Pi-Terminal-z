import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

FocusScope {
    id: splash
    objectName: "startupSplash"
    property bool interfaceReady: false
    property bool wallpaperReady: false
    property bool frameReady: false
    property bool cacheReady: false
    property bool wallpaperFailed: false
    property bool minimumElapsed: false
    property bool cacheDeadlineElapsed: false
    readonly property bool usingFallback: cacheDeadlineElapsed && !cacheReady
    // Ordered milestones, not elapsed-time estimates of work completed.
    readonly property int completedSteps: !interfaceReady ? 0 : !wallpaperReady ? 1
        : !frameReady ? 2 : !(cacheReady || cacheDeadlineElapsed) ? 3 : 4
    readonly property real progress: completedSteps / 4
    readonly property bool canFinish: completedSteps === 4 && minimumElapsed
    readonly property var steps: [I18n.t("读取界面与偏好设置"), I18n.t("加载桌面壁纸"),
        I18n.t("呈现桌面首帧"), I18n.t("准备窗口与面板模糊缓存")]
    readonly property string statusText: completedSteps < 4 ? steps[completedSteps]
        : usingFallback ? I18n.t("缓存预热较慢，使用兼容渲染进入桌面") : I18n.t("准备就绪")
    signal finished()

    focus: true
    Keys.onPressed: event => event.accepted = true
    Keys.onReleased: event => event.accepted = true
    Material.theme: Material.Dark
    Material.accent: Theme.accent
    Material.foreground: Theme.foreground
    onCanFinishChanged: if (canFinish) exitAnimation.start()

    Timer { interval: 1100; running: splash.frameReady && !splash.minimumElapsed; onTriggered: splash.minimumElapsed = true }
    // Native per-surface fallback already exists. Never call a timed-out cache ready.
    Timer { interval: 6000; running: !splash.cacheReady; onTriggered: splash.cacheDeadlineElapsed = true }

    Rectangle { anchors.fill: parent; color: Theme.background }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    ColumnLayout {
        id: content
        objectName: "startupContent"
        anchors.centerIn: parent
        width: Math.min(480, Math.max(0, splash.width - 48))
        spacing: splash.height < 400 ? 16 : 24
        // Keep the first submitted frame readable even if initialization stalls
        // the GUI thread before the animation clock can advance.
        opacity: 0.72
        scale: 0.96

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
            spacing: 16
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 48; Layout.preferredHeight: 48
                radius: 16; color: Theme.primaryContainer
                MaterialIcon { anchors.centerIn: parent; name: "extension"; width: 24; height: 24 }
            }
            Label {
                Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter
                text: I18n.t("模块化终端")
                color: Theme.foreground; font.pixelSize: 24; font.weight: Font.Normal
                wrapMode: Text.WordWrap
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: details.implicitHeight + 48
            radius: 16; color: Theme.surfaceContainer
            ColumnLayout {
                id: details
                anchors.fill: parent; anchors.margins: 24; spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: I18n.t("正在启动"); color: Theme.foreground; font.pixelSize: 16; Layout.fillWidth: true }
                    Label { objectName: "startupPercent"; text: Math.round(splash.progress * 100) + "%"; color: Theme.accent; font.pixelSize: 14 }
                }
                ProgressBar {
                    id: progressBar
                    objectName: "startupProgress"
                    Layout.fillWidth: true
                    from: 0; to: 1; value: splash.progress
                    Accessible.name: I18n.t("启动进度")
                    padding: 0
                    background: Item {
                        implicitHeight: 4
                        Rectangle {
                            x: Math.min(parent.width, progressBar.visualPosition * parent.width + (progressBar.value > 0 ? 4 : 0))
                            width: parent.width - x; height: 4; radius: 2
                            color: Theme.outlineVariant
                        }
                        Rectangle { anchors.right: parent.right; width: 4; height: 4; radius: 2; color: Theme.accent }
                    }
                    contentItem: Item {
                        implicitHeight: 4
                        Rectangle { width: progressBar.visualPosition * parent.width; height: 4; radius: 2; color: Theme.accent }
                    }
                }
                ColumnLayout {
                    visible: splash.height >= 440
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                      model: splash.steps
                      delegate: RowLayout {
                        required property int index
                        required property string modelData
                        objectName: "startupStep" + index
                        Layout.fillWidth: true
                        Layout.minimumHeight: splash.height < 560 ? 48 : 56
                        spacing: 16
                        Item {
                            objectName: "startupStepIconSlot" + index
                            Layout.preferredWidth: 24; Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            MaterialIcon {
                                objectName: "startupStepIcon" + index
                                anchors.fill: parent
                                name: index < splash.completedSteps ? "check_circle" : "pending"
                                opacity: index <= splash.completedSteps ? 1 : 0.38
                            }
                        }
                        Label {
                            objectName: "startupStepText" + index
                            Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter
                            text: index === 1 && splash.wallpaperFailed ? I18n.t("壁纸不可用，已使用默认背景")
                                : index === 3 && splash.usingFallback ? I18n.t("模糊缓存稍后就绪，使用兼容渲染") : modelData
                            color: index <= splash.completedSteps ? Theme.foreground : Theme.disabledText
                            font.pixelSize: 14; lineHeight: 20; lineHeightMode: Text.FixedHeight
                            wrapMode: Text.WordWrap
                        }
                      }
                    }
                }
            }
        }
        Label {
            objectName: "startupStatus"
            Layout.fillWidth: true
            text: splash.statusText
            color: Theme.disabledText; font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            Accessible.name: text
        }
    }
    ParallelAnimation {
        running: splash.frameReady
        NumberAnimation { target: content; property: "opacity"; to: 1; duration: 240 }
        NumberAnimation { target: content; property: "scale"; to: 1; duration: 360; easing.type: Easing.OutCubic }
    }
    SequentialAnimation {
        id: exitAnimation
        PauseAnimation { duration: 200 }
        NumberAnimation { target: splash; property: "opacity"; to: 0; duration: 240; easing.type: Easing.InOutCubic }
        ScriptAction { script: splash.finished() }
    }
}
