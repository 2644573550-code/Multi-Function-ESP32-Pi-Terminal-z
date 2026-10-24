import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import "WheelStrings.js" as Strings

ApplicationWindow {
    id: window
    width: 1024; height: 600; visible: true
    property string language: "zh"
    title: Strings.t("原生网页方向轮盘", language)
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Switch { id: toggle; text: Strings.t("网页摇杆", window.language); checked: false; implicitHeight: 56 }
            Button { text: Strings.t("放大网页", window.language); implicitHeight: 48; onClicked: web.zoomFactor = Math.min(3,web.zoomFactor+0.25) }
            Button { text: Strings.t("缩小网页", window.language); implicitHeight: 48; onClicked: web.zoomFactor = Math.max(0.5,web.zoomFactor-0.25) }
            Button { text: window.language === "zh" ? "English" : "中文"; implicitHeight: 48; onClicked: window.language = window.language === "zh" ? "en" : "zh" }
            Item { Layout.fillWidth: true }
        }
    }
    WebEngineView { id: web; anchors.fill: parent; url: Qt.resolvedUrl("../tests/page.html") }
    BrowserWheel {
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16
        switchedOn: toggle.checked; targetView: web; language: window.language
        translate: function(text) { return Strings.t(text, window.language) }
    }
}
