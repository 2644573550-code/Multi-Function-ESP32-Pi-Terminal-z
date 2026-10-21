import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string iconName: "apps"
    property url customIcon: ""
    property string accessibleLabel
    property bool selected: false
    property real uiScale: 1
    implicitWidth: 50 * uiScale
    implicitHeight: 50 * uiScale
    padding: 8 * uiScale
    Accessible.role: Accessible.Button
    Accessible.name: accessibleLabel
    Accessible.description: selected ? accessibleLabel + I18n.t("，已选中") : accessibleLabel
    background: Rectangle {
        radius: Theme.shapeMedium
        color: control.pressed ? Theme.pressed : control.selected ? Theme.accent : Theme.raised
        border.width: control.activeFocus ? 2 : 1
        border.color: control.selected ? Theme.accent : Theme.outline
    }
    contentItem: Item {
        Image {
            visible:control.customIcon.toString()!==""
            anchors.centerIn:parent;width:Theme.icon;height:width
            source:control.customIcon;sourceSize.width:width*2;sourceSize.height:height*2
            fillMode:Image.PreserveAspectFit;smooth:true
        }
        MaterialIcon {visible:control.customIcon.toString()==="";anchors.centerIn:parent;width:Theme.icon;height:width;name:control.iconName;light:!control.selected}
    }
}
