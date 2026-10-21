import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtMultimedia

Item {
    id: root
    objectName: "musicPlayerPage"
    Material.theme:Material.Dark
    Material.accent:"#1ed760"
    Material.foreground:"#ffffff"
    Material.background:"#181818"
    Rectangle {id:musicPageBackground;anchors.fill:parent;color:"#121212";z:-1}
    property bool active: false
    // Inactive StackLayout pages can have a zero size. Keep their last valid
    // visual geometry without unloading the controls or the media player.
    property size contentViewport: Qt.size(640,400)
    function updateContentViewport() {
        if (active && width > 0 && height > 0) contentViewport = Qt.size(width,height)
    }
    onWidthChanged: updateContentViewport()
    onHeightChanged: updateContentViewport()
    property real uiScale: 1
    property string currentTitle: ""
    readonly property bool mediaPlaying: audioPlayer.playbackState === MediaPlayer.PlayingState
    readonly property bool hasMedia: audioPlayer.source.toString() !== ""
    readonly property real mediaPosition: audioPlayer.position
    readonly property real mediaDuration: audioPlayer.duration
    function toggleMedia() { if (hasMedia && !onlineLoading) mediaPlaying ? audioPlayer.pause() : audioPlayer.play() }
    function pauseMedia() { audioPlayer.pause() }
    property string currentArtist: ""
    property string currentCover: ""
    property string currentAlbum: ""
    property string currentGenre: ""
    property string currentYear: ""
    property string lastPlayRequestId: ""
    property double lastPlayRequestAt: 0
    property string currentRelative: ""
    property string folder: ""
    property var entries: []
    property var queue: []
    property int queueIndex: -1
    property string mode: "offline"
    property string searchText: ""
    property string statusText: ""
    property bool onlinePaused: true
    property real volume: 0.8
    property real announcementGain: systemTools.mediaGain
    Behavior on announcementGain { NumberAnimation { duration: 120 } }
    property bool compact: width < 1200 || height < 700
    property string currentPlaylistId: ""
    property string currentPlaylistName: ""
    property string playlistScope: "created"
    property var onlineRows: []
    // Playback snapshot survives searches, playlist navigation and pagination.
    property var onlineQueue: []
    property int onlineQueueIndex: -1
    property var playlistBrowserRows: []
    property int pendingSeekSeconds: 0
    property real seekPreviewPosition: 0
    property bool seeking: false
    property bool seekCachePending: false
    property bool seekCacheSourceSwap: false
    property bool seekCachePositionApplied: false
    property bool seekCacheSawLoading: false
    property bool cachePrimeRequested: false
    property bool cachePrimeMuted: false
    property string seekCacheTrackId: ""
    property string cachedOnlineTrackId: ""
    property url onlineSourceUrl: ""
    property real pendingCachedSeekPosition: 0
    property real preCachePlaybackPosition: 0
    property bool resumeCachedSeekPlayback: false
    property int cachedSeekPollCount: 0
    property bool showTranslation: true
    function previewSeek(position) {
        if (!Number.isFinite(position) || !audioPlayer.seekable || audioPlayer.duration <= 0 || onlineLoading) return false
        seekSettleTimer.stop()
        seekPreviewPosition = Math.max(0, Math.min(audioPlayer.duration - 1, Math.round(position)))
        seeking = true
        return true
    }
    function requestSeek(position) {
        if (!previewSeek(position)) return
        seekCommitTimer.restart()
    }
    function commitSeek() {
        seekCommitTimer.stop()
        if (!seeking || !audioPlayer.seekable || audioPlayer.duration <= 0 || onlineLoading) { cancelSeek(); return }
        const target = Math.max(0, Math.min(audioPlayer.duration - 1, Math.round(seekPreviewPosition)))
        if (onlinePlayback) {
            // YesPlayMusic's cacheTrackSource stores the complete audio body,
            // and its Player reads that cache before creating a Blob source.
            // Keep the current remote stream untouched until the whole track
            // is atomically cached; the selected timestamp is then sought in
            // the local file before playback resumes.
            seekCachePending = true
            seekCacheTrackId = pendingOnlineId
            pendingCachedSeekPosition = target
            statusText = I18n.t("正在缓存跳转音频")
            musicService.cacheAudioForSeek(pendingOnlineId, onlineSourceUrl.toString())
            return
        }
        // Local/offline source, or a fully cached online track: seek in place.
        audioPlayer.position = target
        seekSettleTimer.restart()
    }
    function cancelSeek() {
        if (seekCachePending && seekCacheTrackId !== "") musicService.cancelSeekAudioCache(seekCacheTrackId)
        seekCommitTimer.stop(); seekSettleTimer.stop(); seeking = false
        seekCacheTimer.stop(); seekCachePending = false; seekCacheSourceSwap = false
        seekCachePositionApplied = false; seekCacheSawLoading = false
        cachePrimeRequested = false; cachePrimeMuted = false
        seekCacheTrackId = ""; cachedSeekPollCount = 0
    }
    function beginCachedSeek(id, localSource) {
        if (!seekCachePending || id !== pendingOnlineId || id !== seekCacheTrackId || !localSource.toString().startsWith("file:")) return
        cachedOnlineTrackId = id
        preCachePlaybackPosition = audioPlayer.position
        resumeCachedSeekPlayback = audioPlayer.playbackState === MediaPlayer.PlayingState
        pendingCachedSeekPosition = Math.max(0, Math.min(audioPlayer.duration - 1, Math.round(seekPreviewPosition)))
        seekCachePending = false
        seekCacheSourceSwap = true
        const sourceAlreadyCached = audioPlayer.source.toString() === localSource.toString()
        seekCachePositionApplied = false
        seekCacheSawLoading = sourceAlreadyCached
        cachePrimeRequested = sourceAlreadyCached
        cachePrimeMuted = sourceAlreadyCached
        cachedSeekPollCount = 0
        statusText = I18n.t("已缓存音频，正在定位")
        if (sourceAlreadyCached) {
            if (resumeCachedSeekPlayback) audioPlayer.pause()
            else applyCachedSeekPosition()
        } else {
            audioPlayer.source = localSource
        }
        seekCacheTimer.restart()
        Qt.callLater(applyCachedSeekPosition)
    }
    function applyCachedSeekPosition() {
        if (!seekCacheSourceSwap || !seekCacheSawLoading || !audioPlayer.source.toString().startsWith("file:") || seekCachePositionApplied || audioPlayer.duration <= 0) return
        if (audioPlayer.mediaStatus !== MediaPlayer.LoadedMedia && audioPlayer.mediaStatus !== MediaPlayer.BufferedMedia) return
        if (!cachePrimeRequested) {
            // Prime the complete local file muted. Once GStreamer has entered
            // PLAYING, pause this local pipeline, seek its buffered decoder,
            // then restore gain and resume at the selected timestamp.
            cachePrimeMuted = true
            cachePrimeRequested = true
            audioPlayer.play()
            return
        }
        if (audioPlayer.playbackState !== MediaPlayer.PausedState || !audioPlayer.seekable) return
        pendingCachedSeekPosition = Math.max(0, Math.min(audioPlayer.duration - 1, Math.round(pendingCachedSeekPosition)))
        audioPlayer.position = pendingCachedSeekPosition
        seekCachePositionApplied = true
    }
    function pollCachedSeekPosition() {
        if (!seekCacheSourceSwap) { seekCacheTimer.stop(); return }
        applyCachedSeekPosition()
        if (seekCachePositionApplied && Math.abs(audioPlayer.position - pendingCachedSeekPosition) <= 900) {
            seekCacheTimer.stop()
            seekCacheSourceSwap = false
            seekCachePositionApplied = false
            seekCacheSawLoading = false
            cachePrimeRequested = false
            cachePrimeMuted = false
            seeking = false
            cachedSeekPollCount = 0
            statusText = ""
            if (resumeCachedSeekPlayback) audioPlayer.play()
            else onlinePaused = true
            return
        }
        if (++cachedSeekPollCount >= 100) {
            seekCacheTimer.stop()
            failCachedSeek(I18n.t("缓存音频定位失败，正在恢复在线播放"))
        }
    }
    function failCachedSeek(message) {
        seekCacheTimer.stop(); seekCacheSourceSwap = false; seekCachePositionApplied = false; seekCacheSawLoading = false
        cachePrimeRequested = false; cachePrimeMuted = false; seeking = false; seekCachePending = false
        statusText = message
        if (onlineSourceUrl.toString() !== "") {
            cachedOnlineTrackId = ""
            audioPlayer.source = onlineSourceUrl
            audioPlayer.play()
        }
    }
    property bool autoNetworkMode: true
    property bool onlineLoading: false
    property string pendingOnlineId: ""
    property bool onlinePlayback: false
    property int playlistOffset: 0
    property string repeatMode: "off"
    property bool shufflePlayback: false
    property var playbackOrder: []
    readonly property var playingQueue: onlinePlayback ? onlineQueue : queue
    readonly property int playingIndex: onlinePlayback ? onlineQueueIndex : queueIndex
    property int lyricIndex: -1
    readonly property int trackTextSize: Math.max(16, Math.round(20 * uiScale))
    readonly property int secondaryTextSize: Math.max(14, Math.round(17 * uiScale))
    property var auditLyrics: null
    readonly property var lyricRows: auditLyrics !== null ? auditLyrics : musicService.lyrics
    function auditMusicPalette() {
        const colors=[]
        function visit(item){
            if(!item||!item.visible)return
            if(item.musicText && colors.indexOf(String(item.color))<0)colors.push(String(item.color))
            const childItems=item.children
            if(childItems)for(let i=0;i<childItems.length;++i)visit(childItems[i])
        }
        visit(root)
        for(const dialog of [queueDialog,lyricsDialog,loginDialog])if(dialog.visible)visit(dialog.contentItem)
        return {dark:root.Material.theme===Material.Dark,background:String(musicPageBackground.color),textColors:colors}
    }
    function auditMusicScenario(scenario) {
        autoNetworkMode = false; mode = "online"
        const tracks = []
        for (let i = 0; i < 50; ++i) tracks.push({originalId: String(i + 1), encryptedId: String(i + 1), type: "song", playable: true,
            name: i % 2 ? "A very long song title — live at the midnight concert" : "夜空中最亮的星 · 现场特别版与漫长的歌曲标题",
            artist: i % 2 ? "The Midnight Orchestra / Guest Artist" : "逃跑计划 / 特邀歌手", album: "Live Collection", duration: 245000, index: i + 1})
        onlineRows = tracks; onlineQueue = tracks.slice(); onlineQueueIndex = 12; onlinePlayback = true
        currentTitle = tracks[12].name; currentArtist = tracks[12].artist; pendingOnlineId = "13"
        if (scenario === "queue") queueDialog.open()
        if (scenario === "lyrics") {
            const lines = []
            for (let i = 0; i < 20; ++i) lines.push({time:i * 3000, text:i % 2 ? "Can you hear the stars singing tonight?" : "夜空中最亮的星，请照亮我前行的路", translation:i % 2 ? "你能听见今夜繁星的歌声吗？" : ""})
            auditLyrics = lines; lyricsDialog.open()
        }
    }

    function rebuildPlaybackOrder() {
        let order = playingQueue.map(function(_, index) { return index })
        if (shufflePlayback) {
            order = order.filter(function(index) { return index !== root.playingIndex })
            for (let i = order.length - 1; i > 0; --i) {
                const j = Math.floor(Math.random() * (i + 1))
                const saved = order[i]; order[i] = order[j]; order[j] = saved
            }
            order.unshift(playingIndex)
        }
        playbackOrder = order
    }
    function playQueueIndex(index) {
        if (index < 0 || index >= playingQueue.length) return
        if (onlinePlayback) { onlineQueueIndex = index; startOnlineTrack(onlineQueue[index]) }
        else playEntry(index)
    }
    function navigatePlayback(delta, natural) {
        if (!playingQueue.length) return
        if (natural && repeatMode === "one") { playQueueIndex(playingIndex); return }
        if (playbackOrder.length !== playingQueue.length) rebuildPlaybackOrder()
        const cursor = playbackOrder.indexOf(playingIndex)
        const target = cursor + delta
        if (target < 0 || target >= playbackOrder.length) {
            if (repeatMode !== "all") return
        }
        playQueueIndex(playbackOrder[(target + playbackOrder.length) % playbackOrder.length])
    }
    function updateLyricPosition() {
        const lines = lyricRows
        let low = 0, high = lines.length - 1, found = -1
        while (low <= high) {
            const mid = Math.floor((low + high) / 2)
            if (Number(lines[mid].time) <= audioPlayer.position) { found = mid; low = mid + 1 }
            else high = mid - 1
        }
        if (found !== lyricIndex) {
            lyricIndex = found
            if (found >= 0 && lyricsDialog.visible && !lyricsList.moving) lyricsList.positionViewAtIndex(found, ListView.Center)
        }
    }
    readonly property string audioExtensions: ".mp3,.m4a,.aac,.flac,.ogg,.oga,.opus,.wav,.aiff,.ape,.wma"

    function refresh() {
        entries = systemTools.files(folder).filter(function(e) {
            if (e.directory) return true
            const dot = e.name.lastIndexOf(".")
            return dot >= 0 && audioExtensions.indexOf(e.name.substring(dot).toLowerCase()) >= 0
        }).map(function(e) {
            if (e.directory) return e
            const metadata = systemTools.localMusicMetadata((root.folder ? root.folder + "/" : "") + e.name)
            return Object.assign({}, e, metadata, { relative: (root.folder ? root.folder + "/" : "") + e.name, local: true })
        }).sort(function(a,b) {
            if (a.directory !== b.directory) return a.directory ? -1 : 1
            return a.name.localeCompare(b.name)
        })
    }
    function openMusicFolder() {
        const homeEntries = systemTools.files("")
        const candidates = ["Music", "music", "音乐", "音樂"]
        for (let i = 0; i < candidates.length; ++i) {
            const match = homeEntries.find(function(e) { return e.directory && e.name === candidates[i] })
            if (match) { folder = match.name; break }
        }
        refresh()
    }
    function folderLabel() {
        if (!folder) return I18n.t("离线音乐")
        const last = folder.split("/").pop()
        return ["Music", "music", "音乐", "音樂"].indexOf(last) >= 0 ? I18n.t("本地音乐") : last
    }
    function playEntry(index) {
        if (index < 0 || index >= queue.length) return
        cancelSeek()
        onlineStartupTimer.stop()
        musicService.cancelPlaybackRequest()
        onlineLoading = false; pendingOnlineId = ""; onlinePlayback = false
        audioPlayer.stop()
        queueIndex = index
        const item = queue[index]
        currentTitle = item.title || item.name
        currentArtist = item.artist || ""
        currentAlbum = item.album || ""
        currentGenre = item.genre || ""
        currentYear = item.year || ""
        currentCover = item.coverUrl || ""
        currentRelative = item.relative
        audioPlayer.source = systemTools.fileUrl(item.relative)
        if (audioPlayer.source.toString() !== "") audioPlayer.play()
    }
    function playFromEntry(entry) {
        const files = entries.filter(function(e) { return !e.directory && e.name.toLowerCase().indexOf(root.searchText.toLowerCase()) >= 0 })
        queue = files.slice(0).map(function(e) { return Object.assign({}, e, { "name": e.name, "relative": e.relative || ((root.folder ? root.folder + "/" : "") + e.name) }) })
        const index = queue.findIndex(function(e) { return e.relative === ((root.folder ? root.folder + "/" : "") + entry.name) })
        playEntry(index)
        rebuildPlaybackOrder()
    }
    function playNext(delta) {
        if (queue.length) playEntry((queueIndex + delta + queue.length) % queue.length)
    }
    function showMyPlaylists(scope) {
        clearOnlineGroup()
        playlistScope = scope
        musicService.myPlaylists(scope)
    }
    function openPlaylist(item) {
        playlistBrowserRows = onlineRows.slice(0)
        currentPlaylistId = item.encryptedId
        currentPlaylistName = item.name
        onlineRows = []
        playlistOffset = 0
        musicService.playlist(currentPlaylistId, 0)
    }
    function leavePlaylist() {
        currentPlaylistId = ""
        currentPlaylistName = ""
        onlineRows = playlistBrowserRows.slice(0)
    }
    function playOnlineTrack(track) {
        const now = Date.now()
        if (track.encryptedId === lastPlayRequestId && now - lastPlayRequestAt < 800) return
        onlineQueue = onlineRows.filter(function(row) { return row.type === "song" }).slice(0)
        const id = String(track.originalId || track.id || "")
        onlineQueueIndex = onlineQueue.findIndex(function(row) { return String(row.originalId || row.id || "") === id })
        if (onlineQueueIndex < 0) { onlineQueue = [track]; onlineQueueIndex = 0 }
        startOnlineTrack(track)
        rebuildPlaybackOrder()
    }
    function startOnlineTrack(track) {
        cancelSeek()
        musicService.cancelPlaybackRequest()
        onlineLoading = false
        audioPlayer.stop()
        audioPlayer.source = ""
        pendingOnlineId = String(track.originalId || track.id)
        lastPlayRequestId = track.encryptedId
        lastPlayRequestAt = Date.now()
        currentTitle = track.name; currentArtist = track.artist; currentAlbum = track.album || ""; currentGenre = ""; currentYear = ""; currentCover = track.coverUrl
        onlinePlayback = true; onlinePaused = true; onlineLoading = true; statusText = ""
        cachedOnlineTrackId = ""; onlineSourceUrl = ""
        onlineStartupTimer.restart()
        musicService.resolve(pendingOnlineId)
        lyricIndex = -1
        musicService.fetchLyrics(pendingOnlineId)
    }
    function playOnlineNext(delta, natural) {
        if (!onlineQueue.length || (delta !== 1 && delta !== -1)) return
        // Manual navigation wraps; natural completion stops at the queue end.
        for (let step = 1; step <= onlineQueue.length; ++step) {
            const target = onlineQueueIndex + delta * step
            if (natural && (target < 0 || target >= onlineQueue.length)) return
            const i = (target % onlineQueue.length + onlineQueue.length) % onlineQueue.length
            const track = onlineQueue[i]
            if (track.playable === false || !(track.originalId || track.id)) continue
            onlineQueueIndex = i
            startOnlineTrack(track)
            return
        }
    }
    function indexOnlineRows(rows) {
        return rows.map(function(track, index) {
            const copy = Object.assign({}, track)
            copy.index = index + 1
            return copy
        })
    }
    function clearOnlineGroup() {
        onlineRows = []
        playlistBrowserRows = []
        currentPlaylistId = ""
        currentPlaylistName = ""
    }
    function formatTime(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        return Math.floor(s / 60).toString().padStart(2,"0") + ":" + (s % 60).toString().padStart(2,"0")
    }
    function formatSeconds(seconds) {
        const s = Math.max(0, Math.floor(seconds || 0))
        return Math.floor(s / 60).toString().padStart(2,"0") + ":" + (s % 60).toString().padStart(2,"0")
    }
    function parentFolder() {
        if (!folder) return
        folder = folder.substring(0, Math.max(0, folder.lastIndexOf("/")))
        refresh()
    }

    Component.onCompleted: openMusicFolder()
    onActiveChanged: {
        if (!active) return
        updateContentViewport()
        if (mode === "offline") refresh()
    }
    Connections {
        target: systemTools
        function onMusicNetworkChanged() {
            if (!root.autoNetworkMode) return
            const next = systemTools.musicNetworkOnline ? "online" : "offline"
            if (root.mode !== next) root.mode = next
        }
    }
    MediaPlayer {
        id: audioPlayer
        objectName: "musicMediaPlayer"
        audioOutput: AudioOutput { objectName:"musicAudioOutput"; volume: root.volume * root.announcementGain * (root.cachePrimeMuted ? 0 : 1) }
        onErrorOccurred: function(error, errorString) {
            if (error !== MediaPlayer.NoError) {
                if (root.seekCacheSourceSwap) root.failCachedSeek(I18n.t("缓存音频定位失败，正在恢复在线播放"))
                else root.cancelSeek()
                root.statusText = errorString; root.onlineLoading = false; onlineStartupTimer.stop()
            }
        }
        onPlaybackStateChanged: {
            root.onlinePaused = playbackState !== MediaPlayer.PlayingState
            if (playbackState === MediaPlayer.PlayingState) { root.onlineLoading = false; onlineStartupTimer.stop() }
            if (root.seekCacheSourceSwap && root.cachePrimeRequested && !root.seekCachePositionApplied) {
                if (playbackState === MediaPlayer.PlayingState) audioPlayer.pause()
                else if (playbackState === MediaPlayer.PausedState) root.applyCachedSeekPosition()
            }
        }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia) Qt.callLater(function() { root.navigatePlayback(1, true) })
            if (root.seekCacheSourceSwap && audioPlayer.source.toString().startsWith("file:") && mediaStatus === MediaPlayer.LoadingMedia)
                root.seekCacheSawLoading = true
            root.applyCachedSeekPosition()
        }
        onPositionChanged: root.updateLyricPosition()
        onSourceChanged: if (!root.seekCacheSourceSwap) root.cancelSeek()
    }
    Timer { id: seekCommitTimer; interval: 100; onTriggered: root.commitSeek() }
    Timer { id: seekSettleTimer; interval: 1500; onTriggered: root.seeking = false }
    Timer { id: seekCacheTimer; interval: 50; repeat: true; onTriggered: root.pollCachedSeekPosition() }
    Timer { id: onlineStartupTimer; interval: 20000; onTriggered: { musicService.cancelPlaybackRequest(); audioPlayer.stop(); root.onlineLoading = false; root.statusText = I18n.t("播放超时，请重试") } }
    Timer { interval: 2500; repeat: true; running: loginDialog.visible && musicService.qrUrl !== ""; onTriggered: musicService.checkLogin() }
    Popup {
        anchors.centerIn: parent
        width: Math.min(360 * root.uiScale, root.width - 32)
        visible: root.active && root.onlineLoading
        modal: false
        closePolicy: Popup.NoAutoClose
        padding: 16
        contentItem: ColumnLayout {
            MusicText { text: root.currentTitle; Layout.fillWidth: true; elide: Text.ElideRight; maximumLineCount: 1 }
            ProgressBar { Layout.fillWidth: true; indeterminate: true }
        }
    }
    Connections {
        target: musicService
        function onLyricsChanged() { root.updateLyricPosition() }
        function onRowsChanged() {
            if (root.mode !== "online") return
            root.onlineRows = musicService.rows.map(function(track, index) {
                const copy = Object.assign({}, track)
                copy.type = track.playlist ? "playlist" : "song"
                copy.coverUrl = track.cover
                copy.playable = true
                copy.index = index + 1
                return copy
            })
        }
        function onSourceReady(id, source) {
            if (!root.onlinePlayback || !root.onlineLoading || id !== root.pendingOnlineId) return
            root.onlineSourceUrl = source
            const cached = musicService.cachedAudioForTrack(id)
            if (cached.toString() !== "") {
                root.cachedOnlineTrackId = id
                audioPlayer.source = cached
            } else {
                audioPlayer.source = source
            }
            audioPlayer.play()
        }
        function onSourceFailed(id, message) { if (id !== root.pendingOnlineId) return; root.statusText = message; root.onlinePaused = true; root.onlineLoading = false; onlineStartupTimer.stop() }
        function onSeekAudioCacheProgress(id, received, total) {
            if (!root.seekCachePending || id !== root.seekCacheTrackId || id !== root.pendingOnlineId) return
            root.statusText = total > 0 ? I18n.t("正在缓存跳转音频") + " " + Math.min(99, Math.floor(received * 100 / total)) + "%" : I18n.t("正在缓存跳转音频")
        }
        function onSeekAudioCacheReady(id, localSource) { root.beginCachedSeek(id, localSource) }
        function onSeekAudioCacheFailed(id, message) {
            if (!root.seekCachePending || id !== root.seekCacheTrackId || id !== root.pendingOnlineId) return
            root.seekCachePending = false
            root.seeking = false
            root.seekCacheTrackId = ""
            root.statusText = I18n.t("跳转预缓存失败，保持当前播放：") + message
        }
        function onChanged() {
            if (musicService.error) root.statusText = musicService.error
        }
    }

    Dialog {
        id: queueDialog; objectName: "musicQueueDialog"
        Material.theme:Material.Dark
        Overlay.modal: Rectangle { color: "#b3000000" }
        anchors.centerIn: parent; modal: true
        width: Math.min(620 * root.uiScale, root.width - 16)
        height: root.height - 8
        title: I18n.t("播放队列"); standardButtons: Dialog.NoButton
        header:MusicText {
            text:queueDialog.title;heading:true;leftPadding:20;rightPadding:20;topPadding:16;bottomPadding:16
            background:Rectangle {color:"#202020";radius:16}
        }
        footer: Item { implicitHeight: 52; IconButton { anchors.right: parent.right; iconName: "close"; label: I18n.t("关闭"); onClicked: queueDialog.close() } }
        background: Rectangle { radius: 24; color: "#181818"; border.color: "#383838" }
        contentItem: ListView {
            id: queueList; clip: true; model: root.playingQueue
            implicitWidth: 0; implicitHeight: 0
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: queueList.width; height: Math.max(64, 80 * root.uiScale, Theme.hit)
                Accessible.name: modelData.name
                onClicked: root.playQueueIndex(index)
                background: Rectangle { radius: 8; color: index === root.playingIndex ? "#25362b" : parent.down ? "#383838" : parent.hovered ? "#282828" : "transparent" }
                contentItem: RowLayout {
                    MusicText { text: index + 1; subdued: true; Layout.preferredWidth: 32 }
                                 ColumnLayout {
                                     Layout.fillWidth: true; Layout.minimumWidth:0; Layout.preferredWidth:0; spacing: 2
                        MusicText { text: modelData.name; fontPixelSize: root.trackTextSize; color: index === root.playingIndex ? "#1ed760" : "#ffffff"; Layout.fillWidth: true; elide: Text.ElideRight; maximumLineCount: 1 }
                        MusicText { text: modelData.artist || ""; fontPixelSize: root.secondaryTextSize; subdued: true; Layout.fillWidth: true; elide: Text.ElideRight; maximumLineCount: 1 }
                    }
                    MaterialIcon { name: "volume_up"; visible: index === root.playingIndex; width: 24; height: 24 }
                }
            }
            MusicText { anchors.centerIn: parent; visible: !queueList.count; text: I18n.t("暂无播放队列") }
        }
    }
    Dialog {
        id: lyricsDialog; objectName: "musicLyricsDialog"
        Material.theme:Material.Dark
        Overlay.modal: Rectangle { color: "#b3000000" }
        header: Label {
            text: lyricsDialog.title; color: "#ffffff"; font.pixelSize: root.trackTextSize; font.bold: true
            padding: 16; rightPadding: 2*Theme.hit+8; elide: Text.ElideRight; maximumLineCount: 1; clip: true
            IconButton { objectName:"lyricsTranslationButton";anchors.right: closeLyrics.left; anchors.verticalCenter: parent.verticalCenter; iconName: "language"; label: I18n.t("翻译歌词"); filled: root.showTranslation; accentColor: "#1ed760"; onClicked: root.showTranslation = !root.showTranslation }
            IconButton { id: closeLyrics;objectName:"lyricsCloseButton"; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; iconName: "close"; label: I18n.t("关闭"); onClicked: lyricsDialog.close() }
            background: Rectangle { color: "#202e25"; radius: 16 }
        }
        anchors.centerIn: parent; modal: true
        width: Math.min(900 * root.uiScale, root.width - 16)
        height: root.height - 8
        title: root.currentTitle || I18n.t("歌词"); standardButtons: Dialog.NoButton
        footer: Item { implicitHeight: 0 }
        background: Rectangle {
            color:"#121212"
            radius: 24
            gradient: Gradient { GradientStop { position: 0; color: "#263e30" } GradientStop { position: 1; color: "#121212" } }
        }
        onOpened: root.updateLyricPosition()
        contentItem: Item {
            RowLayout {
                anchors.fill: parent
                spacing: 16 * root.uiScale
                ColumnLayout {
                    Layout.preferredWidth: Math.min(230 * root.uiScale, root.width * 0.3)
                    Layout.maximumWidth: Math.min(230 * root.uiScale, root.width * 0.3)
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0; Layout.minimumHeight: 0
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(144 * root.uiScale, root.height * 0.3)
                        radius: 12; color: "#283a30"
                        Image { id: lyricsCover; anchors.fill: parent; source: root.currentCover; asynchronous: true; sourceSize: Qt.size(256, 256); fillMode: Image.PreserveAspectFit }
                        MaterialIcon { anchors.centerIn: parent; name: "volume_up"; width: 48; height: width; visible: lyricsCover.status !== Image.Ready }
                    }
                    MusicText {
                        objectName:"lyricsTrackMetadata"
                        text: root.currentArtist + (root.currentAlbum ? "\n" + root.currentAlbum : "") + (root.currentYear ? " · " + root.currentYear : "")
                        subdued: true; Layout.fillWidth: true; Layout.minimumWidth:0
                        Layout.maximumHeight:Math.ceil(root.secondaryTextSize*1.35)*3
                        maximumLineCount:3;elide:Text.ElideRight;wrapMode:Text.WrapAnywhere;clip:true
                    }
                    Item { Layout.fillHeight: true; Layout.minimumHeight: 0 }
                    RowLayout {
                        objectName:"lyricsPlaybackControls"
                        Layout.alignment: Qt.AlignHCenter
                        IconButton { iconName: "arrow_back"; label: I18n.t("上一首"); onClicked: root.navigatePlayback(-1, false) }
                        IconButton { iconName: audioPlayer.playbackState === MediaPlayer.PlayingState ? "pause" : "play_arrow"; filled: true; accentColor: "#1ed760"; label: I18n.t("播放/暂停"); enabled: !root.onlineLoading && !root.seekCacheSourceSwap && audioPlayer.source.toString() !== ""; onClicked: audioPlayer.playbackState === MediaPlayer.PlayingState ? audioPlayer.pause() : audioPlayer.play() }
                        IconButton { iconName: "arrow_forward"; label: I18n.t("下一首"); onClicked: root.navigatePlayback(1, false) }
                    }
                }
                ListView {
                    id: lyricsList; objectName: "musicLyricsList"
                    implicitWidth: 0; implicitHeight: 0
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0; Layout.minimumHeight: 0; clip: true
                    model: root.onlinePlayback ? root.lyricRows : []; spacing: 12
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        objectName: "musicLyricLine"
                        width: lyricsList.width
                        implicitHeight: Math.max(Theme.hit, lyricContent.implicitHeight + 16)
                        Accessible.name: modelData.text
                        onClicked: root.requestSeek(Number(modelData.time))
                        background: Rectangle { radius: 8; color: parent.down ? "#38483d" : "transparent" }
                        contentItem: ColumnLayout {
                            id: lyricContent; spacing: 4
                            MusicText { text: modelData.text || "♪"; font.pointSize: Theme.body; font.bold: true; color: index === root.lyricIndex ? "#1ed760" : "#b3b3b3"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            MusicText { text: modelData.translation; visible: root.showTranslation && text !== ""; subdued: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        }
                    }
                    MusicText { anchors.centerIn: parent; visible: !lyricsList.count; text: musicService.lyricsBusy && root.onlinePlayback ? I18n.t("正在加载歌词") : I18n.t("暂无歌词") }
                }
            }
        }
    }
    Dialog {
        id: apiEndpointDialog; anchors.centerIn: parent; modal: true
        Material.theme:Material.Dark
        width: Math.min(460 * root.uiScale, Math.max(240, root.width - 24))
        title: I18n.t("配置 API"); standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: apiEndpointField.text = musicService.endpoint
        onAccepted: musicService.setEndpoint(apiEndpointField.text)
        contentItem: TextField { id: apiEndpointField; selectByMouse: true }
    }
    Dialog {
        id: loginDialog; anchors.centerIn: parent; modal: true
        Material.theme:Material.Dark
        width: Math.min(340 * root.uiScale, Math.max(240, root.width - 24))
        title: I18n.t("扫码登录"); standardButtons: Dialog.Close
        contentItem: Column {
            spacing: 5
            Image { source: musicService.qrUrl; width:190; height:190; anchors.horizontalCenter:parent.horizontalCenter }
            MusicText { text: musicService.account ? I18n.t("已登录") : musicService.error; width:parent.width; wrapMode:Text.Wrap }
        }
    }
    ColumnLayout {
        id: playerLayout
        x:0;y:0;width:root.contentViewport.width;height:root.contentViewport.height
        spacing: root.compact ? 4 * root.uiScale : 0

        // Spotify-inspired library navigation and content surface.
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: root.compact ? 4 * root.uiScale : Theme.gap / 2

            Rectangle {
                id: musicSidebar
                Layout.preferredWidth: Math.min((root.compact ? 240 : 300) * root.uiScale, playerLayout.width * (root.compact ? 0.26 : 0.28))
                Layout.fillHeight: true
                radius: Theme.shapeLarge
                color: "#121212"
                Column {
                    id: sidebarContents
                    x: root.compact ? Theme.gap / 2 : Theme.gap
                    y: root.compact ? Theme.gap / 2 : Theme.gap
                    width: Math.max(0, musicSidebar.width - 2 * (root.compact ? Theme.gap / 2 : Theme.gap))
                    spacing: root.compact ? 4 * root.uiScale : Theme.gap / 2
                    RowLayout {
                        width: sidebarContents.width
                        MaterialIcon { visible: !root.compact; name: "apps"; width: 24 * root.uiScale; height: width }
                        MusicText { text: root.compact ? I18n.t("资料库") : I18n.t("你的资料库"); heading: true; Layout.fillWidth: true }
                        IconButton { iconName: "refresh"; label: I18n.t("刷新"); onClicked: root.refresh() }
                    }
                    RowLayout {
                        visible: true
                        width: sidebarContents.width
                        spacing: root.compact ? 4 * root.uiScale : Theme.gap / 3
                        Repeater {
                            model: [{id:"offline", title:I18n.t("离线")}, {id:"online", title:I18n.t("在线")}]
                            delegate: Button {
                                Layout.fillWidth: true
                                text: modelData.title
                                font.pointSize: Theme.body
                                onClicked: { root.autoNetworkMode = false; root.mode = modelData.id }
                                implicitHeight: 48 * root.uiScale
                                Accessible.name: text
                                background: Rectangle { radius: Theme.shapeFull; color: parent.down ? "#3a3a3a" : root.mode === modelData.id ? "#1ed760" : "#282828"; border.width: parent.activeFocus ? 2 : 0; border.color: "#ffffff" }
                                contentItem: Text { text: parent.text; color: root.mode === modelData.id ? "#07120b" : Theme.foreground; font: parent.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                        }
                    }
                    Loader {
                        active: root.mode === "offline" && !root.compact
                        visible: active
                        width: sidebarContents.width
                        height: 78 * root.uiScale
                        sourceComponent: Rectangle {
                        radius: Theme.shapeMedium
                        color: "#1c292f"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: Theme.gap / 2; spacing: Theme.gap / 2
                            Rectangle {
                                Layout.preferredWidth: 48 * root.uiScale; Layout.preferredHeight: 48 * root.uiScale
                                radius: Theme.shapeSmall; color: "#405c50"
                                MaterialIcon { anchors.centerIn: parent; width: 27 * root.uiScale; height: width; name: "description" }
                            }
                                         ColumnLayout {
                                             Layout.fillWidth: true; Layout.minimumWidth:0; Layout.preferredWidth:0; spacing: 2
                                MusicText { text: I18n.t("本地音乐"); heading: true; Layout.fillWidth: true }
                            }
                            MaterialIcon { name: "chevron_right"; width: 24 * root.uiScale; height: width }
                        }
                        TapHandler { onTapped: root.mode = "offline" }
                        }
                    }
                    Rectangle { width: sidebarContents.width; height: 1; color: "#29353b" }
                    MusicText { visible: !root.compact; text: root.mode === "offline" ? I18n.t("文件夹") : I18n.t("我的歌单"); subdued: true; width: sidebarContents.width }
                    Button {
                        width: sidebarContents.width
                        visible: root.mode === "offline" && root.folder !== ""
                        implicitHeight: Theme.hit * 0.8
                        text: "‹  " + I18n.t("上一级")
                        onClicked: root.parentFolder()
                        background: Rectangle { radius: Theme.shapeMedium; color: parent.down ? Theme.pressed : "transparent" }
                    }
                    CarButton { visible: root.mode === "online"; text: I18n.t("我创建的歌单"); buttonColor: down ? "#383838" : hovered ? "#282828" : "#181818"; textColor: enabled ? "#ffffff" : "#b3b3b3"; width: sidebarContents.width; enabled: !!musicService.account && !musicService.busy; onClicked: root.showMyPlaylists("created") }
                    CarButton { visible: root.mode === "online"; text: I18n.t("我收藏的歌单"); buttonColor: down ? "#383838" : hovered ? "#282828" : "#181818"; textColor: enabled ? "#ffffff" : "#b3b3b3"; width: sidebarContents.width; enabled: !!musicService.account && !musicService.busy; onClicked: root.showMyPlaylists("collected") }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.shapeLarge
                color: "#121212"
                clip: true

                Column {
                    id: libraryContents
                    anchors.fill:parent
                    spacing: 0
                    Rectangle {
                        width: libraryContents.width
                        implicitHeight: (root.compact ? 64 : 88) * root.uiScale
                        color:"#18231d"
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#253a30" }
                            GradientStop { position: 1; color: "#18231d" }
                        }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: root.compact ? Theme.gap / 2 : Theme.gap; anchors.rightMargin: root.compact ? Theme.gap / 2 : Theme.gap; spacing: root.compact ? 4 : Theme.gap / 2
                            IconButton { iconName: "arrow_back"; label: root.currentPlaylistId ? I18n.t("返回歌单") : I18n.t("返回"); onClicked: root.currentPlaylistId ? root.leavePlaylist() : root.parentFolder(); visible: (root.mode === "offline" && root.folder !== "") || root.currentPlaylistId !== "" }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                MusicText { text: root.mode === "offline" ? root.folderLabel() : (root.currentPlaylistName || I18n.t("网易云音乐")); hero: !root.compact; heading: root.compact; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
                            }
                            TextField {
                                visible: root.mode === "offline"
                                Layout.preferredWidth: Math.min((root.compact ? 150 : 300) * root.uiScale, root.width * 0.25)
                                Layout.preferredHeight: root.compact ? 48 * root.uiScale : implicitHeight
                                placeholderText: root.compact ? I18n.t("搜索") : I18n.t("搜索本机歌曲")
                                onTextChanged: root.searchText = text
                                color:"#ffffff";placeholderTextColor:"#b3b3b3";selectionColor:"#1ed760";selectedTextColor:"#07120b"
                                leftPadding: 16 * root.uiScale
                                Accessible.name: placeholderText
                                background: Rectangle { radius: Theme.shapeFull; color: "#242424"; border.width: parent.activeFocus ? 2 : 0; border.color: "#ffffff" }
                            }
                            TextField {
                                visible: root.mode === "online" && root.currentPlaylistId === ""
                                 enabled: !musicService.busy
                                Layout.preferredWidth: Math.min((root.compact ? 180 : 220) * root.uiScale, root.width * 0.25)
                                Layout.preferredHeight: root.compact ? 48 * root.uiScale : implicitHeight
                                placeholderText: root.compact ? I18n.t("搜索") : I18n.t("搜索网易云歌曲")
                                color:"#ffffff";placeholderTextColor:"#b3b3b3";selectionColor:"#1ed760";selectedTextColor:"#07120b"
                                onAccepted: {
                                    root.currentPlaylistId = ""; root.currentPlaylistName = ""; root.playlistBrowserRows = []
                                     musicService.search(text, false)
                                }
                                leftPadding: 16 * root.uiScale
                                Accessible.name: placeholderText
                                background: Rectangle { radius: Theme.shapeFull; color: "#242424"; border.width: parent.activeFocus ? 2 : 0; border.color: "#ffffff" }
                            }
                             IconButton { visible: root.mode === "online" && !!musicService.account && !root.currentPlaylistId; iconName: "history"; label: I18n.t("每日推荐"); onClicked: musicService.recommendations() }
                             IconButton { visible: root.mode === "online" && !!musicService.account; enabled: !musicService.busy; iconName: "link_off"; label: I18n.t("退出帐号"); onClicked: { musicService.logout(); audioPlayer.stop(); audioPlayer.source = ""; root.onlineLoading = false; onlineStartupTimer.stop(); root.pendingOnlineId = ""; root.onlineQueue = []; root.onlineQueueIndex = -1; root.currentTitle = ""; root.currentArtist = ""; root.currentCover = ""; root.currentPlaylistId = "" } }
                        }
                    }
                    Item {
                        x: root.compact ? Theme.gap / 2 : Theme.gap
                        width: Math.max(0,libraryContents.width-x*2)
                        height: Math.max(tableHeading.implicitHeight,20*root.uiScale) + (root.compact ? 4 : Theme.gap)
                        MusicText { text: "#"; subdued: true; width:32*root.uiScale; anchors.verticalCenter:parent.verticalCenter; horizontalAlignment:Text.AlignHCenter }
                        MusicText { id:tableHeading; text:I18n.t("标题"); subdued:true; x:32*root.uiScale+5; anchors.verticalCenter:parent.verticalCenter }
                        MaterialIcon { name:"history"; anchors.right:parent.right; anchors.rightMargin:12*root.uiScale; anchors.verticalCenter:parent.verticalCenter; width:20*root.uiScale; height:width }
                    }
                    Rectangle { x:Theme.gap; width:Math.max(0,libraryContents.width-Theme.gap*2); height: 1; color: "#29353b" }
                    ListView {
                        id: trackList
                        objectName: "musicFileList"
                        visible: root.mode === "offline"
                        width:libraryContents.width; height:Math.max(0,libraryContents.height-y)
                        clip: true
                        model: root.mode === "offline" ? root.entries.filter(function(e) { return e.directory || e.name.toLowerCase().indexOf(root.searchText.toLowerCase()) >= 0 }) : []
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: trackList.width
                            height: Math.max(64, 80 * root.uiScale, Theme.hit)
                            onClicked: {
                                if (modelData.directory) {
                                    root.folder = (root.folder ? root.folder + "/" : "") + modelData.name
                                    root.searchText = ""; root.refresh()
                                } else root.playFromEntry(modelData)
                            }
                            Accessible.name: modelData.name
                            background: Rectangle { color: parent.down ? "#383838" : currentRelative === ((root.folder ? root.folder + "/" : "") + modelData.name) ? "#25362b" : parent.hovered ? "#282828" : "transparent"; radius: 8 * root.uiScale; border.width: parent.activeFocus ? 2 : 0; border.color: "#1ed760" }
                            contentItem: Item {
                                id: localTrackContent
                                MusicText { text: index + 1; subdued: true; width:32*root.uiScale; anchors.verticalCenter:parent.verticalCenter; horizontalAlignment:Text.AlignHCenter }
                                Rectangle {
                                    id: localTrackCover
                                    x:32*root.uiScale+Theme.gap/2; anchors.verticalCenter:parent.verticalCenter
                                    width:48*root.uiScale; height:width
                                    radius: Theme.shapeSmall
                                     color: modelData.directory ? "#403d2a" : "#253b31"
                                     clip: true
                                     Image { id: localCoverArt; anchors.fill: parent; source: modelData.coverUrl || ""; asynchronous: true; sourceSize: Qt.size(96, 96); fillMode: Image.PreserveAspectCrop; visible: !modelData.directory && status === Image.Ready }
                                     MaterialIcon { anchors.centerIn: parent; width: 25 * root.uiScale; height: width; name: modelData.directory ? "folder" : "play_arrow"; visible: !localCoverArt.visible }
                                }
                                Column {
                                    x:localTrackCover.x+localTrackCover.width+Theme.gap/2
                                    width:Math.max(0,localTrackContent.width-x-(modelData.directory?0:25*root.uiScale+Theme.gap/2))
                                    anchors.verticalCenter:parent.verticalCenter;spacing:2
                                      MusicText { text: modelData.directory ? modelData.name : (modelData.title || modelData.name); fontPixelSize: root.trackTextSize; color: currentRelative === ((root.folder ? root.folder + "/" : "") + modelData.name) ? "#1ed760" : "#ffffff"; heading: true; width:parent.width; maximumLineCount: 1; elide: Text.ElideMiddle }
                                      MusicText { text: modelData.directory ? I18n.t("文件夹") : ((modelData.artist || "") + (modelData.album ? " · " + modelData.album : "")); fontPixelSize: root.secondaryTextSize; subdued: true; width:parent.width; maximumLineCount: 1; elide: Text.ElideRight }
                                }
                                MaterialIcon { name: "play_arrow"; anchors.right:parent.right; anchors.verticalCenter:parent.verticalCenter; width: 25 * root.uiScale; height: width; visible: !modelData.directory }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: root.mode === "offline" && trackList.count === 0
                            text: I18n.t("此文件夹没有音频文件")
                            color: Theme.disabledText
                            font.pointSize: Theme.body
                        }
                    }
                    Rectangle {
                        visible: root.mode === "online"
                        width:libraryContents.width; height:Math.max(0,libraryContents.height-y)
                        color: "#121212"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: root.compact ? Theme.gap / 2 : Theme.gap
                            spacing: root.compact ? 4 : Theme.gap / 2
                            RowLayout {
                                Layout.fillWidth: true
                                 visible: !musicService.account
                                 CarButton { text: I18n.t("配置 API"); buttonColor: down ? "#383838" : "#242424"; textColor: "#ffffff"; Layout.fillWidth: true; onClicked: apiEndpointDialog.open() }
                                 CarButton { text: I18n.t("扫码登录"); buttonColor: down ? "#16b84d" : "#1ed760"; textColor: "#07120b"; enabled: !musicService.busy; Layout.fillWidth: true; onClicked: { musicService.login(); loginDialog.open() } }
                            }
                             BusyIndicator { running: musicService.busy; visible: running; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 36 * root.uiScale; Layout.preferredHeight: 36 * root.uiScale }
                             MusicText { text: root.statusText || musicService.error; visible: text !== ""; color: Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        visible: root.currentPlaylistId !== ""
                        Layout.fillWidth: true
                                IconButton { iconName: "arrow_back"; label: I18n.t("前 50 首"); enabled: root.playlistOffset > 0 && !musicService.busy; onClicked: { root.playlistOffset = Math.max(0, root.playlistOffset - 50); musicService.playlist(root.currentPlaylistId, root.playlistOffset) } }
                                MusicText { text: (root.playlistOffset + 1) + "–" + (root.playlistOffset + root.onlineRows.length); subdued: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                                IconButton { iconName: "arrow_forward"; label: I18n.t("后 50 首"); enabled: root.onlineRows.length === 50 && !musicService.busy; onClicked: { root.playlistOffset += 50; musicService.playlist(root.currentPlaylistId, root.playlistOffset) } }
                            }
                            ListView {
                                objectName: "musicResultList"
                                Layout.fillWidth: true; Layout.fillHeight: true
                                clip: true
                                model: root.onlineRows
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: ItemDelegate {
                                    required property var modelData
                                    width: ListView.view ? ListView.view.width : 0
                                    height: Math.max(64, 80 * root.uiScale, Theme.hit)
                                    enabled: Boolean(modelData.encryptedId && modelData.originalId)
                                    onClicked: {
                                        if (modelData.type === "playlist") { root.openPlaylist(modelData); return }
                                        root.playOnlineTrack(modelData)
                                    }
                                    Accessible.name: modelData.name + " " + modelData.artist
                                    background: Rectangle { radius: 8 * root.uiScale; color: parent.down ? "#383838" : root.pendingOnlineId === String(modelData.originalId) ? "#25362b" : parent.hovered ? "#282828" : "transparent"; border.width: parent.activeFocus ? 2 : 0; border.color: "#1ed760" }
                                    contentItem: RowLayout {
                                        spacing: Theme.gap / 2
                                        MusicText { text: modelData.index || ""; color: root.pendingOnlineId === String(modelData.originalId) ? "#1ed760" : "#b3b3b3"; Layout.preferredWidth: 32 * root.uiScale; horizontalAlignment: Text.AlignHCenter }
                                        Rectangle {
                                            Layout.preferredWidth: (root.compact ? 40 : 46) * root.uiScale; Layout.preferredHeight: (root.compact ? 40 : 46) * root.uiScale
                                            radius: Theme.shapeSmall; color: "#263a30"
                                            clip: true
                                            Image { id: coverArt; anchors.fill: parent; source: modelData.coverUrl ? modelData.coverUrl.replace(/^http:/, "https:") : ""; sourceSize: Qt.size(96, 96); asynchronous: true; fillMode: Image.PreserveAspectCrop; visible: status === Image.Ready }
                                            MaterialIcon { anchors.centerIn: parent; width: 25 * root.uiScale; height: width; name: modelData.type === "playlist" ? "view_list" : modelData.playable ? "play_arrow" : "close"; visible: !coverArt.visible }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 2
                                             MusicText { text: modelData.name; fontPixelSize: root.trackTextSize; color: root.pendingOnlineId === String(modelData.originalId) ? "#1ed760" : "#ffffff"; heading: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
                                             MusicText { text: modelData.artist + (modelData.album ? " · " + modelData.album : ""); fontPixelSize: root.secondaryTextSize; subdued: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
                                        }
                                        MusicText { text: modelData.type === "song" ? root.formatTime(modelData.duration) : ""; subdued: true }
                                    }
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: root.onlineRows.length === 0 && !musicService.busy && musicService.error === ""
                                    text: I18n.t("搜索网易云歌曲"); color: Theme.disabledText; font.pointSize: Theme.body
                                }
                            }
                        }
                    }
                }
            }
        }

        // Persistent now-playing bar, with transport controls centered.
        Rectangle {
            Layout.fillWidth: true
            // The two-row transport (buttons + progress/seek) must remain inside the bar on 600px-tall screens.
            implicitHeight: (root.compact ? 120 : 116) * root.uiScale
            color: "#000000"
            Item {
                id: playbackBarContents
                anchors.fill: parent
                anchors.leftMargin: root.compact ? Theme.gap / 2 : Theme.gap; anchors.rightMargin: root.compact ? Theme.gap / 2 : Theme.gap
                readonly property real spacing: root.compact ? 4 * root.uiScale : Theme.gap / 2
                RowLayout {
                    id: nowPlayingInfo
                    anchors.left:parent.left;anchors.verticalCenter:parent.verticalCenter
                    width:Math.max((root.compact ? 120 : 180) * root.uiScale,playerLayout.width * (root.compact ? 0.24 : 0.25))
                    spacing: root.compact ? 4 * root.uiScale : Theme.gap / 2
                    Rectangle {
                        Layout.preferredWidth: (root.compact ? 44 : 58) * root.uiScale; Layout.preferredHeight: (root.compact ? 44 : 58) * root.uiScale
                        radius: Theme.shapeSmall; color: "#263a30"
                        clip: true
                         Image { id: currentCoverArt; anchors.fill: parent; source: root.currentCover ? root.currentCover.replace(/^http:/, "https:") : ""; asynchronous: true; sourceSize: Qt.size(128, 128); fillMode: Image.PreserveAspectCrop; visible: status === Image.Ready }
                        MaterialIcon { anchors.centerIn: parent; width: 30 * root.uiScale; height: width; name: "play_arrow"; visible: !currentCoverArt.visible }
                    }
                      ColumnLayout {
                          Layout.fillWidth: true; Layout.minimumWidth:0; Layout.preferredWidth:0; Layout.alignment: Qt.AlignVCenter; spacing: 0
                         Button {
                             Layout.fillWidth: true; implicitHeight: Math.max(32 * root.uiScale, titleLabel.implicitHeight); leftPadding: 0; rightPadding: 0
                             enabled: root.currentTitle !== ""; Accessible.name: root.currentTitle + " " + I18n.t("歌词")
                             onClicked: lyricsDialog.open()
                             background: Rectangle { radius: 8; color: parent.down || parent.hovered ? "#282828" : "transparent"; border.width: parent.activeFocus ? 2 : 0; border.color: "#1ed760" }
                              contentItem: MusicText { id: titleLabel; objectName: "musicNowPlayingTitle"; text: root.currentTitle || I18n.t("尚未播放歌曲"); fontPixelSize: root.trackTextSize; heading: true; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter; maximumLineCount: 1; elide: Text.ElideRight }
                         }
                          MusicText { objectName: "musicNowPlayingArtist"; text: root.currentArtist + (root.currentAlbum ? " · " + root.currentAlbum : ""); subdued: true; Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; maximumLineCount: 1; elide: Text.ElideRight }
                    }
                }
                ColumnLayout {
                    anchors.left:nowPlayingInfo.right;anchors.right:soundControls.left
                    anchors.leftMargin:playbackBarContents.spacing;anchors.rightMargin:playbackBarContents.spacing
                    anchors.verticalCenter:parent.verticalCenter;spacing:0
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        IconButton { iconName: "arrow_back"; label: I18n.t("上一首"); enabled: root.playingQueue.length > 0; onClicked: root.navigatePlayback(-1, false) }
                         IconButton { iconName: audioPlayer.playbackState === MediaPlayer.PlayingState ? "pause" : "play_arrow"; label: root.onlinePaused ? I18n.t("继续播放") : I18n.t("暂停"); filled: true; accentColor: "#1ed760"; enabled: !root.onlineLoading && !root.seekCacheSourceSwap && audioPlayer.source.toString() !== ""; onClicked: audioPlayer.playbackState === MediaPlayer.PlayingState ? audioPlayer.pause() : audioPlayer.play() }
                        IconButton { iconName: "arrow_forward"; label: I18n.t("下一首"); enabled: root.playingQueue.length > 0; onClicked: root.navigatePlayback(1, false) }
                    }
                    ProgressBar {
                        visible: root.onlineLoading
                        Layout.fillWidth: true
                        from: 0; to: 100
                        indeterminate: true
                    }
                    RowLayout {
                        spacing: Theme.gap / 2
                        MusicText { text: root.formatTime(audioPlayer.position); subdued: true }
                        Slider {
                             id: playbackSeek; objectName: "musicSeekSlider"; Layout.fillWidth: true
                             from: 0; to: Math.max(1, audioPlayer.duration)
                             Binding { target: playbackSeek; property: "value"; value: root.seeking ? root.seekPreviewPosition : audioPlayer.position; when: !playbackSeek.pressed; restoreMode: Binding.RestoreNone }
                             enabled: audioPlayer.seekable && !root.onlineLoading && !root.seekCacheSourceSwap && audioPlayer.duration > 0
                               onMoved: if (pressed) root.previewSeek(value); else root.requestSeek(value)
                               onPressedChanged: {
                                   if (pressed) { seekCommitTimer.stop(); seekSettleTimer.stop() }
                                  else if (root.seeking) root.commitSeek()
                              }
                        }
                        MusicText { text: root.formatTime(audioPlayer.duration); subdued: true }
                    }
                }
                Row {
                    id: soundControls
                    anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter
                    spacing:5
                    width:Math.max((root.compact ? 160 : 200) * root.uiScale,queueButton.width+volumeIcon.width+musicVolumeSlider.width+spacing*3)
                    IconButton { id:queueButton; anchors.verticalCenter:parent.verticalCenter; iconName: "view_list"; label: I18n.t("播放队列"); onClicked: queueDialog.open() }
                    Item { width:Math.max(0,soundControls.width-queueButton.width-volumeIcon.width-musicVolumeSlider.width-soundControls.spacing*3);height:1 }
                    MaterialIcon { id:volumeIcon; anchors.verticalCenter:parent.verticalCenter; name: "volume_up"; width: 24 * root.uiScale; height: width }
                    Slider {
                        id:musicVolumeSlider; anchors.verticalCenter:parent.verticalCenter
                        width: (root.compact ? 72 : 120) * root.uiScale; from: 0; to: 1
                        value: root.volume
                        enabled: true
                        onMoved: {
                            root.volume = value
                        }
                    }
                }
            }
        }
    }
}
