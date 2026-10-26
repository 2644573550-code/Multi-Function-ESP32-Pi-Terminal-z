import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import "TimeMath.js" as TimeMath

// 闹钟引擎：活在时间页之外（在 Main.qml 实例化一次），页面被销毁也照常计时。
//
// 边界：
// - 不碰渲染/模糊/连接/媒体管线；只新增一个 MediaPlayer 作为铃声音源。
// - 提示复用宿主的 notify() 通知横幅与 announce() 语音播报（既有的降音链路）。
// - 闹钟数据只通过 SystemTools 的 "time/" 前缀设置读写持久化。
Item {
    id: engine

    signal notifyRequested(string title, string message, bool important)
    signal announceRequested(string text)

    readonly property int maxAlarms: 8
    readonly property string defaultRingtone: "qrc:/ringtones/default-ringtone.mp3"
    property alias alarms: alarmModel
    property int enabledCount: 0
    readonly property bool hasEnabledAlarms: enabledCount > 0

    // 响铃状态（页面/状态坞只需要读）
    property bool ringing: false
    property string ringingName: ""
    property string ringingTime: ""
    property int ringingSnoozeMinutes: 5
    property double ringEndsAtMs: 0

    // 计时器也活在这里：时间页关掉后仍然到点提示。
    property bool timerRunning: false
    property bool timerFinished: false
    property double timerRemainingMs: 0
    property double timerTotalMs: 0
    property double timerEndsAtMs: 0

    property var ringAlarm: null
    property var snoozedAlarm: null
    property double snoozeAtMs: 0
    property int lastMinuteEpoch: -1
    property bool previewing: false

    ListModel { id: alarmModel }

    MediaPlayer {
        id: ringPlayer
        audioOutput: AudioOutput { id: ringOutput; volume: 1.0 }
        loops: MediaPlayer.Infinite
        onPlaybackStateChanged: if (engine.previewing && playbackState === MediaPlayer.StoppedState) engine.previewing = false
        onErrorOccurred: function(error, message) {
            if (error === MediaPlayer.NoError) return
            engine.notifyRequested(I18n.t("闹钟"), I18n.t("铃声无法播放：") + message, false)
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: engine.tick()
    }

    Component.onCompleted: engine.load()

    // ------------------------------------------------------------ 持久化

    function settingRead(key, fallback) {
        if (typeof systemTools === "undefined" || !systemTools ||
            typeof systemTools.settingsRead !== "function") return fallback
        return systemTools.settingsRead(key, fallback)
    }

    function settingWrite(key, value) {
        if (typeof systemTools === "undefined" || !systemTools ||
            typeof systemTools.settingsWrite !== "function") return false
        return systemTools.settingsWrite(key, value)
    }

    function normalize(record) {
        return {
            hour: TimeMath.clamp(Number(record.hour) || 0, 0, 23),
            minute: TimeMath.clamp(Number(record.minute) || 0, 0, 59),
            repeatDaily: record.repeatDaily !== false,
            alarmEnabled: record.alarmEnabled !== false,
            name: String(record.name || "").slice(0, 32),
            ringtone: String(record.ringtone || defaultRingtone),
            ringSeconds: TimeMath.clamp(Number(record.ringSeconds) || 30, 5, 120),
            snoozeMinutes: TimeMath.clamp(Number(record.snoozeMinutes) || 5, 1, 30)
        }
    }

    function load() {
        alarmModel.clear()
        let list = []
        try {
            list = JSON.parse(settingRead("time/alarms", "[]"))
        } catch (error) {
            console.warn("alarm engine: unreadable alarm list")
        }
        if (!Array.isArray(list)) list = []
        for (let i = 0; i < list.length && i < maxAlarms; ++i)
            alarmModel.append(normalize(list[i]))
        recount()
    }

    function persist() {
        const list = []
        for (let i = 0; i < alarmModel.count; ++i) {
            const item = alarmModel.get(i)
            list.push({
                hour: item.hour, minute: item.minute,
                repeatDaily: item.repeatDaily, alarmEnabled: item.alarmEnabled,
                name: item.name, ringtone: item.ringtone,
                ringSeconds: item.ringSeconds, snoozeMinutes: item.snoozeMinutes
            })
        }
        settingWrite("time/alarms", JSON.stringify(list))
    }

    function recount() {
        let enabled = 0
        for (let i = 0; i < alarmModel.count; ++i)
            if (alarmModel.get(i).alarmEnabled) ++enabled
        enabledCount = enabled
    }

    // ------------------------------------------------------------ 增删改

    function addAlarm(record) {
        if (alarmModel.count >= maxAlarms) return false
        const item = normalize(record)
        for (let i = 0; i < alarmModel.count; ++i) {
            const existing = alarmModel.get(i)
            if (existing.hour === item.hour && existing.minute === item.minute &&
                existing.name === item.name) return false
        }
        alarmModel.append(item)
        persist(); recount()
        return true
    }

    function setEnabled(index, enabled) {
        if (index < 0 || index >= alarmModel.count) return
        alarmModel.setProperty(index, "alarmEnabled", enabled)
        persist(); recount()
        if (!enabled && ringing && ringAlarm && ringAlarm.hour === alarmModel.get(index).hour &&
            ringAlarm.minute === alarmModel.get(index).minute) stopRing()
    }

    function removeAlarm(index) {
        if (index < 0 || index >= alarmModel.count) return
        alarmModel.remove(index)
        persist(); recount()
    }

    function alarmTimeLabel(alarm) {
        return TimeMath.alarmTimeLabel(alarm)
    }

    // ------------------------------------------------------------ 响铃

    function tick() {
        const now = Date.now()
        const parts = TimeMath.localParts(now)
        const minuteEpoch = Math.floor(now / 60000)
        if (minuteEpoch !== lastMinuteEpoch) {
            lastMinuteEpoch = minuteEpoch
            if (!ringing) checkDue(parts, now)
        }
        if (ringing && now >= ringEndsAtMs) stopRing()
        if (timerRunning && now >= timerEndsAtMs) finishTimer()
        else if (timerRunning && timerRemainingMs !== timerEndsAtMs - now) timerRemainingMs = Math.max(0, timerEndsAtMs - now)
        if (snoozedAlarm && now >= snoozeAtMs) {
            const alarm = snoozedAlarm
            snoozedAlarm = null
            startRing(alarm)
        }
    }

    function checkDue(parts, now) {
        for (let i = 0; i < alarmModel.count; ++i) {
            const alarm = alarmModel.get(i)
            if (!alarm.alarmEnabled) continue
            if (!TimeMath.alarmDue(parts, alarm, -1)) continue
            startRing(alarm)
            if (!alarm.repeatDaily) setEnabled(i, false)
            return
        }
    }

    function startRing(alarm) {
        if (!alarm) return
        stopPreview()
        ringPlayer.loops = MediaPlayer.Infinite
        ringing = true
        ringAlarm = alarm
        ringingName = alarm.name !== "" ? alarm.name : I18n.t("闹钟")
        ringingTime = TimeMath.alarmTimeLabel(alarm)
        ringingSnoozeMinutes = alarm.snoozeMinutes
        ringEndsAtMs = Date.now() + alarm.ringSeconds * 1000
        playRingtone(alarm.ringtone)
        notifyRequested(I18n.t("闹钟"), ringingName + " · " + ringingTime, true)
        announceRequested(I18n.t("闹钟时间到了"))
        if (Overlay.overlay) ringDialog.open()
    }

    function playRingtone(path) {
        if (!path) return
        // 内置铃声走 qrc，文件铃声走宿主的文件沙箱解析。
        if (path.indexOf("qrc:/") === 0) {
            ringPlayer.source = path
            ringPlayer.play()
            return
        }
        if (typeof systemTools === "undefined" || !systemTools ||
            typeof systemTools.fileUrl !== "function") return
        const url = systemTools.fileUrl(path)
        if (!url || url.toString() === "") return
        ringPlayer.source = url
        ringPlayer.play()
    }

    function ringtoneLabel(path) {
        if (!path) return I18n.t("未选择（仅语音播报）")
        if (path === defaultRingtone) return I18n.t("默认铃声（15 秒）")
        const cut = path.lastIndexOf("/")
        return cut >= 0 ? path.slice(cut + 1) : path
    }

    // ------------------------------------------------------------ 计时器

    function startTimer(total) {
        if (!(total > 0)) return
        timerTotalMs = total
        timerRemainingMs = (timerRemainingMs > 0 && timerRemainingMs < total) ? timerRemainingMs : total
        timerEndsAtMs = Date.now() + timerRemainingMs
        timerFinished = false
        timerRunning = true
    }

    function pauseTimer() {
        if (!timerRunning) return
        timerRemainingMs = Math.max(0, timerEndsAtMs - Date.now())
        timerRunning = false
    }

    function resetTimer(total) {
        timerRunning = false
        timerFinished = false
        timerTotalMs = total > 0 ? total : timerTotalMs
        timerRemainingMs = timerTotalMs
    }

    function finishTimer() {
        timerRunning = false
        timerFinished = true
        timerRemainingMs = 0
        notifyRequested(I18n.t("计时器"), I18n.t("计时结束"), true)
        announceRequested(I18n.t("计时结束"))
    }

    function stopAudio() {
        ringPlayer.stop()
        ringPlayer.source = ""
    }

    // 试听：铃声行里的播放按钮，默认铃声与自选文件都走同一条路径。
    function togglePreview(path) {
        if (previewing) { stopPreview(); return }
        if (!path) return
        stopRing()
        ringPlayer.loops = MediaPlayer.Once
        playRingtone(path)
        previewing = true
    }

    function stopPreview() {
        if (!previewing) return
        previewing = false
        stopAudio()
        ringPlayer.loops = MediaPlayer.Infinite
    }

    function stopRing() {
        if (!ringing) return
        ringing = false
        ringAlarm = null
        stopAudio()
        if (ringDialog.opened) ringDialog.close()
    }

    function snoozeRing() {
        if (!ringing || !ringAlarm) return
        snoozedAlarm = ringAlarm
        snoozeAtMs = Date.now() + ringingSnoozeMinutes * 60000
        ringing = false
        ringAlarm = null
        stopAudio()
        if (ringDialog.opened) ringDialog.close()
    }

    Popup {
        id: ringDialog
        objectName: "alarmRingDialog"
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        dim: true
        closePolicy: Popup.NoAutoClose
        padding: Theme.gap
        width: Math.min(520 * Theme.scaleFactor, (parent ? parent.width : 800) - Theme.gap * 4)
        background: Rectangle {
            radius: Theme.shapeLarge
            color: Theme.surface
            border.color: Theme.outline
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: Theme.gap / 2
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                MaterialIcon { name: "alarm"; Layout.preferredWidth: 28 * Theme.scaleFactor; Layout.preferredHeight: 28 * Theme.scaleFactor }
                CarText { text: engine.ringingName; heading: true; Layout.fillWidth: true; maximumLineCount: 1; elide: Text.ElideRight }
                CarText { text: engine.ringingTime; heading: true }
            }
            CarText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: I18n.t("闹钟正在响铃，可再响 ") + engine.ringingSnoozeMinutes + I18n.t(" 分钟后再提醒。")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap / 2
                Item { Layout.fillWidth: true }
                CarButton { objectName: "alarmSnoozeButton"; text: I18n.t("再响") ; onClicked: engine.snoozeRing() }
                CarButton { objectName: "alarmStopButton"; accent: true; text: I18n.t("停止"); onClicked: engine.stopRing() }
            }
        }
    }
}
