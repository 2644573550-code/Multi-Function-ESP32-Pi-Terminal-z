import QtQuick
import QtQuick.Layouts

// 锁屏遮罩：隐藏敏感内容，必须重输 PIN 解锁（不得刷卡解锁）。
// 提示不含姓名、读数、精确位置（f-day1 §2）。
Rectangle {
    id: lockScreen
    color: Theme.background
    property string message: ""
    property bool busy: false

    signal pinSubmitted(string pin)
    signal logoutRequested()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.margin * 2, 420 * Theme.scaleFactor)
        spacing: Theme.gap

        MaterialIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Theme.hit
            Layout.preferredHeight: Theme.hit
            name: "backspace"
        }

        CarText {
            Layout.fillWidth: true
            text: I18n.t("已锁定")
            fontPixelSize: Theme.title
            color: Theme.foreground
            horizontalAlignment: Text.AlignHCenter
        }

        PinPad {
            Layout.fillWidth: true
            busy: lockScreen.busy
            message: lockScreen.message
            messageIsError: lockScreen.message.length > 0
            onSubmitted: function(pin) { lockScreen.pinSubmitted(pin) }
            onCancelled: lockScreen.logoutRequested()
        }
    }
}
