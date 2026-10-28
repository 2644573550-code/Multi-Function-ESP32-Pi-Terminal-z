import QtQuick
import QtQuick.Layouts

// 数字键盘：只接受 ASCII 数字，至少 6 位。
// 不显示已输入内容（只显示位数），避免旁人窥屏。
Item {
    id: pinPad
    property int digits: 6
    property string value: ""
    property bool busy: false
    property string message: ""
    property bool messageIsError: false

    signal submitted(string pin)
    signal cancelled()

    implicitHeight: column.implicitHeight

    function press(digit) {
        if (busy || value.length >= digits) return
        value += digit
        if (value.length === digits) { submitted(value); value = "" }
    }
    function backspace() { if (!busy && value.length > 0) value = value.slice(0, -1) }
    function clear() { value = "" }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Theme.gap / 2

        // 位数指示器：只显示圆点，不显示数字。
        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.gap / 3
            Repeater {
                model: pinPad.digits
                Rectangle {
                    width: Theme.body
                    height: Theme.body
                    radius: width / 2
                    color: index < pinPad.value.length ? Theme.accent : "transparent"
                    border.width: 1
                    border.color: Theme.outline
                }
            }
        }

        CarText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: pinPad.message
            visible: pinPad.message.length > 0
            color: pinPad.messageIsError ? Theme.warning : Theme.foreground
            fontPixelSize: Theme.fontLabel
            wrapMode: Text.WordWrap
        }

        GridLayout {
            Layout.alignment: Qt.AlignHCenter
            columns: 3
            rowSpacing: Theme.gap / 2
            columnSpacing: Theme.gap / 2

            Repeater {
                model: ["1", "2", "3", "4", "5", "6", "7", "8", "9"]
                CarButton {
                    Layout.preferredWidth: Theme.hit * 2
                    Layout.preferredHeight: Theme.hit
                    text: modelData
                    enabled: !pinPad.busy
                    onClicked: pinPad.press(modelData)
                    Accessible.name: I18n.t("数字 ") + modelData
                }
            }
            CarButton {
                Layout.preferredWidth: Theme.hit * 2
                Layout.preferredHeight: Theme.hit
                text: I18n.t("取消")
                enabled: !pinPad.busy
                onClicked: pinPad.cancelled()
            }
            CarButton {
                Layout.preferredWidth: Theme.hit * 2
                Layout.preferredHeight: Theme.hit
                text: "0"
                enabled: !pinPad.busy
                onClicked: pinPad.press("0")
                Accessible.name: I18n.t("数字 0")
            }
            CarButton {
                Layout.preferredWidth: Theme.hit * 2
                Layout.preferredHeight: Theme.hit
                text: I18n.t("删除")
                enabled: !pinPad.busy
                onClicked: pinPad.backspace()
                Accessible.name: I18n.t("删除一位")
            }
        }
    }
}
