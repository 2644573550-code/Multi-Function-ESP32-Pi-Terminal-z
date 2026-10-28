import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// 低敏用户选择：只显示显示名，不显示任何健康数据。
// 选人之后必须再输 PIN，刷卡也不能跳过 PIN。
Item {
    id: picker
    // [{userId, displayName, admin}] —— 不含 salt/verifier/DB key
    property var users: []
    property string selectedUserId: ""
    property string message: ""

    signal userChosen(string userId)
    signal refreshRequested()

    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Theme.gap / 2

        CarText {
            Layout.fillWidth: true
            text: I18n.t("选择用户")
            fontPixelSize: Theme.title
            color: Theme.foreground
        }

        CarText {
            Layout.fillWidth: true
            text: picker.message
            visible: picker.message.length > 0
            color: Theme.warning
            fontPixelSize: Theme.fontLabel
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: picker.users
            delegate: CarButton {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.hit
                text: modelData.displayName + (modelData.disabled ? "（" + I18n.t("已停用") + "）" : "")
                enabled: !modelData.disabled
                onClicked: picker.userChosen(modelData.userId)
                Accessible.name: modelData.displayName
            }
        }

        CarText {
            Layout.fillWidth: true
            text: I18n.t("没有可选用户")
            visible: picker.users.length === 0
            color: Theme.outline
            fontPixelSize: Theme.fontLabel
        }

        CarButton {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.hit
            text: I18n.t("刷新")
            onClicked: picker.refreshRequested()
        }
    }
}
