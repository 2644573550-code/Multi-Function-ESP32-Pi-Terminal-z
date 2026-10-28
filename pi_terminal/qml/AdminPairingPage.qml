import QtQuick
import QtQuick.Layouts

// 管理员配对/撤销页面（D9a）。
//
// 后端归 D 的 TrustStore（T-03），本页只调接口。
// TrustStore 未实现前用 stub：从 sessionManager 拿管理员状态，
// 配对列表为空并明确标注"后端未就绪"，不假装成功。
Item {
    id: adminPage
    property var bridges: []      // [{id, name, fingerprint, moduleCount}]
    property var modules: []      // [{deviceId, displayName, fingerprint}]
    property bool backendReady: false
    property string message: ""

    signal pairRequested(string kind, string id)
    signal revokeRequested(string kind, string id)

    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Theme.gap

        CarText {
            Layout.fillWidth: true
            text: I18n.t("设备配对与撤销")
            fontPixelSize: Theme.title
            color: Theme.foreground
        }

        // 管理员才能进：普通用户看到的是明确的拒绝，不是空页面。
        CarText {
            Layout.fillWidth: true
            visible: !sessionManager.isAdmin
            text: I18n.t("需要管理员权限")
            color: Theme.warning
            fontPixelSize: Theme.fontLabel
        }

        CarText {
            Layout.fillWidth: true
            visible: adminPage.message.length > 0
            text: adminPage.message
            color: Theme.warning
            fontPixelSize: Theme.fontLabel
            wrapMode: Text.WordWrap
        }

        CarText {
            Layout.fillWidth: true
            visible: !adminPage.backendReady && sessionManager.isAdmin
            text: I18n.t("信任存储后端尚未就绪，无法配对或撤销")
            color: Theme.outline
            fontPixelSize: Theme.fontLabel
            wrapMode: Text.WordWrap
        }

        CarText {
            Layout.fillWidth: true
            text: I18n.t("已配对桥板")
            fontPixelSize: Theme.fontLabel
            color: Theme.outline
            visible: sessionManager.isAdmin
        }
        Repeater {
            model: adminPage.bridges
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                CarText {
                    Layout.fillWidth: true
                    text: modelData.name + "  ·  " + modelData.fingerprint
                    color: Theme.foreground
                    fontPixelSize: Theme.fontLabel
                    elide: Text.ElideMiddle
                }
                CarButton {
                    text: I18n.t("撤销")
                    enabled: sessionManager.isAdmin && adminPage.backendReady
                    onClicked: adminPage.revokeRequested("bridge", modelData.id)
                }
            }
        }

        CarText {
            Layout.fillWidth: true
            text: I18n.t("已配对模块")
            fontPixelSize: Theme.fontLabel
            color: Theme.outline
            visible: sessionManager.isAdmin
        }
        Repeater {
            model: adminPage.modules
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                CarText {
                    Layout.fillWidth: true
                    text: modelData.displayName + "  ·  " + modelData.fingerprint
                    color: Theme.foreground
                    fontPixelSize: Theme.fontLabel
                    elide: Text.ElideMiddle
                }
                CarButton {
                    text: I18n.t("撤销")
                    enabled: sessionManager.isAdmin && adminPage.backendReady
                    onClicked: adminPage.revokeRequested("module", modelData.id)
                }
            }
        }

        CarButton {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.hit
            text: I18n.t("配对新设备")
            visible: sessionManager.isAdmin
            enabled: adminPage.backendReady
            onClicked: adminPage.pairRequested("bridge", "")
        }
    }
}
