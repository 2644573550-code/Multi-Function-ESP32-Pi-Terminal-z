import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "pictureViewer"
    modal: true
    property var pictures: []
    property int currentIndex: -1
    property real zoom: 1
    property int angle: 0
    readonly property var current: currentIndex >= 0 && currentIndex < pictures.length ? pictures[currentIndex] : ({})
    title: current.name || I18n.t("图片查看器")
    width: parent ? parent.width - 24 : 900
    height: parent ? parent.height - 24 : 550
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    background: Rectangle { color: "#181818"; radius: 20; border.color: "#424242" }
    Overlay.modal: Rectangle { color: "#b3000000" }
    function showImages(rows, index) { pictures = rows; currentIndex = index; resetView(); open() }
    function resetView() { zoom = 1; angle = 0; viewport.contentX = 0; viewport.contentY = 0 }
    function step(delta) { if (pictures.length) { currentIndex = (currentIndex + delta + pictures.length) % pictures.length; resetView() } }
    header: RowLayout {
        CarText { text: root.title; Layout.fillWidth: true; Layout.leftMargin: 16; maximumLineCount: 1; elide: Text.ElideMiddle }
        IconButton { iconName: "close"; label: I18n.t("关闭"); onClicked: root.close() }
    }
    contentItem: ColumnLayout {
        focus: true
        Keys.onLeftPressed: root.step(-1)
        Keys.onRightPressed: root.step(1)
        RowLayout {
            Layout.fillWidth: true
            IconButton { iconName: "arrow_back"; label: I18n.t("上一张"); enabled: root.pictures.length > 1; onClicked: root.step(-1) }
            IconButton { iconName: "arrow_forward"; label: I18n.t("下一张"); enabled: root.pictures.length > 1; onClicked: root.step(1) }
            IconButton { iconName: "zoom_out"; label: I18n.t("缩小"); onClicked: root.zoom = Math.max(0.25, root.zoom / 1.25) }
            IconButton { iconName: "zoom_in"; label: I18n.t("放大"); onClicked: root.zoom = Math.min(8, root.zoom * 1.25) }
            IconButton { iconName: "fullscreen"; label: I18n.t("适应窗口"); onClicked: root.resetView() }
            IconButton { iconName: "refresh"; label: I18n.t("旋转"); onClicked: root.angle = (root.angle + 90) % 360 }
            CarText { text: Math.round(root.zoom * 100) + "%"; Layout.fillWidth: true }
        }
        Flickable {
            id: viewport
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            contentWidth: Math.max(width, width * root.zoom); contentHeight: Math.max(height, height * root.zoom)
            boundsBehavior: Flickable.StopAtBounds
            Image {
                id: picture
                width: viewport.contentWidth; height: viewport.contentHeight
                source: root.current.relative ? systemTools.fileUrl(root.current.relative) : ""
                asynchronous: true; autoTransform: true; fillMode: Image.PreserveAspectFit
                sourceSize: Qt.size(2048, 2048); rotation: root.angle
            }
            CarText { anchors.centerIn: parent; visible: picture.status === Image.Error; text: I18n.t("无法读取图片") }
            BusyIndicator { anchors.centerIn: parent; running: picture.status === Image.Loading }
        }
        ListView {
            id: strip
            Layout.fillWidth: true; Layout.preferredHeight: 76
            orientation: ListView.Horizontal; spacing: 8; clip: true; model: root.pictures
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: 80; height: 72
                Accessible.name: modelData.name
                onClicked: { root.currentIndex = index; root.resetView() }
                background: Rectangle { radius: 8; color: "#282828"; border.width: index === root.currentIndex ? 2 : 0; border.color: "#8ab4f8" }
                contentItem: Image { source: systemTools.fileUrl(modelData.relative); sourceSize: Qt.size(128, 96); asynchronous: true; autoTransform: true; fillMode: Image.PreserveAspectFit }
            }
        }
        CarText { text: (root.currentIndex + 1) + " / " + root.pictures.length + (picture.sourceSize.width ? "，" + picture.sourceSize.width + " × " + picture.sourceSize.height : ""); subdued: true }
    }
}
