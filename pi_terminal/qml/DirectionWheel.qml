import QtQuick
import QtQuick.Controls
import "WheelStrings.js" as Strings

Item {
    id: root
    implicitWidth: 184
    implicitHeight: 184
    property real deadZone: 0.16
    property real offsetX: 0
    property real offsetY: 0
    property real strength: 0
    property int axisX: 0
    property int axisY: 0
    property bool held: false
    property string language: "zh"
    property var translate: function(text) { return Strings.t(text, root.language) }
    readonly property real travel: Math.min(width, height) * 0.30
    readonly property bool moving: held && strength > 0
    signal stopped()
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: root.translate("网页滚动方向轮盘")
    Accessible.description: root.translate("按住拖动或按方向键，松开停止；Escape 停止")

    function reset() {
        held = false; strength = 0; axisX = 0; axisY = 0
        offsetX = 0; offsetY = 0; stopped()
    }
    function sample(x, y) {
        const dx = (x - width / 2) / Math.max(1, travel)
        const dy = (y - height / 2) / Math.max(1, travel)
        const distance = Math.hypot(dx, dy)
        const limit = Math.max(1, distance)
        offsetX = dx / limit; offsetY = dy / limit
        strength = Math.max(0, (Math.min(1, distance) - deadZone) / (1 - deadZone))
        axisX = 0; axisY = 0
        if (strength > 0) {
            if (Math.abs(dx) >= Math.abs(dy)) axisX = dx > 0 ? 1 : -1
            else axisY = dy > 0 ? 1 : -1
        }
    }
    onVisibleChanged: if (!visible) reset()
    onEnabledChanged: if (!enabled) reset()
    onActiveFocusChanged: if (!activeFocus && held) reset()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) { reset(); event.accepted = true; return }
        const x = event.key === Qt.Key_Right ? 1 : event.key === Qt.Key_Left ? -1 : 0
        const y = event.key === Qt.Key_Down ? 1 : event.key === Qt.Key_Up ? -1 : 0
        if (!x && !y) return
        held = true; axisX = x; axisY = y; offsetX = x * 0.65; offsetY = y * 0.65
        strength = 0.65; event.accepted = true
    }
    Keys.onReleased: function(event) {
        if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].indexOf(event.key) >= 0) {
            if (!event.isAutoRepeat) reset()
            event.accepted = true
        }
    }
    Rectangle {
        anchors.fill: parent; radius: width / 2; color: "#172d49"
        border.color: root.activeFocus ? "#ffd45c" : "#a8c2df"; border.width: 3
    }
    Canvas {
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const r = Math.min(width, height) / 2 - 4
            const a = r / Math.sqrt(2)
            ctx.strokeStyle = "#a8c2df"; ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(width/2-a, height/2-a); ctx.lineTo(width/2+a, height/2+a)
            ctx.moveTo(width/2+a, height/2-a); ctx.lineTo(width/2-a, height/2+a)
            ctx.stroke()
        }
    }
    Repeater {
        model: [{label:"上",x:0,y:-1}, {label:"右",x:1,y:0}, {label:"下",x:0,y:1}, {label:"左",x:-1,y:0}]
        delegate: Text {
            required property var modelData
            width: 36; height: 36; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            x: root.width/2 - 18 + modelData.x * root.width * 0.36
            y: root.height/2 - 18 + modelData.y * root.height * 0.36
            text: root.translate(modelData.label); color: "white"; font.pixelSize: 18; font.bold: true
        }
    }
    Rectangle {
        width: root.width * 0.36; height: width; radius: width/2
        x: (root.width-width)/2 + root.offsetX*root.travel
        y: (root.height-height)/2 + root.offsetY*root.travel
        color: root.held ? "#8bc3ff" : "#c2d8ed"; border.color: "white"; border.width: 3
    }
    MouseArea {
        anchors.fill: parent; preventStealing: true
        onPressed: function(mouse) {
            if (Math.hypot(mouse.x-width/2, mouse.y-height/2) > width/2) { mouse.accepted = false; return }
            root.forceActiveFocus(); root.held = true; root.sample(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) { if (pressed) root.sample(mouse.x, mouse.y) }
        onReleased: root.reset()
        onCanceled: root.reset()
    }
}
