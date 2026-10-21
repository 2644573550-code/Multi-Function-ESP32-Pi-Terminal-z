import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string label: ""
    property string iconName: "settings"
    property bool checked: false
    property bool toggle: true
    property bool blurEnabled: true
    property string blurObjectName: ""
    signal clicked()
    spacing: 6
    Button {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth:64 * Theme.scaleFactor;Layout.preferredHeight:64 * Theme.scaleFactor
        implicitWidth: 64 * Theme.scaleFactor; implicitHeight: implicitWidth
        Accessible.name: root.label
        topInset:0;bottomInset:0;leftInset:0;rightInset:0
        Accessible.role: root.toggle ? Accessible.CheckBox : Accessible.Button
        Accessible.checked: root.checked
        onClicked: root.clicked()
        background: GlassSurface {
            objectName:root.blurObjectName
            radius: width / 2
            color: root.checked ? Theme.accent : Theme.raised
            opacity: parent.down ? 0.7 : 1
            blurEnabled:root.blurEnabled;blurStrength:3.2;tintOpacity:0.92
            borderWidth: parent.activeFocus ? 2 : 0; borderColor: Theme.foreground
        }
        contentItem: Item {MaterialIcon {anchors.centerIn:parent;width:28*Theme.scaleFactor;height:width;name:root.iconName;light:!root.checked}}
        padding: 18 * Theme.scaleFactor
        ToolTip.visible: hovered; ToolTip.text: root.label
    }
    CarText {text:root.label;Layout.fillWidth:true;horizontalAlignment:Text.AlignHCenter;maximumLineCount:1;elide:Text.ElideRight;font.pointSize:Theme.body*0.85}
}
