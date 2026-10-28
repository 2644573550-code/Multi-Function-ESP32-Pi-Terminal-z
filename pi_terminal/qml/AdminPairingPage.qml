import QtQuick
import QtQuick.Layouts

// 管理员配对 / 撤销 / 授权页面（D9b）。
//
// 后端是 C++ 的 securityService 门面：QML 只调 Q_INVOKABLE 方法，
// 由服务端复核管理员身份、用户归属并写审计。本页不持有任何密钥。
Item {
    id: adminPage
    property string message: ""
    // 选中的用户（授权对象）。管理员不得代成员授予健康数据访问，
    // 因此这里只显示用户列表，实际授予会在 C++ 侧再校验。
    property string targetUserId: ""

    implicitHeight: column.implicitHeight

    // 后端一变更就重读，避免页面显示过期数据。
    Connections {
        target: securityService
        function onChanged() { adminPage.reload() }
    }

    function reload() {
        bridgeModel.clear()
        for (const entry of securityService.trustedBridges()) bridgeModel.append(entry)
        cardModel.clear()
        for (const entry of securityService.boundCards()) cardModel.append(entry)
        auditModel.clear()
        for (const entry of securityService.recentAudit(20)) auditModel.append(entry)
    }

    Component.onCompleted: reload()

    ListModel { id: bridgeModel }
    ListModel { id: cardModel }
    ListModel { id: auditModel }

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

        // 管理员才能进：普通用户看到明确的拒绝，不是空页面。
        CarText {
            Layout.fillWidth: true
            visible: !sessionManager.isAdmin
            text: I18n.t("需要管理员权限")
            color: Theme.warning
            fontPixelSize: Theme.fontLabel
        }

        // 信任库损坏必须报警：它会拒掉所有桥板，但看起来像"还没配对过"。
        CarText {
            Layout.fillWidth: true
            visible: sessionManager.isAdmin && !securityService.healthy()
            text: I18n.t("信任库已损坏，所有桥板都会被拒绝")
            color: Theme.warning
            fontPixelSize: Theme.fontLabel
            wrapMode: Text.WordWrap
        }

        CarText {
            Layout.fillWidth: true
            visible: adminPage.message.length > 0
            text: adminPage.message
            color: Theme.warning
            fontPixelSize: Theme.fontLabel
            wrapMode: Text.WordWrap
        }

        // ---- 已配对桥板 ----
        CarText {
            Layout.fillWidth: true
            text: I18n.t("已配对桥板")
            fontPixelSize: Theme.fontLabel
            color: Theme.outline
            visible: sessionManager.isAdmin
        }
        CarText {
            Layout.fillWidth: true
            visible: sessionManager.isAdmin && bridgeModel.count === 0
            text: I18n.t("还没有配对任何桥板")
            color: Theme.outline
            fontPixelSize: Theme.fontCaption
        }
        Repeater {
            model: bridgeModel
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                CarText {
                    Layout.fillWidth: true
                    text: modelData.bridgeId + "  ·  " + modelData.fingerprint
                    color: Theme.foreground
                    fontPixelSize: Theme.fontLabel
                    elide: Text.ElideMiddle
                }
                CarButton {
                    text: I18n.t("撤销")
                    enabled: sessionManager.isAdmin
                    onClicked: {
                        if (securityService.revokeBridge(modelData.fingerprint))
                            adminPage.message = ""
                        else
                            adminPage.message = securityService.lastMessage
                    }
                }
            }
        }

        // ---- 卡绑定 ----
        CarText {
            Layout.fillWidth: true
            text: I18n.t("已绑定卡片")
            fontPixelSize: Theme.fontLabel
            color: Theme.outline
            visible: sessionManager.isAdmin
        }
        Repeater {
            model: cardModel
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                CarText {
                    Layout.fillWidth: true
                    text: modelData.label
                    color: Theme.foreground
                    fontPixelSize: Theme.fontLabel
                }
                CarButton {
                    text: I18n.t("挂失")
                    enabled: sessionManager.isAdmin
                    onClicked: {
                        securityService.revokeCard(modelData.rawUidHex)
                        adminPage.reload()
                    }
                }
            }
        }
        CarButton {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.hit
            text: I18n.t("全部挂失")
            visible: sessionManager.isAdmin && cardModel.count > 0
            onClicked: {
                securityService.revokeAllCards()
                adminPage.reload()
            }
        }

        // ---- 审计 ----
        CarText {
            Layout.fillWidth: true
            text: I18n.t("最近操作")
            fontPixelSize: Theme.fontLabel
            color: Theme.outline
            visible: sessionManager.isAdmin && auditModel.count > 0
        }
        Repeater {
            model: auditModel
            delegate: CarText {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.action + "  ·  " + modelData.result
                color: modelData.result === "ok" ? Theme.foreground : Theme.warning
                fontPixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }
}
