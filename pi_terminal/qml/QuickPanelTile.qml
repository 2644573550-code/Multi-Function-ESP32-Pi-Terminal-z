import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id:root
    property string label:""
    property string detail:""
    property string iconName:"settings"
    property bool active:false
    property bool blurEnabled:true
    property string blurObjectName:""
    property Item blurCacheTexture:null
    property real uiScale:ApplicationWindow.window?ApplicationWindow.window.uiScale:Theme.scaleFactor
    implicitHeight:88*uiScale
    Layout.preferredHeight:88*uiScale
    implicitWidth:200*Theme.scaleFactor
    padding:Theme.gap/1.5
    topInset:0;bottomInset:0;leftInset:0;rightInset:0
    Accessible.name:label+" · "+detail
    background:GlassSurface {
        objectName:root.blurObjectName
        color:root.active?Theme.accent:Theme.raised
        radius:Theme.shapeLarge;blurEnabled:root.blurEnabled;staticBlur:true
        blurCacheTexture:root.blurCacheTexture
        blurStrength:3.2;tintOpacity:root.active?0.65:0.34
        borderWidth:root.activeFocus?2:0;borderColor:Theme.accent
    }
    contentItem:RowLayout {
        spacing:Theme.gap/2
        MaterialIcon {name:root.iconName;Layout.preferredWidth:24*root.uiScale;Layout.preferredHeight:24*root.uiScale}
        ColumnLayout {Layout.fillWidth:true;spacing:0
            CarText {text:root.label;Layout.fillWidth:true;maximumLineCount:1;elide:Text.ElideRight;color:root.active?Theme.accentInk:Theme.secondary}
            CarText {text:root.detail;Layout.fillWidth:true;maximumLineCount:1;elide:Text.ElideRight;color:root.active?Theme.accentInk:Theme.secondary}
        }
        MaterialIcon {name:"chevron_right";Layout.preferredWidth:20*Theme.scaleFactor;Layout.preferredHeight:20*Theme.scaleFactor}
    }
}
