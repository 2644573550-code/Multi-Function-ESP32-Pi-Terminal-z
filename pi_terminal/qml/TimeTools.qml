import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "TimeMath.js" as TimeMath

// 时间：时钟 / 计时器 / 秒表 / 闹钟，一个内置应用页（section 12）。
//
// 边界（与作者约定一致）：
// - 本文件只负责界面；闹钟与计时器的运行状态归 AlarmEngine.qml（在 Main 实例化），
//   页面销毁后仍会计时与响铃。提示复用 Main.qml 已有的 notify()/announce()，
//   不新建全局 UI 层；渲染、模糊、主题、键盘与输入法实现均未改动。
// - 世界时钟默认用内置时区表（IANA 标准时区，不含夏令时规则）；只有用户点
//   “网络校时”才发起公开 HTTP 时间接口请求，失败一律显式报错，不伪造时间。
// - 闹钟、计时器预设、城市与校时设置通过 SystemTools 的 "time/" 前缀设置持久化；
//   秒表读数与计次刻意不持久化（恢复一个“还在跑”的秒表只会误导）。
// - primaryTimeHost / fallbackTimeHost 是给测试替换接口地址的接缝，正常运行为空。
Item {
    id: root
    objectName: "timeToolsPage"

    property bool active: false
    property real uiScale: 1

    // 接到宿主的既有通知横幅与语音播报（由 Main.qml 连接）。
    signal notifyRequested(string title, string message, bool important)
    signal announceRequested(string text)
    // 复用宿主已有的 FloatingKeyboard：这里只负责“请宿主打开键盘”。
    signal keyboardRequested()
    // 闹钟状态由 AlarmEngine 持有（页面销毁后仍计时）；为空时闹钟页给出提示。
    property var alarmEngine: null
    onAlarmEngineChanged: if (alarmEngine && alarmRingtone === "") alarmRingtone = alarmEngine.defaultRingtone
    // 离开时间页就停掉试听，避免声音跟着跑到别的应用里。
    onActiveChanged: if (!active && alarmEngine) alarmEngine.stopPreview()

    readonly property int tabClock: 0
    readonly property int tabTimer: 1
    readonly property int tabStopwatch: 2
    readonly property int tabAlarm: 3
    property int tab: 0

    // 城市表：offset 是 IANA 标准时区（分钟），dst 表示该时区有夏令时。
    // 未校时 + dst 时，界面明确说明“夏令时未知”，不假装准确。
    // 可选城市目录：offset 是 IANA 标准时区（分钟），dst 表示该时区有夏令时。
    readonly property var cityCatalog: [
        { id: "Asia/Shanghai", label: "北京", offset: 480, dst: false },
        { id: "Asia/Hong_Kong", label: "香港", offset: 480, dst: false },
        { id: "Asia/Taipei", label: "台北", offset: 480, dst: false },
        { id: "Asia/Tokyo", label: "东京", offset: 540, dst: false },
        { id: "Asia/Seoul", label: "首尔", offset: 540, dst: false },
        { id: "Asia/Singapore", label: "新加坡", offset: 480, dst: false },
        { id: "Asia/Bangkok", label: "曼谷", offset: 420, dst: false },
        { id: "Asia/Kolkata", label: "新德里", offset: 330, dst: false },
        { id: "Asia/Dubai", label: "迪拜", offset: 240, dst: false },
        { id: "Europe/Moscow", label: "莫斯科", offset: 180, dst: false },
        { id: "Europe/London", label: "伦敦", offset: 0, dst: true },
        { id: "Europe/Paris", label: "巴黎", offset: 60, dst: true },
        { id: "Europe/Berlin", label: "柏林", offset: 60, dst: true },
        { id: "Africa/Cairo", label: "开罗", offset: 120, dst: true },
        { id: "America/New_York", label: "纽约", offset: -300, dst: true },
        { id: "America/Chicago", label: "芝加哥", offset: -360, dst: true },
        { id: "America/Los_Angeles", label: "洛杉矶", offset: -480, dst: true },
        { id: "America/Sao_Paulo", label: "圣保罗", offset: -180, dst: false },
        { id: "Australia/Sydney", label: "悉尼", offset: 600, dst: true },
        { id: "Pacific/Auckland", label: "奥克兰", offset: 720, dst: true }
    ]
    readonly property int maxCities: 8
    property var selectedZones: ["Asia/Shanghai", "Asia/Tokyo", "Asia/Singapore",
                                 "Europe/London", "America/New_York", "America/Los_Angeles"]
    property bool editingCities: false
    // 只记住同步成功的偏移；选择变化不会丢掉已经查到的时区。
    property var syncedOffsets: ({})
    readonly property var cities: {
        const list = []
        for (let i = 0; i < selectedZones.length; ++i) {
            const entry = cityById(selectedZones[i])
            if (entry) list.push(entry)
        }
        return list
    }
    readonly property var zoneOffsets: cities.map(function(city) {
        return syncedOffsets[city.id] !== undefined ? syncedOffsets[city.id] : city.offset
    })
    readonly property var weekdayNames: ["星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"]

    // ------------------------------------------------------------------ clock

    property string clockTime: "00:00:00"
    property string clockDate: ""
    property string clockWeekday: ""
    property int localOffset: 0
    property string clockStyle: "digital"      // "digital" | "analog"
    property int clockHour: 0
    property int clockMinute: 0
    property int clockSecond: 0
    property double cityTickMs: 0
    readonly property real clockFontPx: Math.max(24, Math.min(58 * uiScale, width / 8.4))
    // 用一个不显示的文本量出数字格宽：FontMetrics 的 advanceWidth() 是方法调用，
    // 放在绑定里不会随字号变化重新求值，会和真实渲染的宽度对不上。
    CarText {
        id: clockProbe
        visible: false
        maximumLineCount: 1
        fontPixelSize: root.clockFontPx
        text: "00"
    }
    readonly property real clockCellWidth: Math.ceil(clockProbe.implicitWidth + 4 * uiScale)

    // ------------------------------------------------------------------ world

    property bool worldSyncing: false
    property int worldSyncedCount: 0
    property bool worldSyncedOnce: false
    property string worldProvider: ""
    property string worldError: ""
    property double worldSkewMs: 0
    property string worldSyncedAt: ""

    // 校时接口：默认按“公开接口优先”的顺序尝试；自定义模式严格只用自定义地址，
    // 失败不静默改走公共接口（与项目的传输策略一致）。
    property string primaryTimeHost: "https://worldtimeapi.org"
    property string fallbackTimeHost: "https://timeapi.io"
    property string apiMode: "auto"                  // "auto" | "custom"
    property string customHost: ""
    property var providerQueue: []
    property var providerState: ({})                 // id -> 0 未测 / 1 可用 / -1 不可用
    property int providerIndex: -1
    property string apiAlertText: ""
    property int syncIndex: -1
    property int syncToken: 0
    property double syncBaseUtcMs: 0
    property var syncXhr: null

    readonly property var providerIds: apiMode === "custom" && customHost.trim() !== ""
        ? ["custom", "worldtimeapi", "timeapi"] : ["worldtimeapi", "timeapi"]

    readonly property string worldStatus: {
        if (apiAlertText !== "") return apiAlertText
        if (worldSyncing) {
            const current = providerIndex >= 0 && providerIndex < providerQueue.length
                ? providerLabel(providerQueue[providerIndex].id) : ""
            return I18n.t("同步中") + " " + worldSyncedCount + "/" + cities.length +
                (current === "" ? "" : " · " + current)
        }
        if (worldError !== "") return worldError
        if (worldSyncedOnce) {
            let text = I18n.t("已校时") + " · " + worldProvider + " · " + I18n.t("偏差") + " " +
                TimeMath.formatSkew(worldSkewMs) + " · " + worldSyncedAt
            return text
        }
        if (apiMode === "custom")
            return I18n.t("自定义模式：只使用你填写的服务器，失败不会自动改用公共接口。")
        return I18n.t("未校时使用内置标准时区，伦敦/纽约/洛杉矶可能有夏令时偏差。")
    }

    // ------------------------------------------------------------ 持久化设置

    property double timerSavedAt: 0
    property bool loadingSettings: false
    Timer {
        id: timerSaveTimer
        interval: 400
        repeat: false
        onTriggered: root.persistTimerPreset()
    }

    function readSetting(key, fallback) {
        if (typeof systemTools === "undefined" || !systemTools ||
            typeof systemTools.settingsRead !== "function") return fallback
        return systemTools.settingsRead(key, fallback)
    }

    // 写失败只记一次日志，不阻塞交互；页面状态仍是本次会话的真值。
    function writeSetting(key, value) {
        if (typeof systemTools === "undefined" || !systemTools ||
            typeof systemTools.settingsWrite !== "function") return false
        return systemTools.settingsWrite(key, value)
    }

    function loadSettings() {
        loadingSettings = true
        const preset = readSetting("time/timer-preset", "0,5,0").split(",")
        timerHours = TimeMath.clamp(Number(preset[0]) || 0, 0, 23)
        timerMinutes = TimeMath.clamp(Number(preset[1]) || 0, 0, 59)
        timerSeconds = TimeMath.clamp(Number(preset[2]) || 0, 0, 59)
        if (alarmEngine) alarmEngine.resetTimer(timerTotalMs)
        clockStyle = readSetting("time/clock-style", "digital") === "analog" ? "analog" : "digital"
        apiMode = readSetting("time/api-mode", "auto") === "custom" ? "custom" : "auto"
        customHost = readSetting("time/api-host", "")
        const zones = readSetting("time/cities", "")
        if (zones !== "") {
            try {
                const list = JSON.parse(zones)
                const kept = []
                for (let i = 0; i < list.length && kept.length < maxCities; ++i)
                    if (cityById(list[i])) kept.push(list[i])
                if (kept.length > 0) selectedZones = kept
            } catch (error) {
                console.warn("time: ignoring unreadable city list")
            }
        }
        loadingSettings = false
    }

    function persistCities() {
        writeSetting("time/cities", JSON.stringify(selectedZones))
    }

    function persistTimerPreset() {
        writeSetting("time/timer-preset", timerHours + "," + timerMinutes + "," + timerSeconds)
    }

    function scheduleTimerPresetSave() {
        timerSavedAt = Date.now()
        timerSaveTimer.restart()
    }

    onClockStyleChanged: if (!loadingSettings) writeSetting("time/clock-style", clockStyle)
    onApiModeChanged: if (!loadingSettings) persistApiSettings()
    onCustomHostChanged: if (!loadingSettings) persistApiSettings()

    function persistApiSettings() {
        writeSetting("time/api-mode", apiMode)
        writeSetting("time/api-host", customHost.trim())
    }

    // -------------------------------------------------------------- countdown

    // 输入（时/分/秒）留在页面；运行状态归 AlarmEngine，页面关掉也会到点提示。
    property int timerHours: 0
    property int timerMinutes: 5
    property int timerSeconds: 0
    property int timerPresetMinutes: 0
    readonly property double timerTotalMs: TimeMath.hoursMinutesSecondsToMs(timerHours, timerMinutes, timerSeconds)
    readonly property bool timerRunning: alarmEngine ? alarmEngine.timerRunning : false
    readonly property bool timerFinished: alarmEngine ? alarmEngine.timerFinished : false
    readonly property double timerRemainingMs: alarmEngine ? alarmEngine.timerRemainingMs : timerTotalMs
    readonly property real timerProgress: timerTotalMs <= 0 ? 0 : TimeMath.clamp(1 - timerRemainingMs / timerTotalMs, 0, 1)
    readonly property string timerLabel: timerFinished ? I18n.t("计时结束") : TimeMath.formatCountdown(timerRemainingMs)

    // -------------------------------------------------------------- stopwatch

    property bool stopwatchRunning: false
    property double stopwatchStartedAtMs: 0
    property double stopwatchAccumMs: 0
    property double stopwatchElapsedMs: 0
    property var stopwatchLaps: []
    readonly property real stopwatchFontPx: Math.max(26, Math.min(54 * uiScale, width / 10))
    CarText {
        id: stopwatchProbe
        visible: false
        maximumLineCount: 1
        fontPixelSize: root.stopwatchFontPx
        text: "00"
    }
    CarText {
        id: stopwatchTenthsProbe
        visible: false
        maximumLineCount: 1
        fontPixelSize: root.stopwatchFontPx
        text: ".0"
    }
    readonly property string stopwatchMainText: {
        const parts = TimeMath.durationParts(stopwatchElapsedMs)
        const hours = parts.hours > 0 ? TimeMath.pad2(parts.hours) + ":" : ""
        return hours + TimeMath.pad2(parts.minutes) + ":" + TimeMath.pad2(parts.seconds)
    }
    readonly property string stopwatchTenths: "." + TimeMath.durationParts(stopwatchElapsedMs).tenths

    // ----------------------------------------------------------------- alarms

    property int alarmHour: 7
    property int alarmMinute: 30
    property bool alarmRepeatDaily: true
    property string alarmName: ""
    property bool alarmNameTouched: false
    property string alarmRingtone: ""
    property int alarmRingSeconds: 30
    property int alarmSnoozeMinutes: 5
    property bool ringtonePickerOpen: false
    property string ringtoneFolder: ""
    property var ringtoneRows: []
    readonly property var alarmList: alarmEngine ? alarmEngine.alarms : null

    // ---------------------------------------------------------------- actions


    function setTab(next) {
        tab = TimeMath.clamp(next, 0, 3)
    }

    function startCountdown() {
        if (alarmEngine) alarmEngine.startTimer(timerTotalMs)
    }

    function pauseCountdown() {
        if (alarmEngine) alarmEngine.pauseTimer()
    }

    function resetCountdown() {
        if (alarmEngine) alarmEngine.resetTimer(timerTotalMs)
    }

    function applyPreset(minutes) {
        timerPresetMinutes = minutes
        timerHours = Math.floor(minutes / 60)
        timerMinutes = minutes % 60
        timerSeconds = 0
        if (alarmEngine) alarmEngine.resetTimer(timerTotalMs)
        scheduleTimerPresetSave()
    }

    function adjustTimer(unit, delta) {
        if (timerRunning) return
        if (unit === "hour") timerHours = (timerHours + delta + 24) % 24
        else if (unit === "minute") timerMinutes = (timerMinutes + delta + 60) % 60
        else timerSeconds = (timerSeconds + delta + 60) % 60
        timerPresetMinutes = 0
        if (alarmEngine) alarmEngine.resetTimer(timerTotalMs)
        scheduleTimerPresetSave()
    }

    function toggleStopwatch() {
        if (stopwatchRunning) {
            stopwatchAccumMs += Date.now() - stopwatchStartedAtMs
            stopwatchElapsedMs = stopwatchAccumMs
            stopwatchRunning = false
        } else {
            stopwatchStartedAtMs = Date.now()
            stopwatchRunning = true
        }
    }

    function resetStopwatch() {
        stopwatchRunning = false
        stopwatchAccumMs = 0
        stopwatchElapsedMs = 0
        stopwatchLaps = []
    }

    function addLap() {
        if (!stopwatchRunning && stopwatchElapsedMs <= 0) return
        const next = stopwatchLaps.concat([stopwatchElapsedMs])
        stopwatchLaps = next.length > 20 ? next.slice(next.length - 20) : next
    }

    function lapSplit(index) {
        if (index <= 0) return stopwatchLaps[0]
        return stopwatchLaps[index] - stopwatchLaps[index - 1]
    }

    // ---------------------------------------------------------------- 闹钟表单

    function addAlarmFromForm() {
        if (!alarmEngine) return
        const ok = alarmEngine.addAlarm({
            hour: alarmHour,
            minute: alarmMinute,
            repeatDaily: alarmRepeatDaily,
            name: alarmName.trim(),
            ringtone: alarmRingtone,
            ringSeconds: alarmRingSeconds,
            snoozeMinutes: alarmSnoozeMinutes
        })
        if (!ok) notifyRequested(I18n.t("闹钟"), I18n.t("最多 8 个闹钟，或已有同名同时刻的闹钟"), false)
    }

    function openRingtonePicker() {
        // 复用宿主的文件枚举（SystemTools.files），不复制也不修改文件管理器逻辑。
        if (typeof systemTools === "undefined" || !systemTools ||
            typeof systemTools.files !== "function") {
            notifyRequested(I18n.t("铃声"), I18n.t("当前环境无法浏览文件"), false)
            return
        }
        ringtoneFolder = ""
        refreshRingtoneRows()
        ringtonePickerOpen = true
    }

    function refreshRingtoneRows() {
        if (typeof systemTools === "undefined" || !systemTools ||
            typeof systemTools.files !== "function") { ringtoneRows = []; return }
        ringtoneRows = systemTools.files(ringtoneFolder).filter(function(entry) {
            if (entry.directory) return true
            return /\.(mp3|wav|m4a|ogg)$/i.test(entry.name)
        })
    }

    function ringtoneEntryPath(entry) {
        return entry.relative || ((ringtoneFolder ? ringtoneFolder + "/" : "") + entry.name)
    }

    function openRingtoneEntry(entry) {
        if (entry.directory) {
            ringtoneFolder = ringtoneEntryPath(entry)
            refreshRingtoneRows()
            return
        }
        alarmRingtone = ringtoneEntryPath(entry)
        ringtonePickerOpen = false
    }

    // ------------------------------------------------------------- world sync

    function resetWorld() {
        resetZoneData()
        worldSyncedOnce = false
        worldProvider = ""
        worldSyncedAt = ""
        worldSkewMs = 0
        worldError = ""
        providerState = ({})
        providerQueue = []
        providerIndex = -1
    }

    function buildProviderQueue() {
        if (apiMode === "custom") {
            const host = customHost.trim()
            if (!/^https?:\/\//i.test(host)) return []
            return [{ id: "custom", host: host, kind: "worldtimeapi" }]
        }
        return [{ id: "worldtimeapi", host: primaryTimeHost, kind: "worldtimeapi" },
                { id: "timeapi", host: fallbackTimeHost, kind: "timeapi" }]
    }

    // 界面只显示“线路一/线路二”，不暴露真实服务地址。
    function providerLabel(id) {
        if (id === "custom") return customHost === "" ? I18n.t("自定义服务器") : I18n.t("自定义服务器")
        return id === "worldtimeapi" ? I18n.t("线路一") : I18n.t("线路二")
    }

    function providerStateOf(id) {
        const state = providerState[id]
        return state === undefined ? 0 : state
    }

    function setProviderState(id, value) {
        const next = Object.assign({}, providerState)
        next[id] = value
        providerState = next
    }

    function cityById(id) {
        for (let i = 0; i < cityCatalog.length; ++i)
            if (cityCatalog[i].id === id) return cityCatalog[i]
        return null
    }

    function toggleCity(id) {
        const list = selectedZones.slice()
        const at = list.indexOf(id)
        if (at >= 0) {
            if (list.length <= 1) return          // 至少保留一个城市
            list.splice(at, 1)
        } else {
            if (list.length >= maxCities) {
                openApiAlert(I18n.t("最多 8 个城市，先取消一个再添加。"))
                return
            }
            list.push(id)
        }
        selectedZones = list
        persistCities()
    }

    function resetZoneData() {
        syncedOffsets = ({})
        worldSyncedCount = 0
    }

    function openApiAlert(text) {
        apiAlertText = text
    }

    function syncWorld() {
        if (worldSyncing) return
        const queue = buildProviderQueue()
        if (queue.length === 0) {
            openApiAlert(I18n.t("自定义服务器地址需以 http:// 或 https:// 开头，例如 https://time.example.com"))
            return
        }
        providerQueue = queue
        providerIndex = -1
        resetZoneData()
        providerState = ({})          // 红点只在本次同步内有效
        worldSyncing = true
        worldError = ""
        worldSyncedCount = 0
        worldSyncedOnce = false
        worldProvider = ""
        requestNextProvider()
    }

    function requestNextProvider() {
        if (!worldSyncing) return
        ++providerIndex
        if (providerIndex >= providerQueue.length) { finishWorldSync(); return }
        syncIndex = -1
        syncBaseUtcMs = 0
        requestSyncStep()
    }

    function requestSyncStep() {
        if (!worldSyncing) return
        const provider = providerQueue[providerIndex]
        // timeapi.io 的返回只有城市墙上时间，没有偏移量，先取一次 UTC 基准。
        if (provider.kind === "timeapi" && syncBaseUtcMs === 0) { sendSyncRequest(-1); return }
        ++syncIndex
        if (syncIndex >= cities.length) {
            setProviderState(provider.id, 1)
            finishWorldSync()
            return
        }
        sendSyncRequest(syncIndex)
    }

    function sendSyncRequest(index) {
        const provider = providerQueue[providerIndex]
        const token = ++syncToken
        const zone = index < 0 ? "UTC" : cities[index].id
        const url = provider.kind === "timeapi"
            ? TimeMath.buildTimeApiUrl(provider.host, zone)
            : TimeMath.buildWorldTimeUrl(provider.host, zone)
        const xhr = new XMLHttpRequest()
        syncXhr = xhr
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) return
            handleSyncResponse(index, xhr, token)
        }
        syncWatchdog.restart()
        xhr.open("GET", url)
        xhr.send()
    }

    function handleSyncResponse(index, xhr, token) {
        if (token !== syncToken) return
        syncWatchdog.stop()
        if (xhr.status < 200 || xhr.status >= 300) {
            syncRequestFailed(index, token, xhr.status === 0 ? I18n.t("网络不可用或服务不可达") : ("HTTP " + xhr.status))
            return
        }
        const provider = providerQueue[providerIndex]
        let parsed = null
        try {
            parsed = provider.kind === "timeapi" ? TimeMath.parseTimeApiZone(xhr.responseText)
                                                 : TimeMath.parseWorldTimeApi(xhr.responseText)
        } catch (error) {
            syncRequestFailed(index, token, I18n.t("数据格式无效"))
            return
        }
        if (!parsed || !parsed.ok) {
            syncRequestFailed(index, token, (parsed && parsed.error) ? parsed.error : I18n.t("数据格式无效"))
            return
        }
        const now = Date.now()
        if (index < 0) {
            syncBaseUtcMs = parsed.wallUtcMs
            worldSkewMs = TimeMath.skewMs(syncBaseUtcMs, now)
            requestSyncStep()
            return
        }
        const offset = provider.kind === "timeapi"
            ? TimeMath.offsetFromBaselines(parsed.wallUtcMs, syncBaseUtcMs)
            : parsed.offsetMinutes
        if (offset === null) {
            syncRequestFailed(index, token, I18n.t("数据格式无效"))
            return
        }
        applyZoneOffset(index, offset)
        if (provider.kind === "worldtimeapi") worldSkewMs = TimeMath.skewMs(parsed.serverUtcMs, now)
        requestSyncStep()
    }

    function applyZoneOffset(index, offset) {
        if (index < 0 || index >= cities.length) return
        const next = Object.assign({}, syncedOffsets)
        next[cities[index].id] = offset
        syncedOffsets = next
        ++worldSyncedCount
    }

    function cancelSyncRequest() {
        syncWatchdog.stop()
        ++syncToken
        if (syncXhr) { try { syncXhr.abort() } catch (error) {} }
        syncXhr = null
    }

    function syncRequestFailed(index, token, reason) {
        if (token !== syncToken) return
        cancelSyncRequest()
        const provider = providerQueue[providerIndex]
        if (!provider) { worldSyncing = false; return }
        setProviderState(provider.id, -1)
        const more = providerIndex + 1 < providerQueue.length
        const label = providerLabel(provider.id)
        if (more) {
            resetZoneData()
            openApiAlert(label + " · " + reason + "\n" + I18n.t("已切换到下一个校时接口。"))
            requestNextProvider()
            return
        }
        worldSyncedCount = 0
        worldError = I18n.t("网络校时失败：") + reason
        worldSyncing = false
        openApiAlert(label + " · " + reason + "\n" +
            (apiMode === "custom" ? I18n.t("自定义模式不会自动改用公共接口，可切回自动后重试。")
                                  : I18n.t("所有校时接口均不可用，仍显示内置时区。")))
    }

    function finishWorldSync() {
        cancelSyncRequest()
        worldSyncing = false
        if (worldSyncedCount <= 0) {
            worldSyncedOnce = false
            worldError = I18n.t("网络校时失败：") + I18n.t("网络不可用或服务不可达")
            return
        }
        worldSyncedOnce = true
        worldProvider = providerIndex >= 0 && providerIndex < providerQueue.length
            ? providerLabel(providerQueue[providerIndex].id) : ""
        worldSyncedAt = TimeMath.formatHm(TimeMath.localParts(Date.now()))
        worldError = ""
    }

    // -------------------------------------------------------------- main tick

    function tick() {
        const now = Date.now()
        const local = TimeMath.localParts(now)
        const time = TimeMath.formatHms(local, true)
        if (time !== clockTime) clockTime = time
        clockHour = local.hour
        clockMinute = local.minute
        clockSecond = local.second
        const date = TimeMath.formatYmd(local)
        if (date !== clockDate) {
            clockDate = date
            clockWeekday = I18n.t(weekdayNames[local.weekday])
            localOffset = TimeMath.localOffsetMinutes(now)
        }
        if (now - cityTickMs >= 1000) cityTickMs = now
        if (stopwatchRunning) stopwatchElapsedMs = now - stopwatchStartedAtMs + stopwatchAccumMs
    }

    // 页面隐藏时降到 1 Hz，只看时钟、闹钟与计时；显示时 10 Hz 刷新秒表。
    Timer {
        interval: root.active ? 100 : 1000
        running: true
        repeat: true
        onTriggered: root.tick()
    }
    Timer {
        id: syncWatchdog
        interval: 8000
        repeat: false
        onTriggered: root.syncRequestFailed(root.syncIndex, root.syncToken, I18n.t("请求超时"))
    }
    Component.onCompleted: { root.loadSettings(); root.tick() }

    Flickable {
        id: flick
        objectName: "timePage"
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        clip: true

        ColumnLayout {
            id: column
            width: flick.width
            spacing: Theme.gap

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap / 2
                CarButton {
                    text: I18n.t("时钟"); Layout.fillWidth: true; Layout.preferredHeight: Theme.hit
                    checked: root.tab === root.tabClock; onClicked: root.setTab(root.tabClock)
                }
                CarButton {
                    text: I18n.t("计时器"); Layout.fillWidth: true; Layout.preferredHeight: Theme.hit
                    checked: root.tab === root.tabTimer; onClicked: root.setTab(root.tabTimer)
                }
                CarButton {
                    text: I18n.t("秒表"); Layout.fillWidth: true; Layout.preferredHeight: Theme.hit
                    checked: root.tab === root.tabStopwatch; onClicked: root.setTab(root.tabStopwatch)
                }
                CarButton {
                    text: I18n.t("闹钟"); Layout.fillWidth: true; Layout.preferredHeight: Theme.hit
                    checked: root.tab === root.tabAlarm; onClicked: root.setTab(root.tabAlarm)
                }
            }

            // 只实例化当前标签页：Loader 的隐式高度紧跟 item，
            // Flickable 的 contentHeight 不会再多出一截空白。
            Loader {
                id: tabLoader
                objectName: "tabLoader"
                Layout.fillWidth: true
                sourceComponent: root.tab === root.tabClock ? clockPage
                               : root.tab === root.tabTimer ? timerPage
                               : root.tab === root.tabStopwatch ? stopwatchPage : alarmPage
            }
            Component {
                id: clockPage
                ColumnLayout {
                    objectName: "timeClockTab"
                    width: tabLoader.width
                    spacing: Theme.gap
            SettingsGroup {
                Layout.fillWidth: true
                iconName: "schedule"
                accentColor: "#8f7bff"
                title: I18n.t("本机时间")
                subtitle: I18n.t("系统时钟") + " · " + TimeMath.formatOffset(root.localOffset)

                // 数字钟、表盘与日期放在同一个水平居中的列里；样式开关浮在右上角，
                // 不参与列宽计算，所以三者始终落在同一条中线上。
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(clockColumn.implicitHeight, Theme.hit)
                    Column {
                        id: clockColumn
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.gap / 2
                        Row {
                            id: digitalClockRow
                            visible: root.clockStyle === "digital"
                            anchors.horizontalCenter: parent.horizontalCenter
                            Repeater {
                                model: 3
                                delegate: Row {
                                    required property int index
                                    Item {
                                        width: root.clockCellWidth
                                        height: clockText.implicitHeight
                                        CarText {
                                            id: clockText
                                            anchors.centerIn: parent
                                            fontPixelSize: root.clockFontPx
                                            maximumLineCount: 1
                                            text: index === 0 ? root.clockTime.substring(0, 2)
                                                : index === 1 ? root.clockTime.substring(3, 5)
                                                : root.clockTime.substring(6, 8)
                                            Accessible.name: index === 0 ? I18n.t("时") : index === 1 ? I18n.t("分") : I18n.t("秒")
                                        }
                                    }
                                    CarText {
                                        visible: index < 2
                                        fontPixelSize: root.clockFontPx
                                        maximumLineCount: 1
                                        text: ":"
                                        Accessible.ignored: true
                                    }
                                }
                            }
                        }
                        AnalogClock {
                            objectName: "analogClock"
                            visible: root.clockStyle === "analog"
                            anchors.horizontalCenter: parent.horizontalCenter
                            height: 148 * root.uiScale
                            hours: root.clockHour
                            minutes: root.clockMinute
                            seconds: root.clockSecond
                        }
                        CarText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            maximumLineCount: 1
                            text: root.clockDate + "  " + root.clockWeekday
                        }
                    }
                    Row {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        spacing: Theme.gap / 2
                        CarButton {
                            objectName: "clockStyleDigital"
                            text: I18n.t("数字")
                            checked: root.clockStyle === "digital"
                            onClicked: root.clockStyle = "digital"
                        }
                        CarButton {
                            objectName: "clockStyleAnalog"
                            text: I18n.t("指针")
                            checked: root.clockStyle === "analog"
                            onClicked: root.clockStyle = "analog"
                        }
                    }
                }
            }

            SettingsGroup {
                Layout.fillWidth: true
                iconName: "public"
                accentColor: "#4fd4a4"
                title: I18n.t("世界时钟")
                subtitle: I18n.t("点击城市可播报当地时间和时区")

                Repeater {
                    model: root.cities.length
                    delegate: RowLayout {
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: Math.max(Theme.hit, 40 * root.uiScale)
                        spacing: Theme.gap
                        CarText {
                            Layout.fillWidth: true
                            maximumLineCount: 1
                            elide: Text.ElideRight
                            text: I18n.t(root.cities[index].label)
                        }
                        CarText {
                            Layout.preferredWidth: 84 * root.uiScale
                            horizontalAlignment: Text.AlignRight
                            maximumLineCount: 1
                            fontPixelSize: 24 * root.uiScale
                            text: TimeMath.formatHm(TimeMath.cityParts(root.cityTickMs, root.zoneOffsets[index]))
                        }
                        CarText {
                            // 与左侧时间同字号；列宽按 "UTC+08:00" 的实际宽度给。
                            Layout.preferredWidth: 132 * root.uiScale
                            horizontalAlignment: Text.AlignRight
                            maximumLineCount: 1
                            fontPixelSize: 24 * root.uiScale
                            text: TimeMath.formatOffset(root.zoneOffsets[index])
                        }
                        IconButton {
                            iconName: "volume_up"
                            glyphSize: 20 * root.uiScale
                            label: I18n.t("播报") + " " + I18n.t(root.cities[index].label)
                            onClicked: root.announceRequested(
                                I18n.t(root.cities[index].label) + " " +
                                TimeMath.formatHm(TimeMath.cityParts(Date.now(), root.zoneOffsets[index])) + " " +
                                TimeMath.formatOffset(root.zoneOffsets[index]))
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap / 2
                    CarText {
                        Layout.fillWidth: true
                        maximumLineCount: 1
                        fontPixelSize: 13 * root.uiScale
                        text: I18n.t("城市") + " " + root.cities.length + "/" + root.maxCities
                    }
                    CarButton {
                        objectName: "timeCityEdit"
                        text: root.editingCities ? I18n.t("完成") : I18n.t("管理城市")
                        checked: root.editingCities
                        onClicked: root.editingCities = !root.editingCities
                    }
                }
                GridLayout {
                    objectName: "timeCityPicker"
                    Layout.fillWidth: true
                    visible: root.editingCities
                    columns: root.width < 700 * root.uiScale ? 3 : 4
                    columnSpacing: Theme.gap / 2
                    rowSpacing: Theme.gap / 2
                    Repeater {
                        model: root.cityCatalog
                        delegate: CarButton {
                            required property var modelData
                            Layout.fillWidth: true
                            text: I18n.t(modelData.label)
                            checked: root.selectedZones.indexOf(modelData.id) >= 0
                            onClicked: root.toggleCity(modelData.id)
                            Accessible.name: I18n.t(modelData.label) + " " +
                                (checked ? I18n.t("已添加") : I18n.t("未添加"))
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap / 2
                    CarButton {
                        objectName: "timeSyncButton"
                        text: root.worldSyncing ? I18n.t("同步中") + "…" : I18n.t("网络校时")
                        enabled: !root.worldSyncing
                        onClicked: root.syncWorld()
                    }
                    CarButton {
                        visible: root.worldSyncedOnce
                        text: I18n.t("恢复内置时区")
                        onClicked: root.resetWorld()
                    }
                }
                // 接口队列：名字后面跟一个状态点（绿=可用，红=不可用，灰=本次未测）。
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    Repeater {
                        model: root.providerIds
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 6 * root.uiScale
                            CarText {
                                maximumLineCount: 1
                                elide: Text.ElideMiddle
                                fontPixelSize: 15 * root.uiScale
                                text: root.providerLabel(modelData)
                            }
                            Rectangle {
                                objectName: "timeProviderDot"
                                width: 10 * root.uiScale
                                height: width
                                radius: width / 2
                                color: {
                                    const state = root.providerStateOf(modelData)
                                    return state === 1 ? "#5ad59a" : state === -1 ? "#ff6584" : Theme.outlineVariant
                                }
                                Accessible.name: root.providerLabel(modelData) + " " +
                                    (root.providerStateOf(modelData) === 1 ? I18n.t("可用")
                                     : root.providerStateOf(modelData) === -1 ? I18n.t("不可用") : I18n.t("未测试"))
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap / 2
                    CarButton {
                        objectName: "timeApiModeAuto"
                        text: I18n.t("自动")
                        checked: root.apiMode === "auto"
                        onClicked: root.apiMode = "auto"
                    }
                    CarButton {
                        objectName: "timeApiModeCustom"
                        text: I18n.t("自定义服务器")
                        checked: root.apiMode === "custom"
                        onClicked: root.apiMode = "custom"
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.apiMode === "custom"
                    spacing: Theme.gap / 2
                    CarField {
                        id: customHostField
                        objectName: "timeApiHostField"
                        Layout.fillWidth: true
                        text: root.customHost
                        placeholderText: "https://time.example.com"
                        onTextChanged: root.customHost = text
                    }
                }
                CarText {
                    visible: root.apiMode === "custom"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    fontPixelSize: 12 * root.uiScale
                    text: I18n.t("自定义服务器需兼容 worldtimeapi.org 的返回格式（/api/timezone/<区域> → utc_offset、unixtime）。")
                }
                CarText {
                    objectName: "timeSyncStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: root.worldStatus
                }
                CarText {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    fontPixelSize: 12 * root.uiScale
                    text: I18n.t("校时只在你点击时联网，失败会如实显示错误。")
                }
            }
            }
            }
            // ---- 计时器
            Component {
                id: timerPage
                ColumnLayout {
                    objectName: "timeTimerTab"
                    width: tabLoader.width
                    spacing: Theme.gap
            SettingsGroup {
                Layout.fillWidth: true
                objectName: "timeTimerTab"
                iconName: "hourglass_empty"
                accentColor: "#ffbd54"
                title: I18n.t("计时器")
                subtitle: I18n.t("到点后显示通知横幅并语音播报")

                // 固定三列而不是 Flow：Flow 的高度依赖宽度，在嵌套 ColumnLayout 里会
                // 触发 polish 循环（实机 log 可复现），网格布局行为确定。
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width < 620 * root.uiScale ? 3 : 6
                    columnSpacing: Theme.gap / 2
                    rowSpacing: Theme.gap / 2
                    Repeater {
                        model: [1, 3, 5, 10, 15, 30]
                        delegate: CarButton {
                            required property int modelData
                            Layout.fillWidth: true
                            text: modelData + I18n.t(" 分钟")
                            checked: root.timerPresetMinutes === modelData && !root.timerRunning
                            onClicked: root.applyPreset(modelData)
                        }
                    }
                }

                RowLayout {
                    // ColumnLayout 默认把子项拉满宽度；上限定成自身内容宽,
                    // 再用对齐把整组控件居中，而不是让 +/- 被拉到卡片两端。
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: implicitWidth
                    objectName: "timerStepperRow"
                    spacing: Theme.gap
                    NumberStepper {
                        caption: I18n.t("时")
                        value: root.timerHours
                        maximum: 23
                        onAdjusted: function(delta) { root.adjustTimer("hour", delta) }
                    }
                    NumberStepper {
                        caption: I18n.t("分")
                        value: root.timerMinutes
                        maximum: 59
                        onAdjusted: function(delta) { root.adjustTimer("minute", delta) }
                    }
                    NumberStepper {
                        caption: I18n.t("秒")
                        value: root.timerSeconds
                        maximum: 59
                        onAdjusted: function(delta) { root.adjustTimer("second", delta) }
                    }
                }

                Rectangle {
                    objectName: "timerDisplayBox"
                    Layout.fillWidth: true
                    implicitHeight: Math.max(84 * root.uiScale, timerRemaining.implicitHeight + Theme.gap)
                    radius: Theme.shapeLarge
                    color: Theme.surfaceContainerHigh
                    border.width: 1
                    border.color: Theme.outlineVariant
                    CarText {
                        id: timerRemaining
                        objectName: "timerRemaining"
                        anchors.centerIn: parent
                        maximumLineCount: 1
                        fontPixelSize: Math.max(30, Math.min(56 * root.uiScale, root.width / 9))
                        text: root.timerLabel
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 8 * root.uiScale
                    radius: height / 2
                    color: Theme.raised
                    Rectangle {
                        width: parent.width * root.timerProgress
                        height: parent.height
                        radius: parent.radius
                        color: Theme.accent
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap / 2
                    CarButton {
                        objectName: "timerToggle"
                        Layout.fillWidth: true
                        accent: !root.timerRunning
                        text: root.timerRunning ? I18n.t("暂停")
                            : root.timerRemainingMs > 0 && root.timerRemainingMs < root.timerTotalMs ? I18n.t("继续") : I18n.t("开始")
                        onClicked: root.timerRunning ? root.pauseCountdown() : root.startCountdown()
                    }
                    CarButton {
                        text: I18n.t("重置")
                        onClicked: root.resetCountdown()
                    }
                }
            }

                }
            }
            // ---- 秒表
            Component {
                id: stopwatchPage
                ColumnLayout {
                    objectName: "timeStopwatchTab"
                    width: tabLoader.width
                    spacing: Theme.gap
            SettingsGroup {
                Layout.fillWidth: true
                objectName: "timeStopwatchTab"
                iconName: "av_timer"
                accentColor: "#36c8db"
                title: I18n.t("秒表")
                subtitle: I18n.t("计次最多保留 20 条")

                Rectangle {
                    objectName: "stopwatchDisplayBox"
                    Layout.fillWidth: true
                    implicitHeight: Math.max(84 * root.uiScale, root.stopwatchFontPx * 1.6)
                    radius: Theme.shapeLarge
                    color: Theme.surfaceContainerHigh
                    border.width: 1
                    border.color: Theme.outlineVariant
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 0
                        CarText {
                            maximumLineCount: 1
                            fontPixelSize: root.stopwatchFontPx
                            text: root.stopwatchMainText
                            horizontalAlignment: Text.AlignRight
                        }
                        CarText {
                            Layout.preferredWidth: stopwatchTenthsProbe.implicitWidth + 8 * root.uiScale
                            maximumLineCount: 1
                            fontPixelSize: root.stopwatchFontPx
                            text: root.stopwatchTenths
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap / 2
                    CarButton {
                        objectName: "stopwatchToggle"
                        Layout.fillWidth: true
                        accent: !root.stopwatchRunning
                        text: root.stopwatchRunning ? I18n.t("暂停") : (root.stopwatchElapsedMs > 0 ? I18n.t("继续") : I18n.t("开始"))
                        onClicked: root.toggleStopwatch()
                    }
                    CarButton {
                        text: I18n.t("计次")
                        enabled: root.stopwatchRunning
                        onClicked: root.addLap()
                    }
                    CarButton {
                        text: I18n.t("重置")
                        enabled: root.stopwatchElapsedMs > 0
                        onClicked: root.resetStopwatch()
                    }
                }
                CarText {
                    Layout.fillWidth: true
                    visible: root.stopwatchLaps.length === 0
                    subdued: true
                    text: I18n.t("暂无计次")
                }
                // 计次容器：条目越多容器越高（不设固定高度、不在内部滚动），
                // 整页仍由外层 Flickable 滚动，和系统秒表的行为一致。
                Rectangle {
                    objectName: "stopwatchLapBox"
                    Layout.fillWidth: true
                    visible: root.stopwatchLaps.length > 0
                    implicitHeight: lapColumn.implicitHeight + Theme.gap
                    radius: Theme.shapeLarge
                    color: Theme.surfaceContainerHigh
                    border.width: 1
                    border.color: Theme.outlineVariant
                    ColumnLayout {
                        id: lapColumn
                        x: Theme.gap
                        y: Theme.gap / 2
                        width: Math.max(1, parent.width - Theme.gap * 2)
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30 * root.uiScale
                            spacing: Theme.gap / 2
                            CarText {
                                Layout.preferredWidth: 72 * root.uiScale
                                maximumLineCount: 1
                                fontPixelSize: 12 * root.uiScale
                                text: I18n.t("计次")
                            }
                            CarText {
                                Layout.fillWidth: true
                                maximumLineCount: 1
                                horizontalAlignment: Text.AlignRight
                                fontPixelSize: 12 * root.uiScale
                                text: I18n.t("分段")
                            }
                            CarText {
                                Layout.preferredWidth: 120 * root.uiScale
                                maximumLineCount: 1
                                horizontalAlignment: Text.AlignRight
                                fontPixelSize: 12 * root.uiScale
                                text: I18n.t("总计")
                            }
                        }
                        Repeater {
                            model: root.stopwatchLaps.length
                            delegate: ColumnLayout {
                                required property int index
                                Layout.fillWidth: true
                                spacing: 0
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 1
                                    color: Theme.outlineVariant
                                    opacity: 0.5
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    implicitHeight: Math.max(36 * root.uiScale, 36)
                                    spacing: Theme.gap / 2
                                    CarText {
                                        Layout.preferredWidth: 72 * root.uiScale
                                        maximumLineCount: 1
                                        text: I18n.t("计次") + " " + (root.stopwatchLaps.length - index)
                                    }
                                    CarText {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignRight
                                        maximumLineCount: 1
                                        text: "+" + TimeMath.formatStopwatch(root.lapSplit(root.stopwatchLaps.length - 1 - index))
                                    }
                                    CarText {
                                        Layout.preferredWidth: 120 * root.uiScale
                                        horizontalAlignment: Text.AlignRight
                                        maximumLineCount: 1
                                        text: TimeMath.formatStopwatch(root.stopwatchLaps[root.stopwatchLaps.length - 1 - index])
                                    }
                                }
                            }
                        }
                    }
                }
            }

                }
            }
            // ---- 闹钟（数据与响铃由 AlarmEngine 持有）
            Component {
                id: alarmPage
                ColumnLayout {
                    objectName: "timeAlarmTab"
                    width: tabLoader.width
                    spacing: Theme.gap

                    SettingsGroup {
                        Layout.fillWidth: true
                        visible: root.alarmEngine !== null
                        iconName: "alarm"
                        accentColor: "#ff6584"
                        title: I18n.t("新建闹钟")
                        subtitle: I18n.t("到点弹通知并语音播报；闹钟会保存到本机")

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumWidth: implicitWidth
                            spacing: Theme.gap
                            NumberStepper {
                                objectName: "alarmHourStepper"
                                caption: I18n.t("时")
                                value: root.alarmHour
                                maximum: 23
                                onAdjusted: function(delta) { root.alarmHour = (root.alarmHour + delta + 24) % 24 }
                            }
                            NumberStepper {
                                objectName: "alarmMinuteStepper"
                                caption: I18n.t("分")
                                value: root.alarmMinute
                                maximum: 59
                                onAdjusted: function(delta) { root.alarmMinute = (root.alarmMinute + delta + 60) % 60 }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap / 2
                            Switch {
                                id: alarmRepeatSwitch
                                objectName: "alarmRepeatSwitch"
                                text: I18n.t("每天")
                                implicitHeight: Theme.hit
                                Binding { target: alarmRepeatSwitch; property: "checked"; value: root.alarmRepeatDaily }
                                onClicked: root.alarmRepeatDaily = !root.alarmRepeatDaily
                                Accessible.name: I18n.t("每天")
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // 闹钟名：点一下就聚焦并请宿主弹出项目自带屏幕键盘。
                        CarField {
                            id: alarmNameField
                            objectName: "alarmNameField"
                            Layout.fillWidth: true
                            text: root.alarmName
                            // 点过一次之后就不再显示“点击输入”的提示。
                            placeholderText: root.alarmNameTouched ? "" : I18n.t("闹钟名（点击输入）")
                            onTextChanged: root.alarmName = text
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    root.alarmNameTouched = true
                                    alarmNameField.forceActiveFocus()
                                    root.keyboardRequested()
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap / 2
                            CarText {
                                objectName: "alarmRingtoneName"
                                Layout.fillWidth: true
                                maximumLineCount: 1
                                elide: Text.ElideMiddle
                                text: I18n.t("铃声") + " · " +
                                    (root.alarmEngine ? root.alarmEngine.ringtoneLabel(root.alarmRingtone)
                                                      : I18n.t("未选择（仅语音播报）"))
                            }
                            CarButton {
                                objectName: "alarmRingtoneDefault"
                                text: I18n.t("默认")
                                enabled: root.alarmEngine !== null &&
                                         root.alarmRingtone !== root.alarmEngine.defaultRingtone
                                onClicked: {
                                    root.alarmRingtone = root.alarmEngine.defaultRingtone
                                    root.alarmEngine.stopPreview()
                                }
                            }
                            IconButton {
                                objectName: "alarmRingtonePreview"
                                iconName: root.alarmEngine && root.alarmEngine.previewing ? "stop" : "play_arrow"
                                label: root.alarmEngine && root.alarmEngine.previewing ? I18n.t("停止试听") : I18n.t("试听铃声")
                                onClicked: if (root.alarmEngine) root.alarmEngine.togglePreview(root.alarmRingtone)
                            }
                            CarButton {
                                objectName: "alarmRingtoneButton"
                                text: I18n.t("选择铃声")
                                onClicked: root.openRingtonePicker()
                            }
                        }
                        // 标题与自己的数字框同一行：两行搞定，不再出现“共用选择器”的错觉。
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap / 2
                            CarText { Layout.fillWidth: true; maximumLineCount: 1; text: I18n.t("响铃时长") }
                            StepperBox {
                                objectName: "alarmDurationStepper"
                                value: root.alarmRingSeconds
                                maximum: 120
                                caption: I18n.t("响铃时长")
                                onAdjusted: function(delta) {
                                    root.alarmRingSeconds = TimeMath.clamp(root.alarmRingSeconds + delta * 5, 5, 120)
                                }
                            }
                            CarText { maximumLineCount: 1; text: I18n.t("秒") }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap / 2
                            CarText { Layout.fillWidth: true; maximumLineCount: 1; text: I18n.t("再响间隔") }
                            StepperBox {
                                objectName: "alarmSnoozeStepper"
                                value: root.alarmSnoozeMinutes
                                maximum: 30
                                caption: I18n.t("再响间隔")
                                onAdjusted: function(delta) {
                                    root.alarmSnoozeMinutes = TimeMath.clamp(root.alarmSnoozeMinutes + delta, 1, 30)
                                }
                            }
                            CarText { maximumLineCount: 1; text: I18n.t("分") }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            CarButton {
                                objectName: "alarmAddButton"
                                accent: true
                                text: I18n.t("添加闹钟")
                                onClicked: root.addAlarmFromForm()
                            }
                        }
                    }

                    SettingsGroup {
                        Layout.fillWidth: true
                        visible: root.alarmEngine !== null
                        iconName: "schedule"
                        accentColor: "#ff9d37"
                        title: I18n.t("已设置闹钟")
                        subtitle: root.alarmEngine === null ? "" :
                            (root.alarmEngine.enabledCount + "/" + root.alarmEngine.maxAlarms + I18n.t(" 个已启用"))

                        CarText {
                            Layout.fillWidth: true
                            visible: root.alarmList !== null && root.alarmList.count === 0
                            subdued: true
                            text: I18n.t("暂无闹钟")
                        }
                        Repeater {
                            model: root.alarmList
                            delegate: RowLayout {
                                required property int index
                                required property int hour
                                required property int minute
                                required property bool repeatDaily
                                required property bool alarmEnabled
                                required property string name
                                required property int ringSeconds
                                required property int snoozeMinutes
                                Layout.fillWidth: true
                                implicitHeight: Math.max(Theme.hit, 48 * root.uiScale)
                                spacing: Theme.gap

                                Rectangle {
                                    Layout.preferredWidth: 104 * root.uiScale
                                    implicitHeight: 44 * root.uiScale
                                    radius: Theme.shapeMedium
                                    color: Theme.surfaceContainerHigh
                                    border.width: 1
                                    border.color: Theme.outlineVariant
                                    CarText {
                                        anchors.centerIn: parent
                                        maximumLineCount: 1
                                        fontPixelSize: 22 * root.uiScale
                                        text: TimeMath.alarmTimeLabel({ hour: hour, minute: minute })
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    CarText {
                                        Layout.fillWidth: true
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                        text: name !== "" ? name : I18n.t("闹钟")
                                    }
                                    CarText {
                                        Layout.fillWidth: true
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                        fontPixelSize: 12 * root.uiScale
                                        text: (repeatDaily ? I18n.t("每天") : I18n.t("仅一次")) + " · " +
                                              ringSeconds + I18n.t("秒") + " · " +
                                              I18n.t("再响") + " " + snoozeMinutes + I18n.t("分")
                                    }
                                }
                                Switch {
                                    id: alarmRowSwitch
                                    implicitHeight: Theme.hit
                                    Binding { target: alarmRowSwitch; property: "checked"; value: alarmEnabled }
                                    onClicked: root.alarmEngine.setEnabled(index, !alarmEnabled)
                                    Accessible.name: I18n.t("启用闹钟")
                                }
                                IconButton {
                                    iconName: "delete"
                                    label: I18n.t("删除闹钟")
                                    onClicked: root.alarmEngine.removeAlarm(index)
                                }
                            }
                        }
                    }

                    SettingsGroup {
                        Layout.fillWidth: true
                        visible: root.alarmEngine === null
                        iconName: "alarm"
                        accentColor: "#ff6584"
                        title: I18n.t("闹钟不可用")
                        subtitle: I18n.t("闹钟引擎未连接")
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.gap }
        }
    }

    // 铃声选择：页面内覆盖层，复用 SystemTools.files 的目录枚举；
    // 不复制、不修改宿主的文件管理器代码。
    Rectangle {
        id: ringtoneLayer
        objectName: "ringtonePicker"
        anchors.fill: parent
        z: 60
        visible: root.ringtonePickerOpen
        color: "#99040b14"
        MouseArea { anchors.fill: parent; onClicked: root.ringtonePickerOpen = false }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.gap * 4, 640 * root.uiScale)
            height: Math.min(parent.height - Theme.gap * 4, 520 * root.uiScale)
            radius: Theme.shapeLarge
            color: Theme.surface
            border.color: Theme.outline
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.gap
                spacing: Theme.gap / 2
                RowLayout {
                    Layout.fillWidth: true
                    CarText { heading: true; Layout.fillWidth: true; maximumLineCount: 1; text: I18n.t("选择铃声") }
                    IconButton { iconName: "close"; label: I18n.t("关闭"); onClicked: root.ringtonePickerOpen = false }
                }
                RowLayout {
                    Layout.fillWidth: true
                    CarButton {
                        text: I18n.t("上一级")
                        enabled: root.ringtoneFolder !== ""
                        onClicked: {
                            const cut = root.ringtoneFolder.lastIndexOf("/")
                            root.ringtoneFolder = cut > 0 ? root.ringtoneFolder.slice(0, cut) : ""
                            root.refreshRingtoneRows()
                        }
                    }
                    CarText {
                        Layout.fillWidth: true
                        maximumLineCount: 1
                        elide: Text.ElideMiddle
                        fontPixelSize: 12 * root.uiScale
                        text: "/" + root.ringtoneFolder
                    }
                }
                CarText {
                    Layout.fillWidth: true
                    visible: root.ringtoneRows.length === 0
                    subdued: true
                    text: I18n.t("这个目录下没有可用的音频文件")
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.ringtoneRows
                    delegate: Item {
                        required property var modelData
                        width: ListView.view.width
                        height: Math.max(48 * root.uiScale, 48)
                        RowLayout {
                            anchors.fill: parent
                            spacing: Theme.gap / 2
                            MaterialIcon {
                                name: modelData.directory ? "folder" : "volume_up"
                                Layout.preferredWidth: 22 * root.uiScale
                                Layout.preferredHeight: 22 * root.uiScale
                            }
                            CarText {
                                Layout.fillWidth: true
                                maximumLineCount: 1
                                elide: Text.ElideMiddle
                                text: modelData.name
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.openRingtoneEntry(modelData)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap / 2
                    CarText {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        fontPixelSize: 12 * root.uiScale
                        text: I18n.t("支持 mp3 / wav / m4a / ogg；只列出这些格式，且受文件管理沙箱限制。")
                    }
                    CarButton {
                        objectName: "ringtoneUseDefault"
                        text: I18n.t("使用默认铃声")
                        onClicked: {
                            root.alarmRingtone = root.alarmEngine ? root.alarmEngine.defaultRingtone : ""
                            root.ringtonePickerOpen = false
                        }
                    }
                }
            }
        }
    }

    // 校时失败提示：页面内覆盖层（不新建 Window/Popup 层级），点任意处关闭。
    Rectangle {
        id: apiAlertLayer
        objectName: "timeApiAlert"
        anchors.fill: parent
        z: 50
        visible: root.apiAlertText !== ""
        color: "#99040b14"
        MouseArea { anchors.fill: parent; onClicked: root.apiAlertText = "" }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.gap * 4, 560 * root.uiScale)
            height: alertColumn.implicitHeight + Theme.gap * 2
            radius: Theme.shapeLarge
            color: Theme.surface
            border.color: Theme.outline
            border.width: 1
            ColumnLayout {
                id: alertColumn
                x: Theme.gap
                y: Theme.gap
                width: parent.width - Theme.gap * 2
                spacing: Theme.gap / 2
                CarText {
                    Layout.fillWidth: true
                    heading: true
                    maximumLineCount: 1
                    text: I18n.t("校时接口不可用")
                }
                CarText {
                    objectName: "timeApiAlertText"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: root.apiAlertText
                }
                CarButton {
                    objectName: "timeApiAlertClose"
                    Layout.alignment: Qt.AlignRight
                    text: I18n.t("知道了")
                    onClicked: root.apiAlertText = ""
                }
            }
        }
    }

    // 可复用的数值步进器：48px 加减按钮 + 大数字，触屏优先。
    // 圆角数字框：数字与 ± 同框；点数字聚焦并请求内置键盘。
    component StepperBox: Rectangle {
        id: box
        property int value: 0
        property int maximum: 59
        property string caption: ""
        readonly property string paddedValue: value < 10 ? "0" + value : String(value)
        signal adjusted(int delta)
        function applyText(raw) {
            const digits = String(raw).replace(/[^0-9]/g, "")
            if (digits === "") return
            value = TimeMath.clamp(Number(digits), 0, maximum)
        }
        implicitHeight: Math.max(56 * root.uiScale, stepperRow.implicitHeight + 4 * root.uiScale)
        implicitWidth: stepperRow.implicitWidth + 8 * root.uiScale
        radius: Theme.shapeLarge
        color: Theme.surfaceContainerHigh
        border.width: 1
        border.color: Theme.outlineVariant
        RowLayout {
            id: stepperRow
            anchors.centerIn: parent
            spacing: 2 * root.uiScale
            IconButton {
                iconName: "remove"
                label: I18n.t("减少") + " " + box.caption
                onClicked: box.adjusted(-1)
            }
            TextInput {
                id: stepperInput
                objectName: "timeNumberInput"
                Layout.preferredWidth: 72 * root.uiScale
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                inputMethodHints: Qt.ImhDigitsOnly
                font.pointSize: Theme.title * 1.4
                color: Theme.foreground
                validator: IntValidator { bottom: 0; top: box.maximum }
                text: box.paddedValue
                onTextEdited: box.applyText(text)
                onEditingFinished: { box.applyText(text); text = box.paddedValue }
                Accessible.name: box.caption + " " + box.value
                // 只在“点数字”时请求键盘：跟随焦点变化会在键盘关闭、焦点回弹时
                // 再次触发，表现为键盘关了又开、关不掉。
                MouseArea {
                    anchors.fill: parent
                    onClicked: { stepperInput.forceActiveFocus(); root.keyboardRequested() }
                }
            }
            IconButton {
                iconName: "add"
                label: I18n.t("增加") + " " + box.caption
                onClicked: box.adjusted(1)
            }
        }
    }

    component NumberStepper: ColumnLayout {
        id: stepper
        property string caption: ""
        property int value: 0
        property int maximum: 59
        signal adjusted(int delta)
        spacing: 4 * root.uiScale
        CarText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 1
            subdued: true
            text: stepper.caption
        }
        StepperBox {
            Layout.alignment: Qt.AlignHCenter
            value: stepper.value
            maximum: stepper.maximum
            caption: stepper.caption
            onAdjusted: function(delta) { stepper.adjusted(delta) }
        }
    }

    // 指针表盘：纯 Rectangle 拼装，不引入 Canvas/着色器，1 Hz 更新代价可忽略。
    component AnalogClock: Item {
        id: face
        property int hours: 0
        property int minutes: 0
        property int seconds: 0
        implicitWidth: height
        width: height
        readonly property real radius: width / 2
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.surfaceContainerHigh
            border.width: 2
            border.color: Theme.outlineVariant
        }
        Repeater {
            model: 12
            delegate: Rectangle {
                required property int index
                width: 2 * root.uiScale
                height: 7 * root.uiScale
                radius: width / 2
                color: Theme.outline
                x: face.radius + (face.radius - 10 * root.uiScale) * Math.sin(index * Math.PI / 6) - width / 2
                y: face.radius - (face.radius - 10 * root.uiScale) * Math.cos(index * Math.PI / 6) - height / 2
                rotation: index * 30
            }
        }
        Rectangle {
            width: 5 * root.uiScale
            height: face.radius * 0.5
            radius: width / 2
            color: Theme.foreground
            x: face.radius - width / 2
            y: face.radius - height
            transformOrigin: Item.Bottom
            rotation: (face.hours % 12) * 30 + face.minutes * 0.5
        }
        Rectangle {
            width: 4 * root.uiScale
            height: face.radius * 0.72
            radius: width / 2
            color: Theme.foreground
            x: face.radius - width / 2
            y: face.radius - height
            transformOrigin: Item.Bottom
            rotation: face.minutes * 6 + face.seconds * 0.1
        }
        Rectangle {
            width: 2 * root.uiScale
            height: face.radius * 0.8
            radius: width / 2
            color: Theme.accent
            x: face.radius - width / 2
            y: face.radius - height
            transformOrigin: Item.Bottom
            rotation: face.seconds * 6
        }
        Rectangle {
            width: 10 * root.uiScale
            height: width
            radius: width / 2
            color: Theme.accent
            anchors.centerIn: parent
        }
    }
}
