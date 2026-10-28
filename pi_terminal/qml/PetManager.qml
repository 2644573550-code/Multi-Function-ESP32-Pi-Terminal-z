import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Terminal.Native 1.0

// M3 list-detail: 24dp outer spacing, 16dp list insets, 72dp two-line
// rows, explicit state colors and >=48dp touch targets. No glass backgrounds.
Dialog {
    id: root
    objectName: "petManagerDialog"
    // Like built-in application surfaces, leave the system taskbar usable.
    // Only the explicit download-consent dialog is modal.
    modal: false
    dim: false
    closePolicy: Popup.NoAutoClose
    padding: 0
    font.family: Qt.application.font.family
    property real availableBottom: parent ? parent.height : 680
    property var virtualKeyboard: null
    readonly property real usableHeight: Math.max(0,Math.min(availableBottom,
        virtualKeyboard && virtualKeyboard.visible ? virtualKeyboard.y-8 : availableBottom))
    width: maximized && parent ? parent.width : Math.min(960, parent ? (parent.width<600?parent.width:parent.width-32) : 960)
    height: maximized ? usableHeight : Math.min(680,usableHeight<430?usableHeight:usableHeight-24)
    x:parent?(parent.width-width)/2:0
    y:Math.max(0,(usableHeight-height)/2)
    Material.theme: Material.Dark
    Material.accent: primary
    Material.foreground: ink
    readonly property color surface: Theme.background
    readonly property color surfaceLow: Theme.surface
    readonly property color surfaceHigh: Theme.surfaceContainerHigh
    readonly property color primary: Theme.accent
    readonly property color primaryContainer: Theme.primaryContainer
    readonly property color ink: Theme.foreground
    readonly property color secondaryInk: Theme.disabledText
    readonly property color outline: Theme.outlineVariant
    readonly property bool compact: width < 680
    readonly property bool shortScreen: height < 430
    property bool showModels: false
    property bool showDownloadedCharacters: true
    property bool showNotDownloadedCharacters: true
    property string selectedCharacter: ""
    property string selectedModelId: ""
    property bool licenseAccepted: false
    property bool maximized: false
    signal keyboardRequested()
    property alias library: library
    // Binding can be replaced with fixture rows in an offline UI test.
    property var sourceRows: library.rows
    readonly property var characters: groupCharacters(sourceRows, search.text)
    readonly property var models: modelsForCharacter(sourceRows, selectedCharacter, search.text, category.currentIndex)
    readonly property var selectedModel: findModel(selectedModelId)
    readonly property int modelCount: sourceRows.length
    function t(text) { return typeof systemTools === "undefined" ? text : I18n.t(text) }
    function characterName(row) { return row.character || row.name.split("，")[0] }
    function groupCharacters(rows, query) {
        const groups = {}, result = []
        const q = query.trim().toLowerCase()
        for (let i=0; i<rows.length; ++i) {
            const row = rows[i], name = characterName(row)
            if ((name+" "+row.name+" "+(row.search||"")+" "+row.id).toLowerCase().indexOf(q)<0) continue
            if (!groups[name]) {
                groups[name] = {name:name, english:row.appellation||"", count:0, installed:0, downloaded:false}
                result.push(groups[name])
            }
            groups[name].count++
            if (row.installed) { groups[name].installed++; groups[name].downloaded=true }
        }
        return result.filter(function(group) {
            return group.downloaded ? root.showDownloadedCharacters : root.showNotDownloadedCharacters
        }).sort(function(a,b) {return a.name.localeCompare(b.name)})
    }
    function modelsForCharacter(rows, name, query, tab) {
        const q = query.trim().toLowerCase()
        return rows.filter(function(row) {
            return characterName(row)===name && (tab===0 || row.category===(tab===1?"default":tab===2?"skin":"illustration"))
                && (row.name+" "+(row.search||"")+" "+row.id).toLowerCase().indexOf(q)>=0
        }).sort(function(a,b) {
            const order={"default":0,"skin":1,"illustration":2}
            return a.category===b.category?a.id.localeCompare(b.id):order[a.category]-order[b.category]
        })
    }
    function findModel(id) {
        for (let i=0;i<sourceRows.length;++i) if (sourceRows[i].id===id) return sourceRows[i]
        return null
    }
    function selectCharacter(name) {
        selectedCharacter = name; category.currentIndex = 0
        const candidates = modelsForCharacter(sourceRows, name, search.text, 0)
        selectedModelId = candidates.length ? candidates[0].id : ""
        if (compact) showModels = true
    }
    function applySelection() {
        if (!selectedModel || library.busy) return
        if (selectedModel.installed && !selectedModel.needsUpgrade) library.useInstalled(selectedModelId)
        else if (licenseAccepted) library.download(selectedModelId, true)
        else consentDialog.open()
    }
    onCharactersChanged: {
        const present = characters.some(function(c){return c.name===selectedCharacter})
        if (!present) { selectedCharacter=characters.length?characters[0].name:""; selectedModelId="" }
    }
    onModelsChanged: {
        if (!models.some(function(row){return row.id===selectedModelId})) selectedModelId=models.length?models[0].id:""
    }
    onAboutToShow: {
        if (!selectedCharacter && characters.length) selectedCharacter=characters[0].name
        showModels = false
    }
    PetLibrary { id: library }
    background: Rectangle { color: root.surface; radius: root.compact ? 0 : Theme.shapeExtraLarge; border.color: root.outline }
    Overlay.modal: Rectangle { color: "#99000000" }

    header: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.compact ? 12 : 24
            Layout.rightMargin: 12
            Layout.preferredHeight: root.shortScreen ? 48 : 64
            ToolButton {
                objectName: "petManagerBack"
                visible: root.compact && root.showModels
                implicitWidth: 48; implicitHeight: 48
                icon.source: "qrc:/icons/material/light/arrow_back.svg"
                Accessible.name: root.t("返回角色列表")
                onClicked: root.showModels=false
            }
            ColumnLayout {
                spacing: 2; Layout.fillWidth: true
                Label { text: root.compact&&root.showModels?root.selectedCharacter:root.t("桌宠角色管理"); color: root.ink; font.pixelSize: root.compact?20:Theme.title; Layout.fillWidth:true; elide:Text.ElideRight }
                Label {
                    visible: !root.shortScreen
                    text: root.characters.length+root.t(" 位角色")+"，"+root.modelCount+root.t(" 款模型")
                    color: root.secondaryInk; font.pixelSize: 12
                }
            }
            ToolButton {
                objectName:"petManagerMaximize"
                visible:!root.compact
                implicitWidth:48;implicitHeight:48
                icon.source:root.maximized?"qrc:/icons/material/light/close_fullscreen.svg":"qrc:/icons/material/light/fullscreen.svg"
                Accessible.name:root.t("最大化/恢复窗口")
                onClicked:root.maximized=!root.maximized
            }
            ToolButton {
                objectName:"petManagerCompactFilters"
                visible:root.shortScreen
                implicitWidth:48;implicitHeight:48
                icon.source:"qrc:/icons/material/light/tune.svg"
                Accessible.name:root.t("模型下载状态筛选")
                onClicked:compactFilters.open()
                Menu {
                    id:compactFilters;objectName:"petDownloadStatusMenu"
                    MenuItem {
                        objectName:"petDownloadedRoleFilterMenu"
                        text:root.t("已下载角色");checkable:true;checked:root.showDownloadedCharacters
                        enabled:root.showNotDownloadedCharacters
                        onTriggered:root.showDownloadedCharacters=checked
                    }
                    MenuItem {
                        objectName:"petNotDownloadedRoleFilterMenu"
                        text:root.t("未下载角色");checkable:true;checked:root.showNotDownloadedCharacters
                        enabled:root.showDownloadedCharacters
                        onTriggered:root.showNotDownloadedCharacters=checked
                    }
                }
            }
            ToolButton {
                objectName:"petManagerSize"
                implicitWidth:48;implicitHeight:48
                icon.source:"qrc:/icons/material/light/zoom_in.svg"
                Accessible.name:root.t("大小与画质")
                ToolTip.visible:hovered;ToolTip.text:root.t("角色大小")+"，"+Math.round(library.sizeMultiplier*100)+"%"
                onClicked:sizeDialog.open()
            }
            ToolButton {
                objectName: "petManagerClose"
                implicitWidth: 48; implicitHeight: 48
                icon.source: "qrc:/icons/material/light/close.svg"
                Accessible.name: root.t("关闭")
                onClicked: root.close()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.compact?12:24; Layout.rightMargin: root.compact?12:24
            Layout.bottomMargin: 12
            spacing: 8
            TextField {
                id: search; objectName: "petManagerSearch"
                Layout.fillWidth: true; Layout.preferredHeight: 48
                placeholderText: root.t("搜索角色、服装或模型 ID")
                placeholderTextColor: root.secondaryInk
                leftPadding: 48; rightPadding: 40
                color: root.ink; font.pixelSize: 14
                background: Rectangle { radius: 24; color: root.surfaceHigh; border.width: search.activeFocus?2:0; border.color: root.primary }
                Image { x:16; anchors.verticalCenter:parent.verticalCenter; width:24;height:24;source:"qrc:/icons/material/light/search.svg" }
                ToolButton {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    width:48;height:48;visible:search.text!=="";icon.source:"qrc:/icons/material/light/close.svg"
                    Accessible.name: root.t("清除搜索");onClicked: search.clear()
                }
            }
            ToolButton {
                objectName:"petManagerKeyboard"
                implicitWidth:48;implicitHeight:48;focusPolicy:Qt.NoFocus
                icon.source:"qrc:/icons/material/light/keyboard.svg"
                Accessible.name:root.t("打开屏幕键盘")
                onClicked:{search.forceActiveFocus();root.keyboardRequested()}
            }
            ToolButton {
                objectName: "petManagerRefresh"
                implicitWidth:text!==""?Math.max(120,contentItem.implicitWidth+leftPadding+rightPadding):48
                implicitHeight:48
                text:!library.hasCatalog && root.sourceRows.length<=1?root.t("加载目录"):""
                display:text!==""?AbstractButton.TextBesideIcon:AbstractButton.IconOnly
                enabled: !library.busy
                icon.source:"qrc:/icons/material/light/refresh.svg"
                Accessible.name: root.t("刷新角色目录")
                ToolTip.visible: hovered; ToolTip.text: Accessible.name
                onClicked: library.refresh()
            }
        }
        Rectangle { Layout.fillWidth:true; height:1; color:root.outline }
    }
    Dialog {
        id:sizeDialog;objectName:"petSizeDialog"
        parent:root.parent
        width:Math.min(440,parent?parent.width-24:440)
        height:Math.min(440,root.availableBottom)
        x:parent?(parent.width-width)/2:0
        y:Math.max(0,(root.availableBottom-height)/2)
        modal:false;dim:false
        Material.theme:Material.Dark;Material.accent:root.primary
        Material.foreground:root.ink
        background:Rectangle {color:root.surface;radius:Theme.shapeExtraLarge;border.color:root.outline}
        header:Label {text:root.t("大小与画质");color:root.ink;font.pixelSize:22;padding:24;bottomPadding:8}
        contentItem:ScrollView {
            id:appearanceScroll;clip:true;contentWidth:availableWidth
            ColumnLayout {
            width:appearanceScroll.availableWidth
            spacing:8
            Label {text:Math.round(library.sizeMultiplier*100)+"%";color:root.primary;font.pixelSize:24;Layout.alignment:Qt.AlignHCenter}
            Slider {
                id:sizeSlider;objectName:"petSizeSlider"
                Layout.fillWidth:true;Layout.minimumHeight:48
                from:0.5;to:2;stepSize:0.05
                Accessible.name:root.t("角色大小")
                Binding {target:sizeSlider;property:"value";value:library.sizeMultiplier;when:!sizeSlider.pressed}
                onMoved:library.sizeMultiplier=value
            }
            RowLayout {
                Label {text:"50%";color:root.secondaryInk}
                Item {Layout.fillWidth:true}
                Label {text:"200%";color:root.secondaryInk}
            }
            Label {text:root.t("渲染质量");color:root.ink;font.pixelSize:16}
            ComboBox {
                id:qualitySelector;objectName:"petQualitySelector"
                Layout.fillWidth:true;Layout.minimumHeight:48
                enabled:!library.busy
                model:[root.t("流畅，128 px / 64 MiB"),root.t("标准，256 px / 128 MiB"),root.t("高清，384 px / 256 MiB")]
                currentIndex:["economy","standard","high"].indexOf(library.quality)
                onActivated:library.quality=["economy","standard","high"][currentIndex]
            }
            Label {
                text:root.t("质量影响重新转换的纹理，不是简单放大。选择后在模型列表点击更新；完成前保留当前角色。长动作会降低采样帧率以限制内存。")
                color:root.secondaryInk;wrapMode:Text.Wrap;Layout.fillWidth:true;font.pixelSize:12
            }
            }
        }
        footer:DialogButtonBox {
            background:Rectangle {color:root.surfaceLow;radius:Theme.shapeExtraLarge}
            Button {objectName:"petSizeReset";text:root.t("恢复默认大小");implicitHeight:48;DialogButtonBox.buttonRole:DialogButtonBox.ResetRole;onClicked:library.sizeMultiplier=1}
            Button {text:root.t("完成");implicitHeight:48;DialogButtonBox.buttonRole:DialogButtonBox.AcceptRole}
            onAccepted:sizeDialog.close()
        }
    }

    contentItem: RowLayout {
        spacing: 16
        Rectangle {
            objectName: "petCharacterPane"
            visible: !root.compact || !root.showModels
            Layout.fillHeight:true
            Layout.fillWidth:root.compact
            Layout.preferredWidth: root.compact ? -1 : 248
            Layout.leftMargin: root.compact?12:16
            Layout.rightMargin: root.compact?12:0
            Layout.topMargin:root.shortScreen?4:12; Layout.bottomMargin:root.shortScreen?4:12
            color:root.surfaceLow;radius:16
            ColumnLayout {
                anchors.fill:parent;anchors.margins:8;spacing:4
                Label { visible:!root.shortScreen;text:root.t("角色");color:root.secondaryInk;font.pixelSize:12;Layout.leftMargin:16;Layout.preferredHeight:28;verticalAlignment:Text.AlignVCenter }
                RowLayout {
                    objectName:"petCharacterDownloadFilters"
                    visible:!root.shortScreen
                    Layout.fillWidth:true;Layout.preferredHeight:48;Layout.minimumHeight:48;spacing:0
                    CheckBox {
                        id:downloadedRoleFilter;objectName:"petDownloadedRoleFilter"
                        Layout.fillWidth:true;Layout.minimumWidth:0;Layout.minimumHeight:48
                        text:root.t("已下载");checked:root.showDownloadedCharacters;enabled:root.showNotDownloadedCharacters
                        font.pixelSize:12
                        onToggled:root.showDownloadedCharacters=checked
                    }
                    CheckBox {
                        id:notDownloadedRoleFilter;objectName:"petNotDownloadedRoleFilter"
                        Layout.fillWidth:true;Layout.minimumWidth:0;Layout.minimumHeight:48
                        text:root.t("未下载");checked:root.showNotDownloadedCharacters;enabled:root.showDownloadedCharacters
                        font.pixelSize:12
                        onToggled:root.showNotDownloadedCharacters=checked
                    }
                }
                ListView {
                    id: characterList; objectName:"petCharacterList"
                    Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:4
                    model:root.characters
                    ScrollBar.vertical:ScrollBar { }
                    delegate:ItemDelegate {
                        required property var modelData
                        width:ListView.view.width;height:72
                        checked:root.selectedCharacter===modelData.name
                        Accessible.name:modelData.name+" "+modelData.count+root.t(" 款模型")
                        onClicked:root.selectCharacter(modelData.name)
                        background:Rectangle {radius:12;color:parent.checked?root.primaryContainer:parent.down||parent.hovered?root.surfaceHigh:"transparent";border.width:parent.activeFocus?2:0;border.color:root.primary}
                        contentItem:RowLayout {
                            spacing:12
                            Rectangle {
                                Layout.preferredWidth:40;Layout.preferredHeight:40;radius:20
                                color:root.surfaceHigh
                                Label {anchors.centerIn:parent;text:modelData.name.charAt(0);color:root.primary;font.pixelSize:18}
                            }
                            ColumnLayout {
                                Layout.fillWidth:true;spacing:4
                                Label {text:modelData.name;color:root.ink;font.pixelSize:16;Layout.fillWidth:true;elide:Text.ElideRight}
                                Label {text:modelData.count+root.t(" 款模型")+(modelData.installed?"，"+modelData.installed+root.t(" 已下载"):"");color:root.secondaryInk;font.pixelSize:12;Layout.fillWidth:true;elide:Text.ElideRight}
                            }
                            Image {Layout.preferredWidth:24;Layout.preferredHeight:24;source:"qrc:/icons/material/light/chevron_right.svg";opacity:0.7}
                        }
                    }
                    Label {anchors.centerIn:parent;visible:characterList.count===0;text:root.t("没有匹配的角色");color:root.secondaryInk}
                }
            }
        }
        ColumnLayout {
            objectName:"petModelPane"
            visible:!root.compact || root.showModels
            Layout.fillWidth:true;Layout.fillHeight:true
            Layout.leftMargin:root.compact?12:0;Layout.rightMargin:root.compact?12:24
            Layout.topMargin:root.shortScreen?4:12;Layout.bottomMargin:root.shortScreen?4:12
            spacing:8
            RowLayout {
                visible:!root.shortScreen
                Layout.fillWidth:true;Layout.preferredHeight:root.shortScreen?32:40
                Label {text:root.selectedCharacter||root.t("选择角色");color:root.ink;font.pixelSize:20;Layout.fillWidth:true;elide:Text.ElideRight}
                Label {text:root.models.length+root.t(" 款模型");color:root.secondaryInk;font.pixelSize:12}
            }
            TabBar {
                id:category;objectName:"petModelCategory"
                background:Rectangle {color:root.surfaceLow;radius:Theme.shapeSmall}
                visible:!root.shortScreen
                Layout.fillWidth:true;Layout.preferredHeight:48
                TabButton {text:root.t("全部");implicitHeight:48}
                TabButton {text:root.t("默认模型");implicitHeight:48}
                TabButton {text:root.t("服装模型");implicitHeight:48}
                TabButton {text:root.t("动态立绘");implicitHeight:48}
            }
            Rectangle {
                objectName:"petSelectedModelPreview"
                visible:!root.shortScreen && root.selectedModel && root.selectedModel.installed
                Layout.fillWidth:true;Layout.preferredHeight:124
                radius:16;color:root.surfaceLow;border.color:root.outline
                RowLayout {
                    anchors.fill:parent;anchors.margins:12;spacing:16
                    Rectangle {
                        Layout.preferredWidth:100;Layout.preferredHeight:100;radius:12;color:root.surfaceHigh
                        Image { anchors.fill:parent;anchors.margins:4;source:root.selectedModel?library.previewAtlas(root.selectedModel.id):"";fillMode:Image.PreserveAspectFit;smooth:true }
                    }
                    ColumnLayout {
                        Layout.fillWidth:true;spacing:6
                        Label {text:root.selectedModel?(root.selectedModel.modelName||root.selectedModel.name):"";color:root.ink;font.pixelSize:16;Layout.fillWidth:true;elide:Text.ElideRight}
                        Label {text:root.selectedModel?root.selectedModel.id:"";color:root.secondaryInk;font.pixelSize:12;Layout.fillWidth:true;elide:Text.ElideMiddle}
                        Label {text:root.t("已下载模型预览，待机动作图集");color:root.primary;font.pixelSize:12;wrapMode:Text.Wrap;Layout.fillWidth:true}
                    }
                }
            }
            ListView {
                id:modelList;objectName:"petModelList"
                Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:4
                model:root.models
                currentIndex:root.models.findIndex(function(row){return row.id===root.selectedModelId})
                onCurrentIndexChanged: if(currentIndex>=0) Qt.callLater(function(){modelList.positionViewAtIndex(modelList.currentIndex,ListView.Contain)})
                ScrollBar.vertical:ScrollBar { }
                delegate:ItemDelegate {
                    required property var modelData
                    width:ListView.view.width;height:80
                    checked:root.selectedModelId===modelData.id
                    Accessible.name:(modelData.modelName||modelData.name)+" "+(modelData.current?root.t("使用中"):modelData.installed?root.t("已下载"):root.t("未下载"))
                    onClicked:root.selectedModelId=modelData.id
                    background:Rectangle {radius:12;color:parent.checked?root.primaryContainer:parent.down||parent.hovered?root.surfaceHigh:"transparent";border.width:parent.activeFocus?2:0;border.color:root.primary}
                    contentItem:RowLayout {
                        spacing:8
                        RadioButton {
                            checked:root.selectedModelId===modelData.id
                            implicitWidth:48;implicitHeight:48
                            Accessible.name:root.t("选择模型")+" "+(modelData.modelName||modelData.name)
                            onClicked:root.selectedModelId=modelData.id
                        }
                        ColumnLayout {
                            Layout.fillWidth:true;spacing:4
                            Label {text:modelData.modelName||modelData.name;Layout.fillWidth:true;elide:Text.ElideRight;font.pixelSize:16;color:root.ink}
                            Label {text:(modelData.category==="default"?root.t("默认模型"):modelData.category==="illustration"?root.t("动态立绘"):root.t("服装模型"))+"，"+modelData.id;Layout.fillWidth:true;elide:Text.ElideMiddle;font.pixelSize:12;color:root.secondaryInk}
                        }
                        Label {
                            text:modelData.needsUpgrade?root.t("可升级动作"):modelData.current?root.t("使用中"):modelData.installed?root.t("已下载"):root.t("未下载")
                            color:modelData.current?root.primary:root.secondaryInk;font.pixelSize:12
                            Layout.rightMargin:8
                        }
                    }
                }
                Label {anchors.centerIn:parent;visible:modelList.count===0;text:root.t("此分类暂无模型");color:root.secondaryInk}
            }
        }
    }
    footer:ColumnLayout {
        spacing:0
        Rectangle {Layout.fillWidth:true;height:1;color:root.outline}
        ProgressBar {Layout.fillWidth:true;Layout.preferredHeight:4;visible:library.busy;indeterminate:true}
        RowLayout {
            Layout.fillWidth:true;Layout.leftMargin:root.compact?12:24;Layout.rightMargin:root.compact?12:24
            Layout.topMargin:8;Layout.bottomMargin:8;spacing:8
            Label {
                objectName:"petManagerStatus"
                Layout.fillWidth:true;Layout.maximumHeight:root.shortScreen?36:52
                text:library.status ? root.t(library.status) : (root.selectedModel ? root.selectedCharacter+"，"+(root.selectedModel.modelName||root.selectedModel.name) : root.t("选择角色后查看模型"))
                wrapMode:Text.Wrap;maximumLineCount:2;elide:Text.ElideRight
                color:root.secondaryInk;font.pixelSize:12
            }
            Button {
                id:applyAction
                objectName:"petManagerApply"
                implicitHeight:48
                text:root.selectedModel && root.selectedModel.needsUpgrade?root.t("更新质量/动作"):root.selectedModel && root.selectedModel.current?root.t("使用中"):root.selectedModel && root.selectedModel.installed?root.t("使用此模型"):root.t("下载并使用")
                enabled:!library.busy && root.selectedModel!==null && (!root.selectedModel.current || !!root.selectedModel.needsUpgrade) && (!root.compact || root.showModels)
                highlighted:true
                Material.foreground: Theme.accentInk
                contentItem:Label {
                    text:applyAction.text;color:applyAction.enabled?Theme.accentInk:root.secondaryInk
                    opacity:applyAction.enabled?1:0.38
                    font.pixelSize:14;font.weight:Font.Medium
                    horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter
                }
                onClicked:root.applySelection()
            }
        }
    }
    Dialog {
        id:consentDialog;objectName:"petDownloadConsent"
        parent:root.parent;anchors.centerIn:parent;modal:true
        Material.theme:Material.Dark
        Material.accent:root.primary
        Material.foreground:root.ink
        font.family:root.font.family
        Overlay.modal:Rectangle {color:"#99000000"}
        background:Rectangle {color:root.surface;radius:28;border.color:root.outline}
        width:Math.min(520,parent?parent.width-32:520)
        height:Math.min(490,parent?parent.height-24:490)
        title:root.t("下载与本机转换")
        header:Label {
            text:consentDialog.title;color:root.ink;font.pixelSize:22;wrapMode:Text.Wrap
            leftPadding:24;rightPadding:24;topPadding:24;bottomPadding:16
        }
        contentItem:ScrollView {
            id:consentScroll
            clip:true
            contentWidth:availableWidth
            ColumnLayout {
            width:consentScroll.availableWidth;spacing:12
            Label {Layout.fillWidth:true;wrapMode:Text.Wrap;text:root.t("仅下载所选模型，在当前设备后台转换。需要 JDK17、Python/Pillow 与图形环境；缺少依赖时会提示，不自动安装系统软件。")}
            Label {Layout.fillWidth:true;wrapMode:Text.Wrap;text:root.t("角色素材仅限非商业使用。请确认适用的 Spine 工具许可；勾选确认不能替代实际授权。")}
            Label {
                Layout.fillWidth:true;textFormat:Text.RichText;wrapMode:Text.Wrap
                text:'<a href="https://github.com/isHarryh/Ark-Models#版权声明">'+root.t("素材版权说明")+'</a>，<a href="https://esotericsoftware.com/spine-editor-license">'+root.t("Spine 工具许可")+'</a>'
                linkColor:root.primary
                onLinkActivated:function(link){Qt.openUrlExternally(link)}
            }
            CheckBox {
                id:consent;implicitHeight:Math.max(48,consentLabel.implicitHeight+16);Layout.fillWidth:true
                text:root.t("我已了解并确认适用授权")
                contentItem:Label {id:consentLabel;text:consent.text;leftPadding:consent.indicator.width+consent.spacing;wrapMode:Text.Wrap;verticalAlignment:Text.AlignVCenter}
            }
            }
        }
        footer:DialogButtonBox {
            Button {text:root.t("取消");implicitHeight:48;DialogButtonBox.buttonRole:DialogButtonBox.RejectRole}
            Button {
                id:confirmDownload
                text:root.t("开始下载");implicitHeight:48;enabled:consent.checked;highlighted:true;Material.foreground:Theme.accentInk
                DialogButtonBox.buttonRole:DialogButtonBox.AcceptRole
                contentItem:Label {text:confirmDownload.text;color:confirmDownload.enabled?Theme.accentInk:root.secondaryInk;opacity:confirmDownload.enabled?1:0.38;font.pixelSize:14;font.weight:Font.Medium;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
            }
            onAccepted:{root.licenseAccepted=true;consentDialog.close();library.download(root.selectedModelId,true)}
            onRejected:consentDialog.close()
        }
    }
}
