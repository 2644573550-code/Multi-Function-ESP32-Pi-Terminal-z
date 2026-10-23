import QtQuick
import QtQuick.Window
import QtQuick.Controls
import "BrowserScroll.js" as BrowserScroll

Rectangle {
    id: root
    objectName: "browserJoystick"
    property var targetView: null
    property real uiScale: 1
    property real maximumSpeed: 900 // Logical pixels/second before page zoom.
    property real deadZone: 0.16
    property real axisX: 0
    property real axisY: 0
    property bool held: false
    property bool pending: false
    property int generation: 0
    property double lastTick: 0
    readonly property real travel: width * 0.30
    readonly property bool moving: held && Math.hypot(axisX, axisY) > deadZone
    signal scrollIssued(real dx, real dy)
    implicitWidth: 112 * uiScale
    implicitHeight: implicitWidth
    radius: width / 2
    color: "#dd20242b"
    border.color: "#aab5c4"
    border.width: 1
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("网页滚动摇杆：拖动以滚动当前网页区域")

    function reset() {
        held = false; axisX = 0; axisY = 0
        pending = false; generation++; watchdog.stop()
    }
    function updateAxes(x, y) {
        const dx = (x - width / 2) / travel
        const dy = (y - height / 2) / travel
        const distance = Math.max(1, Math.hypot(dx, dy))
        axisX = dx / distance; axisY = dy / distance
    }
    function tick() {
        const now = Date.now()
        const elapsed = Math.min(50, Math.max(0, now - lastTick)) / 1000
        lastTick = now
        if (!moving || !targetView || pending) return
        const radius = Math.hypot(axisX, axisY)
        const gain = (radius - deadZone) / Math.max(0.01, 1 - deadZone) / radius
        const zoom = Math.max(0.1, targetView.zoomFactor || 1)
        const dx = axisX * gain * maximumSpeed * elapsed / zoom
        const dy = axisY * gain * maximumSpeed * elapsed / zoom
        pending = true
        const token = generation
        watchdog.restart()
        targetView.runJavaScript(BrowserScroll.scrollScript(dx, dy), function() {
            if (token !== root.generation) return
            root.pending = false; watchdog.stop()
        })
        scrollIssued(dx, dy)
    }
    onTargetViewChanged: reset()
    onVisibleChanged: if (!visible) reset()
    onEnabledChanged: if (!enabled) reset()
    Connections {
        target: root.Window.window
        function onActiveChanged() { if (!root.Window.window.active) root.reset() }
    }
    Timer {
        interval: 16; repeat: true
        running: root.moving && root.visible && root.enabled && !!root.targetView
        onRunningChanged: if (running) root.lastTick = Date.now()
        onTriggered: root.tick()
    }
    Timer { id: watchdog; interval: 1000; onTriggered: root.reset() }
    Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 6; text: "▲"; color: "#dce5ef" }
    Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 6; text: "▼"; color: "#dce5ef" }
    Text { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 8; text: "◀"; color: "#dce5ef" }
    Text { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 8; text: "▶"; color: "#dce5ef" }
    Rectangle {
        width: root.width * 0.36; height: width; radius: width / 2
        x: (root.width - width) / 2 + root.axisX * root.travel
        y: (root.height - height) / 2 + root.axisY * root.travel
        color: root.held ? "#83b8f2" : "#b8c8d8"
        border.color: "#e0ebf6"
    }
    MouseArea {
        anchors.fill: parent
        preventStealing: true
        onPressed: function(mouse) { root.held = true; root.updateAxes(mouse.x, mouse.y) }
        onPositionChanged: function(mouse) { if (pressed) root.updateAxes(mouse.x, mouse.y) }
        onReleased: root.reset()
        onCanceled: root.reset()
    }
}
