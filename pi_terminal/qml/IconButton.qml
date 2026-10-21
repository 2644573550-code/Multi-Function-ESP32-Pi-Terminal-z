import QtQuick
import QtQuick.Controls

Button {
    id:button
    property string iconName:"apps"
    property string label:""
    property color accentColor:Theme.accent
    property bool filled:false
    property bool auditControl:true
    property real glyphSize:Theme.icon
    font.pointSize:Theme.body
    implicitWidth:Theme.hit;implicitHeight:Theme.hit
    padding:16*Theme.scaleFactor;leftPadding:padding;rightPadding:padding;topPadding:padding;bottomPadding:padding
    topInset:0;bottomInset:0;leftInset:0;rightInset:0
    focusPolicy:Qt.StrongFocus
    Accessible.name:label
    ToolTip.text:label;ToolTip.visible:(hovered || down) && label!=="";ToolTip.delay:down?0:800
    background:Rectangle {
        radius:Theme.shapeFull;color:button.down?Theme.pressed:button.filled?button.accentColor:"transparent"
        border.width:button.activeFocus?2:0;border.color:Theme.accent
    }
    contentItem:Item {
        MaterialIcon {anchors.centerIn:parent;width:button.glyphSize;height:width;name:button.iconName;light:!button.filled || button.down}
    }
}
