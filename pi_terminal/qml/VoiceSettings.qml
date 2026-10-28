import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 「语音音色」页（section 13）：只负责选择与试听，引擎与回退归 SystemTools。
//
// 边界（与作者约定一致）：
// - 只做界面：音色列表、当前选择与试听都调用 SystemTools 的既有 Q_INVOKABLE；
//   试听按一次正常播报处理（走 speechActive_ 降音、15 s 超时与播报开关）。
// - 全部复用项目既有组件（SettingsGroup / CarButton / CarText / MaterialIcon）与
//   Theme token；热区不小于 Theme.hit（内部有 48px 下限），不引入新依赖、新模糊算法。
// - 不新建 Window：确认弹窗用 Overlay 上的 Popup，与时间页的提示同一套机制。
// - 文案全部走 I18n.t()，中英文词表同步补齐；用户内容不翻译。
// - 微软音色常驻；BanG Dream 只在「测试模式」（彩蛋）开启后出现，并给关闭入口。
Item {
    id: root
    objectName: "voiceSettingsPage"

    property bool active: false
    property real uiScale: 1

    // 列表在页面创建时取一次快照：语言切换、选中态都不重建 Repeater 模型
    //（重建模型会在布局 polish 中销毁带 Layout 的委托，Qt 6.4 下会崩）。
    property var allVoices: []
    Component.onCompleted: root.allVoices = systemTools.voiceOptions
    onActiveChanged: if (active && root.allVoices.length === 0) root.allVoices = systemTools.voiceOptions

    readonly property var microsoftVoices: {
        const list = []
        for (let i = 0; i < allVoices.length; ++i)
            if (allVoices[i].family === "microsoft" && allVoices[i].available) list.push(allVoices[i])
        return list
    }
    readonly property var testVoices: {
        const list = []
        if (!systemTools.voiceTestMode) return list
        for (let i = 0; i < allVoices.length; ++i)
            if (allVoices[i].family === "bangdream" && allVoices[i].available) list.push(allVoices[i])
        return list
    }
    readonly property string currentName: I18n.t(systemTools.voiceName)

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            width: parent.width
            spacing: Theme.gap

            CarText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: I18n.t("当前音色：") + root.currentName
            }
            CarText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                subdued: true
                text: I18n.t("点「试听」会用该音色播报一句话；中文走本地引擎，英文或音色缺失时自动回退系统语音。")
            }

            // ------------------------------------------------------ 微软声音
            SettingsGroup {
                Layout.fillWidth: true
                iconName: "volume_up"
                accentColor: "#10b7a8"
                title: I18n.t("微软声音")
                subtitle: I18n.t("Edge 微软神经网络音色")

                Repeater {
                    model: root.microsoftVoices
                    delegate: VoiceRow { rowData: modelData }
                }
                CarText {
                    Layout.fillWidth: true
                    visible: root.microsoftVoices.length === 0
                    wrapMode: Text.Wrap
                    text: I18n.t("没有找到微软音色数据，请检查安装目录。")
                }
            }

            // ------------------------------------------------ 测试音色（彩蛋）
            SettingsGroup {
                Layout.fillWidth: true
                visible: systemTools.voiceTestMode
                iconName: "volume_up"
                accentColor: "#d889e8"
                title: I18n.t("BanG Dream 测试音色")
                subtitle: I18n.t("测试版：音质与稳定性较差")

                Repeater {
                    model: root.testVoices
                    delegate: VoiceRow { rowData: modelData }
                }
                CarText {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    subdued: true
                    text: I18n.t("测试音色由本地零样本模型逐音节生成，声调与音质不如正式音色；关闭测试模式会自动切回晓晓。")
                }
                CarButton {
                    objectName: "voiceDisableTestMode"
                    Layout.fillWidth: true
                    text: I18n.t("关闭测试模式")
                    onClicked: disableTestMode.open()
                }
            }

        }
    }

    // 关闭测试模式要二次确认：它会把当前音色从角色切回晓晓。
    Popup {
        id: disableTestMode
        objectName: "voiceDisableTestModeConfirm"
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape
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
            CarText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: I18n.t("关闭后 BanG Dream 测试音色会从列表中移除，当前音色切回晓晓。确定关闭？")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap / 2
                Item { Layout.fillWidth: true }
                CarButton {
                    objectName: "voiceDisableTestModeCancel"
                    text: I18n.t("取消")
                    onClicked: disableTestMode.close()
                }
                CarButton {
                    objectName: "voiceDisableTestModeConfirmButton"
                    text: I18n.t("确认关闭")
                    accent: true
                    onClicked: {
                        systemTools.setVoiceTestMode(false)
                        disableTestMode.close()
                    }
                }
            }
        }
    }

    // 一行 = 名称 + 来源 + 试听 + 使用/使用中。热区与文字都走 Theme token。
    component VoiceRow: RowLayout {
        id: voiceRow
        property var rowData: null
        Layout.fillWidth: true
        spacing: Theme.gap / 2
        implicitHeight: Math.max(Theme.hit, labelColumn.implicitHeight)

        ColumnLayout {
            id: labelColumn
            Layout.fillWidth: true
            spacing: 0
            CarText {
                Layout.fillWidth: true
                maximumLineCount: 1
                elide: Text.ElideRight
                text: voiceRow.rowData ? I18n.t(voiceRow.rowData.name) : ""
            }
            CarText {
                Layout.fillWidth: true
                maximumLineCount: 1
                elide: Text.ElideRight
                subdued: true
                fontPixelSize: Theme.fontCaption
                text: (voiceRow.rowData ? voiceRow.rowData.family : "") === "microsoft" ? I18n.t("微软语音") : I18n.t("测试音色")
            }
        }
        CarButton {
            objectName: "voicePreview-" + voiceRow.rowData ? voiceRow.rowData.id : ""
            text: I18n.t("试听")
            onClicked: systemTools.previewVoice(voiceRow.rowData ? voiceRow.rowData.id : "")
        }
        CarButton {
            objectName: "voiceSelect-" + voiceRow.rowData ? voiceRow.rowData.id : ""
            text: voiceRow.rowData && systemTools.voiceId === voiceRow.rowData.id ? I18n.t("使用中") : I18n.t("使用")
            checked: voiceRow.rowData !== null && systemTools.voiceId === voiceRow.rowData.id
            onClicked: systemTools.setVoice(voiceRow.rowData ? voiceRow.rowData.id : "")
        }
    }
}
