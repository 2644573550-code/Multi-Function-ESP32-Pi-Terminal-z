import QtQuick

// One Material-themed icon container shared by launcher, drawer and taskbar.
Item {
    id: root
    property string iconName: "apps"
    property real iconScale: 0.52
    property color accentColor: Theme.accent
    readonly property color containerColor: Qt.darker(accentColor, 2.35)
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.containerColor
        border.width: Math.max(1, width * 0.025)
        border.color: Qt.lighter(root.accentColor, 1.18)
    }
    Image {
        visible:root.iconName==="extension"
        anchors.centerIn:parent
        width:Math.min(root.width,root.height)*0.76
        height:width
        source:"qrc:/icons/module-default.svg"
        sourceSize.width:width*2;sourceSize.height:height*2
        fillMode:Image.PreserveAspectFit
        smooth:true
    }
    MaterialIcon {
        visible:root.iconName!=="extension"
        anchors.centerIn: parent
        width: Math.min(root.width, root.height) * root.iconScale
        height: width
        name: root.iconName
        light: true
    }
    Accessible.ignored: true
}
