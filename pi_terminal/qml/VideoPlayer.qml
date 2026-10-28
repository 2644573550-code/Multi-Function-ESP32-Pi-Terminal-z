import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Terminal.Native 1.0

Item {
    id: root
    objectName: "videoPlayerPage"
    property bool active: false
    property real uiScale: 1
    readonly property bool compact: width < 1200 || height < 700
    property var hostWindow: null
    property string folder: ""
    property var videoRows: []
    property string selectedFile: ""
    property string selectedRelative: ""
    property bool playing: false
    readonly property bool mediaPlaying: playing
    readonly property bool hasMedia: mediaPlayer.source.toString() !== ""
    readonly property real mediaPosition: mediaPlayer.position
    readonly property real mediaDuration: mediaPlayer.duration
    function toggleMedia() { if (hasMedia) playing ? mediaPlayer.pause() : mediaPlayer.play() }
    function pauseMedia() { mediaPlayer.pause() }
    function navigateMedia(delta) {
        const rows = videoRows.filter(function(e) { return !e.directory })
        const index = rows.findIndex(function(e) { return e.relative === root.selectedRelative })
        const next = index + delta
        if (index >= 0 && next >= 0 && next < rows.length) playFile(rows[next].relative, rows[next].name)
    }
    property bool muted: false
    property bool fullscreen: false
    property real volume: 0.8
    property int outputHeight: 480
    readonly property var outputFormats: [
        {height: 480, label: "480p", detail: I18n.t("省资源")},
        {height: 720, label: "720p", detail: I18n.t("平衡")},
        {height: 1080, label: "1080p", detail: I18n.t("高规格")}
    ]
    readonly property real outputFormatMenuWidth:
        Math.max(150 * root.uiScale,
                 ( ("1080p，" + I18n.t("高规格")).length * Theme.body * 0.72) + Theme.gap * 2)
    property real announcementGain: systemTools.mediaGain
    Behavior on announcementGain { NumberAnimation { duration: 120 } }
    property bool fullscreenControlsVisible: true
    property bool previousWindowMaximized: false
    property int pageIndex: 0
    property int transitionTarget: 0
    property var transitionAction: null
    property real transitionProgress: 0
    property string playbackError: ""
    readonly property string videoExtensions: ".mp4,.webm,.ogv,.m4v,.mov,.mkv,.avi,.ts,.mpeg,.mpg"

    function refresh() {
        videoRows = systemTools.files(folder).filter(function(entry) {
            return entry.directory || root.videoExtensions.indexOf("." + entry.name.split(".").pop().toLowerCase()) >= 0
        }).sort(function(a, b) {
            if (a.directory !== b.directory) return a.directory ? -1 : 1
            return a.name.localeCompare(b.name)
        })
    }
    function playFile(relative, name) {
        folder = relative.substring(0, Math.max(0, relative.lastIndexOf("/")))
        refresh()
        selectedFile = name
        selectedRelative = relative
        playbackError = ""
        mediaPlayer.source = systemTools.fileUrl(relative)
        switchPage(1, function() { mediaPlayer.play() })
    }
    function showLibrary() {
        if (fullscreen) setFullscreen(false)
        switchPage(0, function() {
            mediaPlayer.stop()
            selectedFile = ""
            selectedRelative = ""
        })
    }
    function switchPage(target, action) {
        if (pageTransition.running) pageTransition.stop()
        transitionTarget = target
        transitionAction = action || null
        transitionPanel.x = width
        transitionPanel.visible = true
        pageTransition.start()
    }
    function parentFolder() {
        if (!folder) return
        folder = folder.substring(0, Math.max(0, folder.lastIndexOf("/")))
        refresh()
    }
    function setFullscreen(value) {
        if (fullscreen === value) return
        fullscreen = value
        if (hostWindow) {
            if (value) {
                previousWindowMaximized = hostWindow.visibility === Window.Maximized
                fullscreenControlsVisible = true
                hostWindow.showFullScreen()
            } else {
                fullscreenControlsVisible = true
                if (previousWindowMaximized) hostWindow.showMaximized()
                else hostWindow.showNormal()
            }
        }
    }
    function revealFullscreenControls() {
        if (!fullscreen) return
        fullscreenControlsVisible = true
        fullscreenControlsTimer.restart()
    }
    function formatTime(ms) {
        const seconds = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(seconds / 60).toString().padStart(2, "0") + ":" + (seconds % 60).toString().padStart(2, "0")
    }
    function chooseOutputHeight(height) {
        if (height === 1080) {
            Qt.callLater(function() { highOutputWarning.open() })
            return
        }
        outputHeight = height
    }
    function outputFormatIndex(height) {
        for (let i = 0; i < outputFormats.length; ++i)
            if (outputFormats[i].height === height) return i
        return 0
    }
    onOutputHeightChanged: {
        if (outputFormatCombo) outputFormatCombo.currentIndex = outputFormatIndex(outputHeight)
        if (outputFormatComboFullscreen) outputFormatComboFullscreen.currentIndex = outputFormatIndex(outputHeight)
    }

    Component.onCompleted: refresh()
    onActiveChanged: {
        if (active) refresh()
    }
    Shortcut { sequence: "Escape"; enabled: root.fullscreen; onActivated: root.setFullscreen(false) }
    Shortcut { sequence: "F"; enabled: root.selectedFile !== ""; onActivated: root.setFullscreen(!root.fullscreen) }
    Timer {
        id: fullscreenControlsTimer
        interval: 3500; repeat: false
        onTriggered: if (root.fullscreen) root.fullscreenControlsVisible = false
    }
    onFullscreenChanged: {
        fullscreenControlsVisible = true
        if (fullscreen) fullscreenControlsTimer.restart()
        else fullscreenControlsTimer.stop()
    }
    Popup {
        id: highOutputWarning
        objectName: "videoHighOutputWarning"
        parent: Overlay.overlay
        width: Math.min(440 * root.uiScale, parent.width - Theme.gap * 2)
        height: Math.min(300 * root.uiScale, parent.height - Theme.gap * 2)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.surfaceContainerHigh; radius: Theme.shapeLarge; border.color: Theme.outlineVariant; border.width: 1 }
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.gap
            spacing: Theme.gap / 2
            CarText { text: I18n.t("高规格输出提醒"); heading: true; Layout.fillWidth: true }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.outlineVariant }
            CarText {
                text: I18n.t("1080p 会增加 Pi 3B 的视频处理、内存带宽和帧拷贝负载，可能掉帧或发热。建议优先使用 480p 或 720p。仍要切换到 1080p 吗？")
                fontPixelSize: 16 * root.uiScale
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "videoHighOutputCancel"
                    text: I18n.t("取消")
                    implicitHeight: Math.max(48, Theme.hit)
                    onClicked: {
                        outputFormatCombo.currentIndex = root.outputFormatIndex(root.outputHeight)
                        outputFormatComboFullscreen.currentIndex = root.outputFormatIndex(root.outputHeight)
                        highOutputWarning.close()
                    }
                }
                Button {
                    objectName: "videoHighOutputConfirm"
                    text: I18n.t("切换到1080p")
                    highlighted: true
                    implicitHeight: Math.max(48, Theme.hit)
                    onClicked: { root.outputHeight = 1080; highOutputWarning.close() }
                }
            }
        }
    }
    SequentialAnimation {
        id: pageTransition
        ParallelAnimation {
            NumberAnimation { target: transitionPanel; property: "x"; to: 0; duration: 240; easing.type: Easing.OutCubic }
            NumberAnimation { target: root; property: "transitionProgress"; to: 1; duration: 330; easing.type: Easing.OutCubic }
        }
        ScriptAction {
            script: {
                root.pageIndex = root.transitionTarget
                const action = root.transitionAction
                root.transitionAction = null
                if (action) action()
            }
        }
        ParallelAnimation {
            NumberAnimation { target: transitionPanel; property: "x"; to: -root.width; duration: 260; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "transitionProgress"; to: 0; duration: 260; easing.type: Easing.InCubic }
        }
        ScriptAction { script: { transitionPanel.visible = false; transitionPanel.x = root.width } }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.pageIndex

        // Library: choose first, then enter a dedicated playback surface.
        ColumnLayout {
            spacing: root.compact ? 4 * root.uiScale : Theme.gap / 2
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: root.compact ? Theme.gap / 2 : Theme.gap; Layout.rightMargin: root.compact ? Theme.gap / 2 : Theme.gap
                Layout.topMargin: root.compact ? 2 : Theme.gap / 2
                IconButton { iconName: "arrow_back"; label: I18n.t("上一级"); implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit; enabled: root.folder !== ""; onClicked: root.parentFolder() }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2 * root.uiScale
                    CarText { text: root.compact && root.folder ? root.folder.split("/").pop() : I18n.t("视频库"); heading: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideMiddle }
                    CarText { visible: !root.compact; text: root.folder || I18n.t("选择要播放的视频"); subdued: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideMiddle }
                }
                IconButton { iconName: "refresh"; label: I18n.t("刷新视频列表"); implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit; onClicked: root.refresh() }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.leftMargin: root.compact ? Theme.gap / 2 : Theme.gap; Layout.rightMargin: root.compact ? Theme.gap / 2 : Theme.gap; Layout.bottomMargin: root.compact ? Theme.gap / 2 : Theme.gap
                radius: Theme.shapeExtraLarge; color: Theme.surfaceContainer; clip: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: root.compact ? Theme.gap / 2 : Theme.gap; spacing: root.compact ? 4 * root.uiScale : Theme.gap / 2
                    ListView {
                        id: videoList
                        objectName: "videoFileList"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        model: root.videoRows; boundsBehavior: Flickable.StopAtBounds
                        spacing: root.compact ? 4 * root.uiScale : Theme.gap / 2
                        delegate: ItemDelegate {
                            required property var modelData
                            width: ListView.view.width
                            height: Math.max(Theme.hit, 64 * root.uiScale)
                            onClicked: {
                                if (modelData.directory) {
                                    root.folder = (root.folder ? root.folder + "/" : "") + modelData.name
                                    root.refresh()
                                } else root.playFile((root.folder ? root.folder + "/" : "") + modelData.name, modelData.name)
                            }
                            background: Rectangle {
                                radius: Theme.shapeLarge
                                color: parent.down ? Theme.pressed : parent.hovered ? Theme.surfaceContainerHigh : Theme.surface
                                border.color: parent.activeFocus ? Theme.accent : Theme.outlineVariant
                                border.width: parent.activeFocus ? 2 : 1
                            }
                            contentItem: RowLayout {
                                spacing: Theme.gap / 2
                                Rectangle {
                                    Layout.preferredWidth: 44 * root.uiScale; Layout.preferredHeight: 44 * root.uiScale
                                    radius: Theme.shapeMedium; color: Theme.primaryContainer
                                    MaterialIcon { anchors.centerIn: parent; width: 24 * root.uiScale; height: width; name: modelData.directory ? "folder" : "play_arrow" }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 3 * root.uiScale
                                    Text { text: modelData.name; color: Theme.foreground; font.pointSize: Theme.body; font.weight: Font.DemiBold; elide: Text.ElideMiddle; maximumLineCount: 1; Layout.fillWidth: true }
                                }
                                MaterialIcon { name: "chevron_right"; Layout.preferredWidth: 24 * root.uiScale; Layout.preferredHeight: 24 * root.uiScale }
                            }
                        }
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }
                    CarText { visible: root.videoRows.length === 0; text: I18n.t("此目录没有可播放视频"); subdued: true; Layout.fillWidth: true }
                }
            }
        }

        // VLC-inspired dedicated playback layout.
        ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: "#080a0e"
                VlcVideoItem {
                    id: mediaPlayer
                    objectName: "vlcVideoOutput"
                    anchors.fill: parent
                    muted: root.muted
                    volume: root.volume * root.announcementGain
                    outputHeight: root.outputHeight
                    onStatusChanged: root.playing = playing
                    onErrorChanged: root.playbackError = errorString
                }
                MouseArea {
                    anchors.fill: parent
                    z: 1
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: root.fullscreen && !root.fullscreenControlsVisible ? Qt.BlankCursor : Qt.ArrowCursor
                    onPositionChanged: root.revealFullscreenControls()
                    onClicked: root.revealFullscreenControls()
                }
                ColumnLayout {
                    anchors.centerIn: parent; spacing: Theme.gap
                    visible: mediaPlayer.loading
                    BusyIndicator { running: visible; Layout.alignment: Qt.AlignHCenter }
                    CarText { text: I18n.t("正在载入视频"); subdued: true; Layout.alignment: Qt.AlignHCenter }
                }
                CarText {
                    anchors.centerIn: parent; visible: root.playbackError !== ""
                    text: root.playbackError; color: Theme.warning; wrapMode: Text.Wrap; width: parent.width * 0.8; horizontalAlignment: Text.AlignHCenter
                }
                Rectangle {
                    id: fullscreenControls
                    visible: root.fullscreen && root.fullscreenControlsVisible
                    z: 2
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: fullscreenColumn.implicitHeight + (root.compact ? Theme.gap : Theme.gap * 2)
                    color: "#d90a111c"
                    ColumnLayout {
                        id: fullscreenColumn
                        x: root.compact ? Theme.gap / 2 : Theme.gap * 1.5
                        y: root.compact ? Theme.gap / 2 : Theme.gap
                        width: Math.max(0, parent.width - 2 * x)
                        height: implicitHeight
                        spacing: root.compact ? 2 : Theme.gap / 2
                        CarText { text: root.selectedFile; heading: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideMiddle }
                        ComboBox {
                            id: outputFormatComboFullscreen
                            objectName: "videoOutputFormatSelectorFullscreen"
                            Layout.preferredWidth: Math.min(root.outputFormatMenuWidth, root.width * 0.42)
                            Layout.minimumWidth: Math.min(Theme.hit, root.width * 0.28)
                            implicitHeight: Math.max(48, Theme.hit)
                            font.pixelSize: Math.max(16, 16 * root.uiScale)
                            model: root.outputFormats
                            textRole: "label"
                            currentIndex: root.outputFormatIndex(root.outputHeight)
                            Accessible.name: I18n.t("视频输出规格")
                            displayText: root.outputHeight + "p"
                            delegate: ItemDelegate {
                                required property var modelData
                                width: ListView.view.width
                                height: Math.max(Theme.hit, 56 * root.uiScale)
                                leftPadding: Theme.gap
                                rightPadding: Theme.gap
                                contentItem: RowLayout {
                                    spacing: Theme.gap / 2
                                    ColumnLayout {
                                        spacing: 1
                                        CarText { text: modelData.label; fontPixelSize: 16 * root.uiScale; heading: true; Layout.fillWidth: true }
                                        CarText { text: modelData.detail; fontPixelSize: 12 * root.uiScale; subdued: true; maximumLineCount: 1; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                    MaterialIcon { visible: modelData.height === root.outputHeight; name: "check"; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                                }
                            }
                            onActivated: function(index) {
                                root.chooseOutputHeight(root.outputFormats[index].height)
                                Qt.callLater(function() { outputFormatComboFullscreen.currentIndex = root.outputFormatIndex(root.outputHeight) })
                            }
                            popup.width: Math.min(Math.max(outputFormatMenuWidth + Theme.gap * 2, 220), Math.max(220, root.width - Theme.gap * 2))
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: Theme.gap / 2
                            CarText { text: root.formatTime(mediaPlayer.position); subdued: true }
                            Slider { Layout.fillWidth: true; from: 0; to: Math.max(1, mediaPlayer.duration); value: mediaPlayer.position; enabled: mediaPlayer.seekable; onMoved: mediaPlayer.position = value }
                            CarText { text: root.formatTime(mediaPlayer.duration); subdued: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: root.compact ? 2 : Theme.gap / 2
                            IconButton { iconName: "stop"; label: I18n.t("停止"); implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit; onClicked: mediaPlayer.stop() }
                            IconButton { iconName: root.playing ? "pause" : "play_arrow"; label: root.playing ? I18n.t("暂停") : I18n.t("继续播放"); implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit; filled: true; onClicked: root.playing ? mediaPlayer.pause() : mediaPlayer.play() }
                            Item { Layout.fillWidth: true }
                            IconButton { iconName: root.muted ? "volume_off" : "volume_up"; label: root.muted ? I18n.t("打开声音") : I18n.t("静音"); onClicked: root.muted = !root.muted }
                            Slider { Layout.preferredWidth: Math.min((root.compact ? 100 : 180) * root.uiScale, root.width * 0.24); from: 0; to: 1; value: root.volume; onMoved: root.volume = value }
                            IconButton { iconName: "close_fullscreen"; label: I18n.t("退出全屏"); implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit; onClicked: root.setFullscreen(false) }
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: controlsColumn.implicitHeight + (root.compact ? Theme.gap : Theme.gap * 1.5)
                color: Theme.surfaceContainer
                visible: !root.fullscreen
                ColumnLayout {
                    id: controlsColumn
                    anchors.fill: parent; anchors.leftMargin: root.compact ? Theme.gap / 2 : Theme.gap * 1.5; anchors.rightMargin: root.compact ? Theme.gap / 2 : Theme.gap * 1.5
                    anchors.topMargin: root.compact ? Theme.gap / 2 : Theme.gap; anchors.bottomMargin: root.compact ? Theme.gap / 2 : Theme.gap; spacing: root.compact ? 2 : Theme.gap / 2
                    RowLayout {
                        Layout.fillWidth: true; spacing: root.compact ? 2 : Theme.gap / 2
                        IconButton { iconName: "arrow_back"; label: I18n.t("返回视频库"); implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit; onClicked: root.showLibrary() }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2 * root.uiScale
                            CarText { text: root.selectedFile; heading: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideMiddle }
                            CarText { text: root.playing ? I18n.t("正在播放") : I18n.t("已暂停"); subdued: true; Layout.fillWidth: true }
                        }
                        ComboBox {
                            id: outputFormatCombo
                            objectName: "videoOutputFormatSelector"
                            Layout.preferredWidth: Math.min(root.outputFormatMenuWidth, root.width * 0.42)
                            Layout.minimumWidth: Math.min(Theme.hit, root.width * 0.28)
                            implicitHeight: Math.max(48, Theme.hit)
                            font.pixelSize: Math.max(16, 16 * root.uiScale)
                            model: root.outputFormats
                            textRole: "label"
                            currentIndex: root.outputFormatIndex(root.outputHeight)
                            Accessible.name: I18n.t("视频输出规格")
                            displayText: root.outputHeight + "p"
                            delegate: ItemDelegate {
                                required property var modelData
                                width: ListView.view.width
                                height: Math.max(Theme.hit, 56 * root.uiScale)
                                leftPadding: Theme.gap
                                rightPadding: Theme.gap
                                contentItem: RowLayout {
                                    spacing: Theme.gap / 2
                                    ColumnLayout {
                                        spacing: 1
                                        CarText { text: modelData.label; fontPixelSize: 16 * root.uiScale; heading: true; Layout.fillWidth: true }
                                        CarText { text: modelData.detail; fontPixelSize: 12 * root.uiScale; subdued: true; maximumLineCount: 1; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                    MaterialIcon { visible: modelData.height === root.outputHeight; name: "check"; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                                }
                            }
                            onActivated: function(index) {
                                root.chooseOutputHeight(root.outputFormats[index].height)
                                Qt.callLater(function() { outputFormatCombo.currentIndex = root.outputFormatIndex(root.outputHeight) })
                            }
                            popup.width: Math.min(Math.max(outputFormatMenuWidth + Theme.gap * 2, 220), Math.max(220, root.width - Theme.gap * 2))
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: Theme.gap / 2
                        CarText { text: root.formatTime(mediaPlayer.position); subdued: true }
                        Slider {
                            id: seekSlider
                            Layout.fillWidth: true; from: 0; to: Math.max(1, mediaPlayer.duration)
                            value: mediaPlayer.position; enabled: mediaPlayer.seekable
                            onMoved: mediaPlayer.position = value
                        }
                        CarText { text: root.formatTime(mediaPlayer.duration); subdued: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: root.compact ? 2 : Theme.gap / 2
                        IconButton { iconName: "stop"; label: I18n.t("停止"); implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit; onClicked: mediaPlayer.stop() }
                        IconButton {
                            iconName: root.playing ? "pause" : "play_arrow"
                            label: root.playing ? I18n.t("暂停") : I18n.t("继续播放")
                            implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit
                            filled: true; onClicked: root.playing ? mediaPlayer.pause() : mediaPlayer.play()
                        }
                        Item { Layout.fillWidth: true }
                        IconButton { iconName: root.muted ? "volume_off" : "volume_up"; label: root.muted ? I18n.t("打开声音") : I18n.t("静音"); onClicked: root.muted = !root.muted }
                        Slider { Layout.preferredWidth: Math.min((root.compact ? 100 : 180) * root.uiScale, root.width * 0.24); from: 0; to: 1; value: root.volume; onMoved: root.volume = value }
                        CarText { text: Math.round(root.volume * 100) + "%"; subdued: true; Layout.preferredWidth: 48 * root.uiScale; horizontalAlignment: Text.AlignRight }
                        IconButton {
                            iconName: root.fullscreen ? "close_fullscreen" : "fullscreen"
                            label: root.fullscreen ? I18n.t("退出全屏") : I18n.t("全屏播放")
                            implicitWidth: root.compact ? 48 * root.uiScale : Theme.hit; implicitHeight: root.compact ? 48 * root.uiScale : Theme.hit
                            onClicked: root.setFullscreen(!root.fullscreen)
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: transitionPanel
        z: 10; visible: false
        x: root.width; y: 0; width: root.width; height: root.height
        gradient: Gradient {
            GradientStop { position: 0; color: "#101c2b" }
            GradientStop { position: 0.55; color: "#0a111c" }
            GradientStop { position: 1; color: "#131d2a" }
        }
        Rectangle {
            width: Math.min(parent.width * 0.78, 700 * root.uiScale)
            height: Math.max(148 * root.uiScale, transitionCardContent.implicitHeight + Theme.gap * 3)
            anchors.centerIn: parent
            radius: Theme.shapeExtraLarge
            color: Theme.surfaceContainer
            border.color: Theme.outlineVariant; border.width: 1
            RowLayout {
                id: transitionCardContent
                x: Theme.gap * 1.5; y: Theme.gap * 1.5
                width: Math.max(0, parent.width - Theme.gap * 3)
                spacing: Theme.gap * 1.5
                Rectangle {
                    Layout.preferredWidth: 76 * root.uiScale; Layout.preferredHeight: 76 * root.uiScale
                    radius: Theme.shapeLarge; color: Theme.primaryContainer
                    MaterialIcon { anchors.centerIn: parent; width: 40 * root.uiScale; height: width; name: root.transitionTarget === 1 ? "play_arrow" : "folder" }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Theme.gap / 3
                    CarText { text: root.transitionTarget === 1 ? I18n.t("正在打开视频") : I18n.t("正在返回视频库"); heading: true; Layout.fillWidth: true }
                    CarText { text: root.transitionTarget === 1 ? root.selectedFile : I18n.t("选择要播放的视频"); subdued: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideMiddle }
                }
                BusyIndicator { running: transitionPanel.visible; Layout.preferredWidth: 34 * root.uiScale; Layout.preferredHeight: 34 * root.uiScale }
            }
        }
        Rectangle {
            anchors.left: parent.left; anchors.bottom: parent.bottom
            width: parent.width * root.transitionProgress; height: 4 * root.uiScale
            color: Theme.accent
        }
    }
}
