import QtQuick
import QtQuick.Layouts

// 登录页：选人 → PIN。刷卡只能跳到这里，不能直接登录。
// 提示文案不含姓名、读数、精确位置（§2）。
Item {
    id: loginPage
    property var users: []
    property string selectedUserId: ""
    property string selectedDisplayName: ""
    // 刷卡选中的用户：进入本页时直接预选，但仍必须输入 PIN。
    property string preSelectedUserId: ""
    property string preSelectedDisplayName: ""
    onPreSelectedUserIdChanged: {
        if (preSelectedUserId === "") return
        selectedUserId = preSelectedUserId
        selectedDisplayName = preSelectedDisplayName
    }
    property string message: ""
    property bool messageIsError: false
    property bool busy: false

    // 服务端返回的剩余等待时间（毫秒）；> 0 时禁用键盘并倒计时。
    property int retryAfterMs: 0

    signal pinSubmitted(string userId, string pin)
    signal cancelled()
    signal refreshRequested()

    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Theme.gap

        CarText {
            Layout.fillWidth: true
            text: loginPage.selectedUserId === "" ? I18n.t("登录") : loginPage.selectedDisplayName
            fontPixelSize: Theme.title
            color: Theme.foreground
            horizontalAlignment: Text.AlignHCenter
        }

        Loader {
            Layout.fillWidth: true
            active: loginPage.selectedUserId === ""
            visible: active
            sourceComponent: UserPicker {
                users: loginPage.users
                message: loginPage.message
                onUserChosen: function(userId) {
                    loginPage.selectedUserId = userId
                    for (const user of loginPage.users)
                        if (user.userId === userId) loginPage.selectedDisplayName = user.displayName
                }
                onRefreshRequested: loginPage.refreshRequested()
            }
        }

        Loader {
            Layout.fillWidth: true
            active: loginPage.selectedUserId !== ""
            visible: active
            sourceComponent: PinPad {
                busy: loginPage.busy || loginPage.retryAfterMs > 0
                message: loginPage.message
                messageIsError: loginPage.messageIsError
                onSubmitted: function(pin) { loginPage.pinSubmitted(loginPage.selectedUserId, pin) }
                onCancelled: {
                    loginPage.selectedUserId = ""
                    loginPage.message = ""
                    loginPage.cancelled()
                }
            }
        }

        // 限速倒计时：只显示剩余秒数，不透露账号是否存在。
        CarText {
            Layout.fillWidth: true
            visible: loginPage.retryAfterMs > 0
            text: I18n.t("请等待") + " " + Math.ceil(loginPage.retryAfterMs / 1000) + " " + I18n.t("秒")
            color: Theme.outline
            fontPixelSize: Theme.fontLabel
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
