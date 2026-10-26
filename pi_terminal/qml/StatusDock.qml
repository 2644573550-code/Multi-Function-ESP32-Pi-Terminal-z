import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Terminal.Native 1.0

Rectangle {
    id:dock;objectName:"systemStatusDock"
    property var status:({})
    property var recentModel
    property int unread:0
    property string clock:""
    property bool appOpen:false
    property string activeEntryId:""
    property bool alarmActive:false
    readonly property var host:ApplicationWindow.window
    readonly property Item backdrop:host && host.blurBackdropItem !== undefined?host.blurBackdropItem:null
    function powerIconFor(value) {
        if(value.powerMode==="external")return "power"
        if(value.powerMode!=="battery")return "battery_unknown"
        if(value.batteryState==="Charging")return "battery_charging_full"
        if(value.batteryState==="Full"||value.batteryPercent>=98)return "battery_full"
        return "battery_"+Math.max(0,Math.min(6,Math.floor(value.batteryPercent*7/100)))+"_bar"
    }
    function powerLabelFor(value) {
        return value.powerMode==="battery"?value.batteryPercent+"%":value.powerMode==="external"?I18n.t("仅外接电源"):I18n.t("电源状态未知")
    }
    readonly property string powerIcon:powerIconFor(status)
    readonly property string powerLabel:powerLabelFor(status)
    signal appDrawerRequested()
    signal statusRequested()
    signal notificationsRequested()
    signal keyboardRequested()
    signal alarmsRequested()
    signal recentRequested(var entry)
    color:"transparent";implicitHeight:Theme.hit+Theme.gap*2
    ShaderEffectSource {
        id:dockSample;parent:dock.backdrop?dock.backdrop.parent:dock;z:-99;width:1;height:1;sourceItem:dock.backdrop;live:true
        sourceRect:{if(!dock.backdrop)return Qt.rect(0,0,1,1);const p=dock.mapToItem(dock.backdrop,0,0);const w=Math.min(dock.width,dock.backdrop.width);const h=Math.min(dock.height,dock.backdrop.height);return Qt.rect(Math.max(0,Math.min(p.x,dock.backdrop.width-w)),Math.max(0,Math.min(p.y,dock.backdrop.height-h)),w,h)}
    }
    BlurItem {objectName:"statusDockBlur";anchors.fill:parent;source:dockSample;levels:systemTools.energySaving?2:4;resolutionScale:systemTools.energySaving?0.25:0.5;strength:2;cornerRadius:Theme.shapeLarge}
    Rectangle {anchors.fill:parent;color:Theme.surface;opacity:0.68;border.color:Theme.outlineVariant;border.width:1}
    RowLayout {
        z:1;anchors.fill:parent;anchors.margins:Theme.gap;spacing:Theme.gap
        IconButton {objectName:"homeButton";iconName:"apps";label:I18n.t("打开应用抽屉");onClicked:dock.appDrawerRequested()}
        Flickable {
            objectName:"taskbarTasks";Layout.fillWidth:true;Layout.minimumWidth:Theme.hit;Layout.preferredHeight:Theme.hit
            contentWidth:taskIcons.implicitWidth;contentHeight:height;clip:true;boundsBehavior:Flickable.StopAtBounds;flickableDirection:Flickable.HorizontalFlick
        RowLayout {
            id:taskIcons;height:parent.height;spacing:Theme.gap/2
            Repeater {model:dock.recentModel
                IconButton {
                    required property var model;iconName:model.iconName
                     contentItem:AppIcon {anchors.centerIn:parent;width:40*Theme.scaleFactor;height:width;iconName:model.iconName;accentColor:model.accentColor}
                    label:(model.minimized?I18n.t("恢复最小化应用："):I18n.t("切换到应用："))+model.title
                    onClicked:dock.recentRequested({entryId:model.entryId,title:model.title,iconName:model.iconName,accentColor:model.accentColor,route:model.route,instance:model.instance,ready:true})
                    Rectangle {anchors.horizontalCenter:parent.horizontalCenter;anchors.bottom:parent.bottom;width:parent.width/3;height:3;color:model.entryId===dock.activeEntryId && !model.minimized?Theme.foreground:Theme.secondary}
                }
            }
        }
        }
        IconButton {objectName:"alarmButton";iconName:"alarm";label:I18n.t("闹钟已开启");visible:dock.alarmActive;onClicked:dock.alarmsRequested()}
        IconButton {objectName:"keyboardButton";iconName:"keyboard";label:I18n.t("打开屏幕键盘");filled:true;accentColor:"#ffffff";visible:dock.width>=480*Theme.scaleFactor;onClicked:dock.keyboardRequested()}
        IconButton {
            visible:dock.width>=540
            objectName:"notificationsButton";iconName:"notifications";label:I18n.t("通知，")+dock.unread+I18n.t("条未读");onClicked:dock.notificationsRequested()
            Rectangle {visible:dock.unread>0;anchors.right:parent.right;anchors.top:parent.top;width:Math.max(32*Theme.scaleFactor,badge.implicitWidth+8*Theme.scaleFactor);height:32*Theme.scaleFactor;radius:height/2;color:Theme.accent
                CarText{id:badge;anchors.centerIn:parent;text:dock.unread>99?"99+":dock.unread.toString();color:Theme.accentInk}}
        }
        Button {
            id:statusButton;objectName:"deviceStatusButton";property bool auditControl:true
            font.pointSize:Theme.body;implicitHeight:Theme.hit;implicitWidth:Math.max(Theme.hit,indicators.implicitWidth)
            padding:0;leftPadding:0;rightPadding:0;topPadding:0;bottomPadding:0;leftInset:0;rightInset:0;topInset:0;bottomInset:0
            Accessible.name:I18n.t("设备状态和快捷设置")+" · "+dock.powerLabel;onClicked:dock.statusRequested()
            background:Rectangle{radius:Theme.shapeLarge;color:statusButton.down?Theme.pressed:"transparent";border.width:statusButton.activeFocus?2:0;border.color:Theme.accent}
            contentItem:RowLayout {
                id:indicators;spacing:Theme.gap
                MaterialIcon {Layout.preferredWidth:24*Theme.scaleFactor;Layout.preferredHeight:24*Theme.scaleFactor
                    visible:dock.width>=420*Theme.scaleFactor
                    name:dock.status.wifiConnected?"wifi":dock.status.networkOnline?"lan":"wifi_off"}
                MaterialIcon {visible:dock.width>=600*Theme.scaleFactor;Layout.preferredWidth:24*Theme.scaleFactor;Layout.preferredHeight:24*Theme.scaleFactor
                    name:dock.status.bluetoothOn?"bluetooth":"bluetooth_disabled"}
                MaterialIcon {visible:dock.width<420*Theme.scaleFactor;name:"settings";Layout.preferredWidth:32*Theme.scaleFactor;Layout.preferredHeight:32*Theme.scaleFactor}
                MaterialIcon {objectName:"statusPowerIcon";Layout.preferredWidth:24*Theme.scaleFactor;Layout.preferredHeight:24*Theme.scaleFactor;name:dock.powerIcon;Accessible.name:dock.powerLabel}
                CarText {objectName:"statusPowerText";text:dock.powerLabel;maximumLineCount:1;visible:dock.width>=480}
                CarText {visible:dock.width>=480;text:dock.clock}
            }
        }
    }
}
