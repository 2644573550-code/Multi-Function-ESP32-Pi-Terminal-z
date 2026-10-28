import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Terminal.Native 1.0

// 免登录网络电视（仅中国）。片源见 assets/tv/SOURCE.md。
// Pi 3B 约束：单一解码器实例、切台先 stop 再换源、退出即释放、低分辨率优先。
// 视觉与控件规格对齐 VideoPlayer.qml：48dp 按钮、#d90a111c 控件条、自动隐藏。
Item {
    id: root
    objectName: "tvPlayerPage"

    property bool active: false
    property real uiScale: 1
    property var hostWindow: null

    readonly property bool compact: width < 1200 || height < 700

    readonly property bool playing: player.playing
    readonly property bool mediaPlaying: playing

    property bool muted: false
    property real volume: 0.8

    property bool loading: false
    property string statusText: ""
    property string errorText: ""

    property var categories: []
    property int categoryIndex: 0
    property int channelIndex: -1
    property int sourceIndex: 0
    property string currentName: ""

    property int pendingSourceIndex: -1
    property string pendingSourceLabel: ""
    property bool fullscreen: false
    property bool previousWindowMaximized: false
    property bool controlsVisible: true
    property point lastMouse: Qt.point(-1, -1)

    readonly property var currentChannels:
        (categories.length > categoryIndex && categoryIndex >= 0)
            ? categories[categoryIndex].channels : []
    readonly property var currentChannel:
        (channelIndex >= 0 && channelIndex < currentChannels.length) ? currentChannels[channelIndex] : null
    // 与 startSource() 使用同一份排序结果，保证 sourceIndex 一致
    readonly property var currentSources: lowResSources(currentChannel)
    // 控制栏只显示“不同清晰度”，同一清晰度的多个镜像不重复占位
    readonly property var sourceOptions: buildSourceOptions(currentChannel)
    readonly property int currentSourceHeight:
        (sourceIndex >= 0 && sourceIndex < currentSources.length && currentSources[sourceIndex].height)
            ? currentSources[sourceIndex].height : 0
    readonly property string currentSourceLabel:
        currentSourceHeight > 0 ? currentSourceHeight + "P" : I18n.t("线路")

    // 左列（搜索栏 + 侧栏）宽度：与节目面板一致
    readonly property real paneWidth: Math.max(236 * uiScale, width * 0.27)

    // 侧栏像文件管理器一样逐级进入：一级分类 → 二级节目
    property bool browsing: false
    readonly property bool drilldown: browsing || searchText.trim() !== ""
    readonly property string drilldownTitle:
        searchText.trim() !== "" ? I18n.t("搜索") + "，" + searchText.trim()
                                 : (categoryIndex >= 0 && categoryIndex < categories.length
                                    ? categories[categoryIndex].title : "")

    // 搜索 + 屏幕键盘（键盘复用项目自带的 FloatingKeyboard）
    property string searchText: ""
    property var virtualKeyboard: null
    signal keyboardRequested()
    // 二级菜单模型：无搜索时是当前分类，有搜索时跨分类匹配
    readonly property var channelModel: buildChannelModel()

    function pauseMedia() { if (playing) player.pause() }

    function releaseMedia() {
        loading = false
        retryTimer.stop()
        // 显式释放 VLC 解码器与媒体：Pi 上硬解实例必须及时回收
        player.release()
        if (player.source !== "") player.source = ""
    }

    function setFullscreen(value) {
        if (fullscreen === value) return
        if (!hostWindow) return
        if (value) {
            previousWindowMaximized = hostWindow.visibility === Window.Maximized
            hostWindow.showFullScreen()
        } else if (previousWindowMaximized) {
            hostWindow.showMaximized()
        } else {
            hostWindow.showNormal()
        }
        fullscreen = value
        controlsVisible = true
        if (value) controlsTimer.restart(); else controlsTimer.stop()
    }

    function revealControls() {
        // 直播的 playbackState 会在 Playing/Buffering 间反复跳变，
        // 只有控件已隐藏时才重新计时，否则永远等不到隐藏。
        if (controlsVisible && controlsTimer.running) return
        controlsVisible = true
        if (playing) controlsTimer.restart(); else controlsTimer.stop()
    }

    function loadChannels() {
        const testUrl = systemTools.tvTestUrl()
        if (testUrl !== "") {
            root.categories = [{ title: "测试源", channels: [{ name: "测试源", sources: [{ url: testUrl, height: 720 }] }] }]
            root.categoryIndex = 0
            root.playChannel(0)
            return
        }
        // 走 C++：QML 的 XMLHttpRequest 默认禁止读取 qrc 本地文件。
        const text = systemTools.tvChannelData()
        if (!text || text.length === 0) {
            errorText = I18n.t("频道数据加载失败")
            return
        }
        try {
            const data = JSON.parse(text)
            root.categories = data.categories || []
            if (root.categories.length > 0) root.categoryIndex = 0
            const preferred = systemTools.tvChannelName()
            if (preferred !== "") {
                for (let ci = 0; ci < root.categories.length; ++ci) {
                    const channels = root.categories[ci].channels || []
                    for (let chi = 0; chi < channels.length; ++chi) {
                        if (channels[chi].name === preferred) {
                            root.categoryIndex = ci
                            root.playChannel(chi)
                            return
                        }
                    }
                }
            }
            if (systemTools.tvAutoplay() && root.currentChannels.length > 0) root.playChannel(0)
        } catch (error) {
            errorText = I18n.t("频道数据解析失败")
        }
    }

    // Pi 3B 优先低分辨率：按 height 从低到高试源（480p → 720p → 1080p）。
    // 原来的实现只挑 ≤480p，没有 480p 时直接按原始顺序，常常一上来就吃 1080p。
    function lowResSources(channel) {
        if (!channel || !channel.sources) return []
        // Qt 的 Array.prototype.sort 不稳定；同 height 的镜像必须保持原顺序，
        // 否则首选的 1080p（带码率信息那条）会被镜像挤到后面。
        return channel.sources.map(function(source, index) {
            return { source: source, index: index }
        }).sort(function(a, b) {
            const ah = a.source.height || 99999
            const bh = b.source.height || 99999
            return ah !== bh ? ah - bh : a.index - b.index
        }).map(function(entry) { return entry.source })
    }

    function sourceEntry(index, height, width, bitrate) {
        const rate = bitrate > 0 ? (bitrate / 1000).toFixed(1) + " Mbps" : ""
        const res = width > 0 ? width + "×" + height : ""
        const detail = [res, rate].filter(function(part) { return part !== "" }).join("，")
        return { index: index, height: height, width: width, bitrate: bitrate,
                 label: height > 0 ? height + "P" : I18n.t("线路") + (index + 1),
                 detail: detail }
    }

    function buildSourceOptions(channel) {
        const list = lowResSources(channel)
        const out = []
        const seen = {}
        for (let i = 0; i < list.length; ++i) {
            const height = list[i].height || 0
            const width = list[i].width || 0
            const bitrate = list[i].bitrate || 0
            if (seen[height] === undefined) {
                seen[height] = [out.length]
                out.push(sourceEntry(i, height, width, bitrate))
                continue
            }
            // 同一清晰度：码率相差 <15% 视为同档镜像（只留一条）；
            // 相差明显（例如 1080P 9.2M 与 1080P 3.5M）则单独成一条，方便切低码率
            let duplicate = false
            for (const slot of seen[height]) {
                const rate = out[slot].bitrate
                if (rate > 0 && bitrate > 0 && Math.abs(rate - bitrate) <= 0.15 * rate)
                    duplicate = true
                if (rate === 0 && bitrate > 0) {
                    out[slot] = { index: i, height: height, width: width, bitrate: bitrate }
                    duplicate = true
                }
            }
            if (!duplicate) {
                seen[height].push(out.length)
                out.push(sourceEntry(i, height, width, bitrate))
            }
        }
        return out
    }

    // 默认线路：优先选实测码率最低的源（没有码率信息时退回清晰度最低的第一条）
    function enterCategory(index) {
        if (index < 0 || index >= categories.length) return
        categoryIndex = index
        browsing = true
        if (searchField.text !== "") searchField.clear()
    }

    function leaveDrilldown() {
        browsing = false
        if (searchField.text !== "") searchField.clear()
    }

    function toggleKeyboard() {
        if (!virtualKeyboard) return
        if (virtualKeyboard.visible) virtualKeyboard.hide()
        else { searchField.forceActiveFocus(); keyboardRequested() }
    }

    function buildChannelModel() {
        const query = searchText.trim().toLowerCase()
        const out = []
        if (query === "") {
            const list = currentChannels
            for (let i = 0; i < list.length; ++i)
                out.push({ channel: list[i], categoryIndex: categoryIndex, channelIndex: i })
            return out
        }
        for (let ci = 0; ci < categories.length; ++ci) {
            const list = categories[ci].channels || []
            for (let chi = 0; chi < list.length; ++chi) {
                const name = (list[chi].name || "").toLowerCase()
                if (name.indexOf(query) >= 0)
                    out.push({ channel: list[chi], categoryIndex: ci, channelIndex: chi })
            }
        }
        return out
    }

    // 搜索结果的分类可能不是当前分类，先切分类再定位频道
    function activateEntry(entry) {
        if (!entry || !entry.channel) return
        browsing = true
        categoryIndex = entry.categoryIndex
        channelIndex = entry.channelIndex
        sourceIndex = defaultSourceIndex(entry.channel)
        currentName = entry.channel.name
        startSource(entry.channel)
    }

    function defaultSourceIndex(channel) {
        const list = lowResSources(channel)
        let best = 0
        for (let i = 1; i < list.length; ++i) {
            const rate = list[i].bitrate || 0
            const bestRate = list[best].bitrate || 0
            if (rate > 0 && (bestRate === 0 || rate < bestRate)) best = i
        }
        return best
    }

    function sourceOptionLabel(source) {
        return source.height > 0 ? source.height + "P" : I18n.t("线路") + (source.index + 1)
    }

    // 弹窗里的第二行：分辨率，码率（频道数据里没有码率时只显示分辨率）
    function sourceSummary(source) {
        const parts = []
        if (source.width > 0 && source.height > 0) parts.push(source.width + "×" + source.height)
        if (source.bitrate > 0) parts.push((source.bitrate / 1000).toFixed(1) + "\u00a0Mbps")
        return parts.join("，")
    }

    // 点选线路：更高码率先弹确认（Pi 3B 带宽/解码余量有限，切上去可能卡）
    function requestSource(index) {
        if (index < 0 || index >= currentSources.length || index === sourceIndex) {
            qualityCombo.syncToSource()
            return
        }
        const target = currentSources[index]
        const current = currentSources[sourceIndex]
        const targetRate = target && target.bitrate ? target.bitrate : 0
        const currentRate = current && current.bitrate ? current.bitrate : 0
        const higher = targetRate > 0 && (currentRate === 0 || targetRate > currentRate)
        const taller = targetRate === 0 && currentRate === 0
            && target && current && target.height > current.height
        if (higher || taller) {
            pendingSourceIndex = index
            qualityCombo.syncToSource()
            switchWarning.open()
            return
        }
        switchSource(index)
    }

    // 底部控制栏里的线路切换：先 release 再换源（Pi 上只能有一个硬解实例）
    function switchSource(index) {
        if (index < 0 || index >= currentSources.length || index === sourceIndex) return
        if (channelIndex < 0 || channelIndex >= currentChannels.length) return
        sourceIndex = index
        errorText = ""
        loading = true
        startSource(currentChannels[channelIndex])
    }

    function startSource(channel) {
        if (!channel) return
        const sources = lowResSources(channel)
        if (sources.length === 0) {
            errorText = I18n.t("该频道暂时没有可用源")
            releaseMedia()
            return
        }
        if (sourceIndex >= sources.length) {
            errorText = I18n.t("所有源均无法播放，请换一个频道")
            releaseMedia()
            return
        }
        errorText = ""
        loading = true
        statusText = I18n.t("正在连接") + " (" + (sourceIndex + 1) + "/" + sources.length + ")"
        controlsVisible = true
        controlsTimer.restart()
        // Pi 3B 同时只能有一个 H.264 硬解实例：必须先彻底停止旧 pipeline
        player.release()
        player.source = sources[sourceIndex].url
        player.play()
    }

    function playChannel(index) {
        if (index < 0 || index >= currentChannels.length) return
        browsing = true
        channelIndex = index
        if (searchText.trim() === "")
            Qt.callLater(function() { channelList.positionViewAtIndex(Math.max(0, index), ListView.Contain) })
        sourceIndex = defaultSourceIndex(currentChannels[index])
        currentName = currentChannels[index].name
        startSource(currentChannels[index])
    }

    function selectCategory(index) {
        enterCategory(index)
    }

    function retryNextSource() {
        if (channelIndex < 0 || channelIndex >= currentChannels.length) return
        sourceIndex += 1
        retryTimer.restart()
    }

    function togglePlay() {
        if (playing) { player.pause(); revealControls() }
        else if (currentName !== "") { player.resume(); revealControls() }
    }

    onVolumeChanged: player.setVolume(root.muted ? 0 : root.volume)
    onMutedChanged: player.setVolume(root.muted ? 0 : root.volume)

    Component.onCompleted: {
        loadChannels()
        player.setVolume(root.muted ? 0 : root.volume)
    }

    onActiveChanged: {
        if (!active) {
            releaseMedia()
            if (fullscreen) setFullscreen(false)
        } else if (systemTools.tvForceFullscreen()) {
            setFullscreen(true)
        }
    }

    onFullscreenChanged: if (hostWindow) hostWindow.videoFullscreen = fullscreen

    Shortcut { sequence: "Escape"; enabled: root.fullscreen; onActivated: root.setFullscreen(false) }
    Shortcut { sequence: "F"; enabled: root.active; onActivated: root.setFullscreen(!root.fullscreen) }

    Timer { id: retryTimer; interval: 1500; repeat: false; onTriggered: root.startSource(root.currentChannels[root.channelIndex]) }
    Timer { id: controlsTimer; interval: 3500; repeat: false; onTriggered: root.controlsVisible = false }
    FontMetrics { id: chipMetrics; font.pointSize: Theme.body }
    FontMetrics { id: qualityMetrics; font.pointSize: Theme.body }

    // 播放内核由视频区域内的 GstVideoItem 实例承载（见下）。
    RowLayout {
        anchors.fill: parent
        anchors.margins: root.fullscreen ? 0 : Theme.gap * 0.6
        spacing: root.fullscreen ? 0 : Theme.gap * 0.4

        // 左列：搜索栏 + 侧栏，宽度与节目面板一致；右侧整高留给视频
        ColumnLayout {
            objectName: "tvLeftPane"
            visible: !root.fullscreen
            Layout.preferredWidth: root.paneWidth
            Layout.minimumWidth: root.paneWidth
            Layout.maximumWidth: root.paneWidth
            Layout.fillHeight: true
            spacing: Theme.gap * 0.4

            // ── 搜索栏：键盘按钮 + 搜索框（全屏时隐藏）──
        RowLayout {
            objectName: "tvSearchBar"
            visible: !root.fullscreen
            Layout.fillWidth: true
            spacing: Theme.gap * 0.5

            // 本模块专用键盘图标（不影响 DOCK 栏用的全局 keyboard 图标）
            IconButton {
                objectName: "tvKeyboardButton"
                iconName: "keyboard_tv"
                label: I18n.t("打开屏幕键盘")
                implicitWidth: Theme.hit
                implicitHeight: Theme.hit
                onClicked: root.toggleKeyboard()
            }
            CarField {
                id: searchField
                objectName: "tvSearchField"
                Layout.fillWidth: true
                placeholderText: I18n.t("搜索节目")
                onTextChanged: root.searchText = text
            }
            IconButton {
                objectName: "tvSearchClear"
                visible: searchField.text !== ""
                iconName: "close"
                label: I18n.t("清除搜索")
                implicitWidth: Theme.hit
                implicitHeight: Theme.hit
                onClicked: searchField.clear()
            }
        }

            // ── 侧栏：一级分类 → 二级节目（像文件管理器一样逐级进入）──
            Rectangle {
                objectName: "tvSidePanel"
                visible: !root.fullscreen
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.shapeLarge
                color: Theme.surface
                border.color: Theme.outlineVariant
                border.width: 1
                clip: true

                // 一级菜单：分类
                ColumnLayout {
                    objectName: "tvCategoryList"
                    anchors.fill: parent
                    anchors.margins: Theme.gap * 0.4
                    spacing: Theme.gap * 0.25
                    visible: !root.drilldown

                    CarText {
                        Layout.fillWidth: true
                        Layout.leftMargin: 4 * root.uiScale
                        text: I18n.t("选择分类")
                        subdued: true
                        maximumLineCount: 1
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: root.categories
                        delegate: Button {
                            required property var modelData
                            required property int index
                            objectName: "tvCategoryRow"
                            Layout.fillWidth: true
                            implicitHeight: Theme.hit
                            padding: 0
                            leftPadding: 12 * root.uiScale
                            rightPadding: 10 * root.uiScale
                            leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0
                            Accessible.name: modelData.title
                            onClicked: root.enterCategory(index)
                            background: Rectangle {
                                radius: Theme.shapeSmall
                                color: index === root.categoryIndex ? Theme.primaryContainer
                                      : (parent.hovered ? Theme.surfaceContainer : "transparent")
                            }
                            contentItem: RowLayout {
                                spacing: Theme.gap * 0.4
                                MaterialIcon {
                                    Layout.alignment: Qt.AlignVCenter
                                    name: "folder"
                                    Layout.preferredWidth: 22 * root.uiScale
                                    Layout.preferredHeight: 22 * root.uiScale
                                }
                                CarText {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                    color: index === root.categoryIndex ? Theme.accent : Theme.secondary
                                }
                                MaterialIcon {
                                    Layout.alignment: Qt.AlignVCenter
                                    name: "chevron_right"
                                    Layout.rightMargin: 8 * root.uiScale
                                    Layout.preferredWidth: 18 * root.uiScale
                                    Layout.preferredHeight: 18 * root.uiScale
                                    opacity: 0.7
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }

                // 二级菜单：节目列表 + 返回
                Item {
                    objectName: "tvChannelPanel"
                    anchors.fill: parent
                    visible: root.drilldown

                    RowLayout {
                        id: drillHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.gap * 0.4
                        spacing: Theme.gap * 0.3

                        IconButton {
                            objectName: "tvBackButton"
                            iconName: "arrow_back"
                            label: I18n.t("返回分类")
                            implicitWidth: 40 * root.uiScale
                            implicitHeight: 40 * root.uiScale
                            onClicked: root.leaveDrilldown()
                        }
                        CarText {
                            Layout.fillWidth: true
                            text: root.drilldownTitle
                            heading: true
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                    }

                    ListView {
                        id: channelList
                        objectName: "tvChannelList"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: drillHeader.bottom
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.gap * 0.4
                        clip: true
                        model: root.channelModel
                        onCountChanged: {
                            if (root.searchText.trim() === "" && root.channelIndex >= 0)
                                positionViewAtIndex(Math.max(0, root.channelIndex), ListView.Contain)
                        }
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Button {
                            required property var modelData
                            required property int index
                            objectName: "tvChannelRow"
                            width: channelList.width
                            implicitHeight: 52 * root.uiScale
                            padding: 0
                            leftPadding: 12 * root.uiScale
                            rightPadding: 10 * root.uiScale
                            leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0
                            Accessible.name: modelData.channel.name
                            onClicked: root.activateEntry(modelData)
                            background: Rectangle {
                                radius: Theme.shapeSmall
                                color: (modelData.categoryIndex === root.categoryIndex
                                        && modelData.channelIndex === root.channelIndex)
                                      ? Theme.primaryContainer
                                      : (hovered ? Theme.surfaceContainer : "transparent")
                            }
                            contentItem: RowLayout {
                                spacing: Theme.gap * 0.4

                                Item {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.preferredWidth: 32 * root.uiScale
                                    Layout.preferredHeight: 32 * root.uiScale
                                    MaterialIcon {
                                        anchors.centerIn: parent
                                        name: "tv"
                                        visible: channelLogo.status !== Image.Ready
                                        width: 24 * root.uiScale
                                        height: 24 * root.uiScale
                                    }
                                    Image {
                                        id: channelLogo
                                        anchors.fill: parent
                                        source: modelData.channel.logo ? modelData.channel.logo : ""
                                        sourceSize.width: 64
                                        sourceSize.height: 64
                                        asynchronous: true
                                        cache: true
                                        fillMode: Image.PreserveAspectFit
                                        visible: status === Image.Ready
                                    }
                                }
                                CarText {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.fillWidth: true
                                    text: modelData.channel.name
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                    color: (modelData.categoryIndex === root.categoryIndex
                                            && modelData.channelIndex === root.channelIndex)
                                          ? Theme.accent : Theme.secondary
                                }
                                CarText {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: (modelData.channel.sources ? modelData.channel.sources.length : 0) + ""
                                    subdued: true
                                    visible: modelData.channel.sources && modelData.channel.sources.length > 1
                                }
                            }
                        }

                        CarText {
                            anchors.centerIn: parent
                            width: parent.width * 0.8
                            horizontalAlignment: Text.AlignHCenter
                            visible: channelList.count === 0
                            wrapMode: Text.Wrap
                            subdued: true
                            text: root.searchText.trim() !== "" ? I18n.t("没有匹配的节目") : I18n.t("暂无节目")
                        }
                    }
                }
            }



        }

        // ── 播放区：按源画面比例自适应，消掉黑边 ──
        Item {
            objectName: "tvVideoPane"
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: videoFrame
                readonly property real sourceAspect:
                    player.videoSize.height > 0 ? player.videoSize.width / player.videoSize.height : (16 / 9)
                width: Math.min(parent.width, parent.height * sourceAspect)
                height: Math.round(width / sourceAspect)
                anchors.centerIn: parent
                radius: root.fullscreen ? 0 : Theme.shapeLarge
                color: "#000000"
                clip: true

                GstVideoItem {
                    id: player
                    objectName: "tvVideoItem"
                    anchors.fill: parent
                    onPlayingChanged: {
                        if (playing) {
                            root.loading = false
                            root.statusText = I18n.t("正在播放")
                            root.errorText = ""
                            root.revealControls()
                            player.setVolume(root.muted ? 0 : root.volume)
                        }
                    }
                    onErrorTextChanged: {
                        if (errorText === "") return
                        root.loading = false
                        root.statusText = ""
                        root.errorText = errorText
                        root.controlsVisible = true
                        root.retryNextSource()
                    }
                }

                // 点击/移动唤出控件；隐藏后光标也隐藏（与 VideoPlayer 一致）
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: root.playing && !root.controlsVisible ? Qt.BlankCursor : Qt.ArrowCursor
                    onPositionChanged: function(mouse) {
                        if (Math.abs(mouse.x - root.lastMouse.x) + Math.abs(mouse.y - root.lastMouse.y) < 3) return
                        root.lastMouse = Qt.point(mouse.x, mouse.y)
                        root.revealControls()
                    }
                    onClicked: root.revealControls()
                }

                // 未播放时的中央提示
                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width * 0.8
                    spacing: Theme.gap * 0.5
                    visible: !root.playing && root.errorText !== ""
                    CarText {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.currentName !== "" ? root.currentName
                              : I18n.t("选择一个频道开始播放")
                        heading: true
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignHCenter
                    }
                    CarText {
                        Layout.fillWidth: true
                        text: root.errorText
                        color: Theme.warning
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // ── 控制条：自动隐藏，规格与 VideoPlayer 全屏控件一致 ──
                Rectangle {
                    id: controlsBar
                    objectName: "tvControlsBar"
                    z: 2
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: barRow.implicitHeight + (root.compact ? Theme.gap * 0.5 : Theme.gap)
                    color: "#d90a111c"
                    opacity: root.controlsVisible ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity { NumberAnimation { duration: 180 } }
                    // 中间留给码率选择器，左右两组各占剩余的一半
                    readonly property real centerSlot:
                        (qualityCombo.visible ? qualityCombo.implicitWidth : 0) + Theme.gap * 0.6

                    RowLayout {
                        id: barRow
                        anchors.left: parent.left
                        anchors.leftMargin: root.compact ? Theme.gap * 0.5 : Theme.gap
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, (parent.width - parent.centerSlot) / 2 - Theme.gap * 0.5)
                        spacing: Theme.gap * 0.4

                        IconButton {
                            objectName: "tvPlayPause"
                            iconName: root.playing ? "pause" : "play_arrow"
                            label: root.playing ? I18n.t("暂停") : I18n.t("继续播放")
                            implicitWidth: 56 * root.uiScale
                            implicitHeight: 56 * root.uiScale
                            filled: true
                            enabled: root.currentName !== ""
                            onClicked: root.togglePlay()
                        }
                        IconButton {
                            objectName: "tvRetry"
                            iconName: "refresh"
                            label: I18n.t("重新连接")
                            implicitWidth: 56 * root.uiScale
                            implicitHeight: 56 * root.uiScale
                            enabled: root.currentName !== ""
                            onClicked: { root.sourceIndex = root.defaultSourceIndex(root.currentChannels[root.channelIndex]); root.startSource(root.currentChannels[root.channelIndex]) }
                        }
                        CarText {
                            objectName: "tvNowPlaying"
                            Layout.fillWidth: true
                            text: root.currentName !== "" ? root.currentName : I18n.t("网络电视")
                            heading: true
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        id: barRight
                        anchors.right: parent.right
                        anchors.rightMargin: root.compact ? Theme.gap * 0.5 : Theme.gap
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, (parent.width - parent.centerSlot) / 2 - Theme.gap * 0.5)
                        spacing: Theme.gap * 0.4

                        IconButton {
                            iconName: root.muted ? "volume_off" : "volume_up"
                            label: root.muted ? I18n.t("打开声音") : I18n.t("静音")
                            implicitWidth: 44 * root.uiScale
                            implicitHeight: 56 * root.uiScale
                            onClicked: root.muted = !root.muted
                        }
                        Slider {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80 * root.uiScale
                            from: 0; to: 1
                            value: root.muted ? 0 : root.volume
                            onMoved: { root.volume = value; if (value > 0) root.muted = false }
                        }
                        IconButton {
                            objectName: "tvFullscreen"
                            iconName: root.fullscreen ? "close_fullscreen" : "fullscreen"
                            label: root.fullscreen ? I18n.t("退出全屏") : I18n.t("全屏播放")
                            implicitWidth: 56 * root.uiScale
                            implicitHeight: 56 * root.uiScale
                            onClicked: root.setFullscreen(!root.fullscreen)
                        }
                    }

                    // 码率选择器：仿记事本「段落对齐」下拉的 Material ComboBox 风格
                    ComboBox {
                        id: qualityCombo
                        objectName: "tvQualityCombo"
                        visible: root.sourceOptions.length > 1
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        implicitHeight: 44 * root.uiScale
                        implicitWidth: Math.max(136 * root.uiScale,
                                                qualityMetrics.advanceWidth(currentText) + 64 * root.uiScale)
                        model: root.sourceOptions
                        textRole: "label"
                        font.pointSize: Theme.body
                        Accessible.name: I18n.t("选择线路") + "，" + currentText
                        onActivated: function(index) {
                            root.requestSource(root.sourceOptions[index].index)
                        }
                        // 控件在底部：向上弹出，宽度加大放完整的分辨率/码率
                        popup.y: -popup.implicitHeight - 6
                        popup.width: Math.max(qualityCombo.width, 286 * root.uiScale)
                        onModelChanged: syncToSource()
                        Component.onCompleted: syncToSource()

                        function syncToSource() {
                            for (let i = 0; i < root.sourceOptions.length; ++i) {
                                if (root.sourceOptions[i].index === root.sourceIndex) {
                                    currentIndex = i
                                    return
                                }
                            }
                            currentIndex = root.sourceOptions.length > 0 ? 0 : -1
                        }

                        Connections {
                            target: root
                            function onSourceIndexChanged() { qualityCombo.syncToSource() }
                        }

                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: ListView.view ? ListView.view.width : implicitWidth
                            implicitHeight: Math.max(48 * root.uiScale,
                                                     contentItem.implicitHeight + 16 * root.uiScale)
                            highlighted: qualityCombo.highlightedIndex === index
                            onClicked: qualityCombo.activated(index)
                            contentItem: ColumnLayout {
                                spacing: 2
                                CarText {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                }
                                CarText {
                                    Layout.fillWidth: true
                                    text: modelData.detail
                                    visible: text !== ""
                                    fontPixelSize: 12
                                    maximumLineCount: 1
                                    elide: Text.ElideRight
                                    subdued: true
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 高码率切换确认框：提示可能卡顿，确认后才真正切
    Popup {
        id: switchWarning
        objectName: "tvSwitchWarning"
        parent: root
        modal: true
        dim: true
        padding: 18 * root.uiScale
        width: Math.min(430 * root.uiScale, root.width - 40)
        height: warningColumn.implicitHeight + 36 * root.uiScale
        anchors.centerIn: parent
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            const target = root.currentSources[root.pendingSourceIndex]
            root.pendingSourceLabel = target
                ? root.sourceOptionLabel(target) + (root.sourceSummary(target) !== ""
                    ? "（" + root.sourceSummary(target) + "）" : "")
                : ""
            controlsTimer.stop()
        }
        onClosed: root.revealControls()
        background: Rectangle {
            color: Theme.surfaceContainerHigh
            radius: Theme.shapeLarge
            border.width: 1
            border.color: Theme.outlineVariant
        }
        contentItem: ColumnLayout {
            id: warningColumn
            spacing: Theme.gap * 0.7
            CarText {
                Layout.fillWidth: true
                text: I18n.t("切换到高码率线路？")
                heading: true
                maximumLineCount: 1
            }
            CarText {
                Layout.fillWidth: true
                text: I18n.t("高码率占用更多带宽，树莓派可能因此卡顿。确定切换到 ") + root.pendingSourceLabel + I18n.t(" 吗？")
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.gap * 0.4
                spacing: Theme.gap * 0.5
                Item { Layout.fillWidth: true }
                CarButton {
                    objectName: "tvSwitchCancel"
                    text: I18n.t("取消")
                    onClicked: switchWarning.close()
                }
                CarButton {
                    objectName: "tvSwitchConfirm"
                    text: I18n.t("确认切换")
                    accent: true
                    onClicked: {
                        const index = root.pendingSourceIndex
                        switchWarning.close()
                        root.switchSource(index)
                    }
                }
            }
        }
    }


}
