import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OrangePiTerminal 1.0

ApplicationWindow {
    id: win
    objectName: "arkpetsPreview"
    width: 1024; height: 600; visible: true
    minimumWidth: 320; minimumHeight: 320
    title: "pi-terminal-Arkpets · 阿米娅本机预览"
    color: "#142334"
    property int backgroundClicks: 0
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.9; height: parent.height * 0.9
        color: "#243950"; radius: 22
        Text { anchors.centerIn: parent; text: "应用窗口\n阿米娅始终显示在此窗口之上"; color: "white"; font.pixelSize: 24; horizontalAlignment: Text.AlignHCenter }
        Button {
            objectName: "backgroundButton"
            anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 20
            text: "底层按钮 " + win.backgroundClicks
            onClicked: win.backgroundClicks++
        }
    }
    property bool desktopPetEnabled:false
    footer: Rectangle {
        id: dock
        height: 64; color: "#0c1723"
        RowLayout {
            anchors.fill: parent; anchors.margins: 8
            Label { text: "任务栏 · 阿米娅本机预览"; color: "white"; Layout.fillWidth: true }
            Button { text: win.desktopPetEnabled?"关闭桌宠":"启动桌宠"; onClicked: win.desktopPetEnabled=!win.desktopPetEnabled }
            Button { text: "桌宠管理"; onClicked: manager.open() }
            Button { objectName: "previewInteract"; text: "互动"; onClicked: pet.interact() }
            Button {text:"坐下";onClicked:pet.setBehavior("sit")}
            Button {text:"躺下";onClicked:pet.setBehavior("sleep")}
            Button { text: pet.roaming ? "暂停行走" : "继续行走"; onClicked: pet.roaming = !pet.roaming }
            Button { text: pet.active ? "隐藏" : "显示"; onClicked: pet.active = !pet.active }
        }
    }
    DesktopPet {
        id: pet
        active:win.desktopPetEnabled
        parent: win.contentItem; anchors.fill: parent; z: 900
        floorY: win.contentItem.height
        petScale: 0.75*manager.library.sizeMultiplier
        modelFolder: manager.library.currentFolder
    }
    PetManager { id: manager; parent: Overlay.overlay; availableBottom:win.contentItem.height }
}
