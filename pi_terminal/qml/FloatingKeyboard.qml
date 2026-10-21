import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Popup {
    id: keyboard
    objectName: "floatingKeyboard"
    property real uiScale: 1
    property var hostWindow
    property string pinyin: ""
    property bool chineseMode: true
    property bool uppercase: false
    property bool symbolMode: false
    property var currentCandidates: []
    property bool dictionaryLoaded: false
    property int candidatePage: 0
    parent:Overlay.overlay
    modal:false;dim:false;focus:false
    closePolicy:Popup.NoAutoClose
    padding:0;margins:8
    width:Math.min(parent?parent.width-16:680,680)
    height:Math.min(parent?parent.height-16:268,268)
    readonly property real keyWidth: Math.floor((width-12-9*4)/10)
    readonly property real keyHeight: 48
    onVisibleChanged: if (visible) {systemTools.captureKeyboardTarget();Qt.callLater(ensureDictionaryLoaded);Qt.callLater(positionNearBottom)} else pinyin=""
    onPinyinChanged: { candidatePage = 0; refreshCandidates() }

    function positionNearBottom() {
        const host = parent
        if (!host) return
        x = Math.max(8, (host.width - width) / 2)
        y = Math.max(8, host.height - height - 76)
        clampPosition()
    }
    function show(){open()}
    function hide(){close()}
    function clampPosition(){if(parent){x=Math.max(8,Math.min(x,parent.width-width-8));y=Math.max(8,Math.min(y,parent.height-height-8))}}
    Connections {target:keyboard.parent;function onWidthChanged(){keyboard.clampPosition()}function onHeightChanged(){keyboard.clampPosition()}}
    function ensureDictionaryLoaded() {
        if (dictionaryLoaded) return
        dictionaryLoaded = true
        refreshCandidates()
    }
    function refreshCandidates() {
        if (!dictionaryLoaded || !pinyin) { currentCandidates = []; return }
        currentCandidates=systemTools.pinyinCandidates(pinyin)
    }
    function candidates() { return currentCandidates.slice(candidatePage*4,candidatePage*4+4) }
    function commitCandidate(value) { systemTools.recordPinyinSelection(pinyin,value);systemTools.keyboardCommit(value);pinyin="" }
    function typeLetter(letter) {
        if (symbolMode) { systemTools.keyboardCommit(letter); return }
        if (!chineseMode) { systemTools.keyboardCommit(uppercase?letter.toUpperCase():letter); return }
        if (pinyin.length < 32) pinyin += letter.toLowerCase()
    }
    function backspace() {
        if(pinyin.length)pinyin=pinyin.slice(0,-1)
        else systemTools.keyboardBackspace()
    }
    function finishComposition() {
        const values = currentCandidates
        if (values.length) commitCandidate(values[0])
        else if (pinyin) { systemTools.keyboardCommit(pinyin); pinyin = "" }
    }

    contentItem:Rectangle {
        radius: Theme.shapeLarge
        color: Theme.surfaceContainerHigh
        border.color: Theme.outline
        border.width: 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth:0
                spacing:4
                Layout.preferredHeight: 48
                MouseArea {
                    Layout.preferredWidth:72
                    Layout.fillHeight: true
                    cursorShape: Qt.SizeAllCursor
                    property point pressPoint
                    onPressed: pressPoint = Qt.point(mouse.x, mouse.y)
                    onPositionChanged: if (pressed) {keyboard.x+=mouse.x-pressPoint.x;keyboard.y+=mouse.y-pressPoint.y;keyboard.clampPosition()}
                    CarText {anchors.fill:parent;text:keyboard.pinyin||I18n.t("全拼");fontPixelSize:16;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;maximumLineCount:1;elide:Text.ElideRight;subdued:true}
                }
                CarText {visible:keyboard.pinyin!==""&&keyboard.currentCandidates.length===0;text:I18n.t("无候选");subdued:true}
                CarText {visible:keyboard.pinyin==="";text:I18n.t("输入拼音后选择候选");fontPixelSize:16;subdued:true;Layout.fillWidth:true;maximumLineCount:1;elide:Text.ElideRight}
                IconButton {
                    enabled:keyboard.candidatePage>0;Layout.minimumWidth:0;Layout.maximumWidth:48;Layout.preferredWidth:48;Layout.preferredHeight:48;padding:12;topInset:0;bottomInset:0
                    iconName:"chevron_left";glyphSize:20;label:I18n.t("上一页候选")
                    onClicked:keyboard.candidatePage=Math.max(0,keyboard.candidatePage-1)
                }
                Repeater {
                    model: keyboard.candidates()
                    delegate: CarButton {
                        objectName:"candidateButton"; required property string modelData
                        text:modelData;Layout.fillWidth:true;Layout.minimumWidth:0;Layout.preferredWidth:1;implicitWidth:0;implicitHeight:48;fontPixelSize:18
                        leftPadding:4;rightPadding:4
                        contentItem:Text {text:modelData;color:parent.textColor;font:parent.font;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight;maximumLineCount:1}
                        onClicked:keyboard.commitCandidate(modelData)
                    }
                }
                IconButton {
                    enabled:(keyboard.candidatePage+1)*4<keyboard.currentCandidates.length;Layout.minimumWidth:0;Layout.maximumWidth:48;Layout.preferredWidth:48;Layout.preferredHeight:48;padding:12;topInset:0;bottomInset:0
                    iconName:"chevron_right";glyphSize:20;label:I18n.t("下一页候选")
                    onClicked:keyboard.candidatePage++
                }
                IconButton {objectName:"keyboardClose";iconName:"close";label:I18n.t("关闭屏幕键盘");glyphSize:20;padding:0;Layout.preferredWidth:48;Layout.preferredHeight:48;onClicked:keyboard.hide()}
            }
            Repeater {
                model: keyboard.symbolMode ? ["1234567890", "@#%&*()-_", "⇧.,:/?!⌫"] : ["qwertyuiop", "asdfghjkl", "⇧zxcvbnm⌫"]
                delegate: RowLayout {
                    required property string modelData
                     Layout.fillWidth: true
                     Layout.minimumWidth:0
                    Layout.preferredHeight: keyboard.keyHeight
                    spacing: 4
                    Layout.alignment:Qt.AlignHCenter
                    Repeater {
                        model: parent.modelData.split("")
                         delegate: KeyboardKey {
                            required property string modelData
                             objectName:"letterKey"
                             Layout.minimumWidth:0
                             Layout.preferredWidth:keyboard.keyWidth
                             Layout.maximumWidth:keyboard.keyWidth
                            Layout.fillHeight:true
                            text: keyboard.symbolMode ? modelData : keyboard.uppercase ? modelData.toUpperCase() : modelData
                             font.pixelSize: 20
                             padding: 2
                            background: Rectangle { radius: Theme.shapeSmall; color: parent.down ? Theme.pressed : Theme.raised; border.color:Theme.outlineVariant; border.width:1 }
                            onClicked:{
                                if(modelData==="⌫")keyboard.backspace()
                                else if(modelData==="⇧")keyboard.uppercase=!keyboard.uppercase
                                else keyboard.typeLetter(modelData)
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                KeyboardKey {
                    text: keyboard.symbolMode ? "ABC" : "123"
                    implicitWidth:64;implicitHeight:48;font.pixelSize:16
                    onClicked: { keyboard.finishComposition(); keyboard.symbolMode = !keyboard.symbolMode }
                }
                KeyboardKey {
                    text: keyboard.chineseMode ? I18n.t("中") : "EN"
                    implicitWidth:64;implicitHeight:48;font.pixelSize:16
                    onClicked: { keyboard.finishComposition(); keyboard.chineseMode = !keyboard.chineseMode }
                }
                KeyboardKey {
                    text: "⌫"; Accessible.name: I18n.t("退格")
                    implicitWidth:64;implicitHeight:48;font.pixelSize:16
                    onClicked: keyboard.backspace()
                }
                KeyboardKey {
                    text:I18n.t("空格");Layout.fillWidth:true;implicitHeight:48;font.pixelSize:16
                    onClicked: keyboard.pinyin ? keyboard.finishComposition() : systemTools.keyboardCommit(" ")
                }
                KeyboardKey {
                    text:I18n.t("回车");implicitWidth:76;implicitHeight:48;font.pixelSize:16
                    onClicked: { keyboard.finishComposition(); systemTools.keyboardReturn() }
                }
            }
        }
    }
}
