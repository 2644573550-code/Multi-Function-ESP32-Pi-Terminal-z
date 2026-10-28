import QtQuick
import QtQuick.Layouts

// 切用户/退出前的任务收尾弹窗（D9a / f-day1 §2）。
//
// 有未完成任务时不能切用户：必须让用户显式选择「完成」「取消」或「停止」。
// 这个判断在 SessionManager::login 里也做了一遍（拒绝返回 BUSY），
// 这里是给用户一个可操作的出口，不是唯一的拦截点。
Item {
    id: finishDialog
    property var tasks: []          // [{taskId, deviceId, label}]
    property string message: ""

    signal finishTask(string taskId)
    signal stopAll()

    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Theme.gap

        CarText {
            Layout.fillWidth: true
            text: I18n.t("还有未完成的任务")
            fontPixelSize: Theme.title
            color: Theme.foreground
        }

        CarText {
            Layout.fillWidth: true
            text: I18n.t("切换用户前需要结束或停止这些任务，否则数据会归属错误。")
            color: Theme.outline
            fontPixelSize: Theme.fontLabel
            wrapMode: Text.WordWrap
        }

        CarText {
            Layout.fillWidth: true
            visible: finishDialog.message.length > 0
            text: finishDialog.message
            color: Theme.warning
            fontPixelSize: Theme.fontLabel
        }

        Repeater {
            model: finishDialog.tasks
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
                    text: I18n.t("完成")
                    onClicked: finishDialog.finishTask(modelData.taskId)
                }
            }
        }

        CarButton {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.hit
            text: I18n.t("全部停止")
            onClicked: finishDialog.stopAll()
        }
    }
}
