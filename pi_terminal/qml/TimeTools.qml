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
// - 世界时钟的城市选择与偏移归 TimeSync.qml（同样在 Main 实例化）。时钟页只显示
//   列表与「管理城市」；校时入口与整点报时开关都在设置页的「时间与报时」分区，
//   这样双栏右栏不必再挤进半屏校时控件。
// - 闹钟、计时器预设、时钟样式通过 SystemTools 的 "time/" 前缀设置持久化；
//   秒表读数与计次刻意不持久化（恢复一个“还在跑”的秒表只会误导）。
// - 内置时区表不含夏令时规则，界面用一行说明如实标注，不假装准确。
Item {
    id: root
    objectName: "timeToolsPage"

    property bool active: false
    property real uiScale: 1
    // 表盘配色偏好（设置页「时间与报时」里选）：auto / light / dark。
    property string clockFace: "auto"
    // 时间格式：24 = 24 小时制，12 = 上午/下午。
    property string timeFormat: "24"
    readonly property bool clockAfternoon: TimeMath.isAfternoon(root.clockHour)

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

    // 默认就是双栏（类平板 UI）：只有真的窄到放不下才退回单栏（左栏在上、整页滚动）。
    // 断点 = 760 * uiScale，严格按作者唯一的缩放边界换算（autoScale ∈ [0.65, 1.4] ×
    // 界面比例 ∈ {1, 1.25, 1.5}，两者相乘就是 Theme.scaleFactor）：
    // 760 是 1.0 倍下的设计宽度，缩放后按同一比例要求可用宽度——于是各档位的相对容量
    // 一致，不会出现"缩小时反而更容易变单栏"。真机视口 ≈ 800 @ 0.75 需要 570，是双栏。
    // 不看高度：短屏同样是两栏，正文放不下就在卡片内部滚动。
    readonly property bool twoPane: width >= 760 * uiScale
    // 左栏下限跟着可用宽度走：原先固定 340 的下限在 560px 页面里会把右栏挤到 200px，
    // 右栏内容（管理城市、城市钟）直接被裁。宽屏仍是 clamp(340, 42%, 460)。
    readonly property real leftPaneWidth: Math.max(240 * uiScale, Math.min(460, width * 0.42))
    // 世界时钟卡片的可用宽度：由页面宽度推算，而不是去读网格自己的 width——
    // 「列数依赖自身宽度」的写法会让 GridLayout 与正文 ColumnLayout 互相推，
    // 首帧被 Qt 中止在两次迭代后，城市格子就会挤成一团。
    readonly property real worldCardWidth: Math.max(1, twoPane
                                                   ? width - leftPaneWidth - Theme.gap
                                                   : width)

    // 城市选择、同步到的偏移与校时线路状态都归 TimeSync.qml（Main 实例化一次），
    // 设置页的「时间与报时」分区与这里看到的是同一份状态；本页面只做显示与操作。
    property var timeSync: null
    readonly property var cities: timeSync ? timeSync.cities : []
    readonly property var cityCatalog: timeSync ? timeSync.catalog : []
    readonly property int maxCities: timeSync ? timeSync.maxCities : 8
    property bool editingCities: false
    // 城市数量到顶时就地提示一句，不弹全局层。
    property string cityNotice: ""
    readonly property var zoneOffsets: cities.map(function(city) {
        return timeSync ? timeSync.offsetOf(city.id) : city.offset
    })

    function toggleCity(id) {
        if (!timeSync) return
        const wasSelected = timeSync.hasZone(id)
        if (timeSync.toggleCity(id)) { cityNotice = ""; return }
        if (!wasSelected && timeSync.zones.length >= maxCities)
            cityNotice = I18n.t("最多 8 个城市，先取消一个再添加。")
    }
    readonly property var weekdayNames: ["星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"]

    // ------------------------------------------------------------------ clock

    property string clockTime: "00:00:00"
    property string clockDate: ""
    property string clockWeekday: ""
    property int localOffset: 0
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

    // ------------------------------------------------------------ 持久化设置

    property double timerSavedAt: 0
    // 初值 true：构造期的初值赋值不能先覆盖磁盘上的设置，见 TimeSync.qml。
    property bool loadingSettings: true
    // 删除时收干净编辑态：删的就是正在编辑那条 → 回新建态；删的是它前面的 →
    // 编号前移一位，继续编辑同一条。接引擎信号而不是 ListModel 的 countChanged，
    // 因为布局里的非可视 Connections 不可靠。
    Connections {
        target: root.alarmEngine
        function onAlarmRemoved(index) {
            if (!root.editingAlarm) return
            if (index === root.editingAlarmIndex) root.resetAlarmForm()
            else if (root.editingAlarmIndex > index) root.editingAlarmIndex -= 1
        }
    }

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
        loadingSettings = false
    }

    function persistTimerPreset() {
        writeSetting("time/timer-preset", timerHours + "," + timerMinutes + "," + timerSeconds)
    }

    function scheduleTimerPresetSave() {
        timerSavedAt = Date.now()
        timerSaveTimer.restart()
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
    // 自选铃声的 15 秒片段起点（毫秒，5 秒步进）；内置铃声是现成的 15 秒节选，起点恒为 0。
    property int alarmRingtoneStartMs: 0
    property int alarmRingSeconds: 30
    property int alarmSnoozeMinutes: 5
    property bool ringtonePickerOpen: false
    // -1 = 新建；>=0 = 正在编辑「已设置闹钟」里的第几条（点行回填）。
    property int editingAlarmIndex: -1
    readonly property bool editingAlarm: editingAlarmIndex >= 0
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

    // 轮盘/预设改过时/分/秒后：没在跑就按新时长复位，并合并写入设置。
    onTimerHoursChanged: root.timerPartsEdited()
    onTimerMinutesChanged: root.timerPartsEdited()
    onTimerSecondsChanged: root.timerPartsEdited()

    function timerPartsEdited() {
        if (loadingSettings) return
        if (alarmEngine && !timerRunning) alarmEngine.resetTimer(timerTotalMs)
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
        // 计次不再设上限：列表在卡片内滚动，留多少条由用户决定。
        stopwatchLaps = stopwatchLaps.concat([stopwatchElapsedMs])
    }

    function lapSplit(index) {
        if (index <= 0) return stopwatchLaps[0]
        return stopwatchLaps[index] - stopwatchLaps[index - 1]
    }

    // ---------------------------------------------------------------- 闹钟表单

    // 点右侧某一行：把该条回填到左栏，标题与按钮切到编辑态。
    function beginEditAlarm(index) {
        if (!alarmEngine || index < 0 || index >= alarmEngine.alarms.count) return
        const record = alarmEngine.alarms.get(index)
        alarmHour = record.hour
        alarmMinute = record.minute
        alarmRepeatDaily = record.repeatDaily
        alarmName = record.name
        alarmNameTouched = true
        alarmRingtone = record.ringtone
        alarmRingtoneStartMs = record.ringtoneStartMs || 0
        alarmRingSeconds = record.ringSeconds
        alarmSnoozeMinutes = record.snoozeMinutes
        editingAlarmIndex = index
    }

    function resetAlarmForm() {
        editingAlarmIndex = -1
        alarmHour = 7
        alarmMinute = 30
        alarmRepeatDaily = true
        alarmName = ""
        alarmNameTouched = false
        alarmRingSeconds = 30
        alarmSnoozeMinutes = 5
        alarmRingtoneStartMs = 0
        if (alarmEngine) {
            alarmEngine.stopPreview()
            alarmRingtone = alarmEngine.defaultRingtone
        }
    }

    function cancelAlarmEdit() {
        resetAlarmForm()
    }

    function saveAlarm() {
        if (!alarmEngine) return
        const record = {
            hour: alarmHour,
            minute: alarmMinute,
            repeatDaily: alarmRepeatDaily,
            name: alarmName.trim(),
            ringtone: alarmRingtone,
            ringtoneStartMs: alarmRingtoneStartMs,
            ringSeconds: alarmRingSeconds,
            snoozeMinutes: alarmSnoozeMinutes
        }
        if (editingAlarm) {
            if (alarmEngine.updateAlarm(editingAlarmIndex, record)) resetAlarmForm()
            else notifyRequested(I18n.t("闹钟"), I18n.t("已有同名同时刻的闹钟，未保存"), false)
            return
        }
        addAlarmFromForm()
    }

    function addAlarmFromForm() {
        if (!alarmEngine) return
        const ok = alarmEngine.addAlarm({
            hour: alarmHour,
            minute: alarmMinute,
            repeatDaily: alarmRepeatDaily,
            name: alarmName.trim(),
            ringtone: alarmRingtone,
            ringtoneStartMs: alarmRingtoneStartMs,
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
    Component.onCompleted: { root.loadSettings(); root.tick() }

    Flickable {
        id: flick
        objectName: "timePage"
        anchors.fill: parent
        contentWidth: width
        contentHeight: root.twoPane ? height : column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        clip: true

        ColumnLayout {
            id: column
            width: flick.width
            // 双栏：整列正好等于视口，页面不整体滚动，溢出交给卡片内部 Flickable。
            // 单栏：按内容高度，页面整体滚动。
            // 这里若取 max(视口, 内容高)，双栏列会比视口更高，而被 clip 的页面又不滚，
            // 末行内容（世界时钟的管理城市按钮）就永远看不到——曾卡住的 bug。
            height: root.twoPane ? flick.height : implicitHeight
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
                Layout.fillHeight: root.twoPane
                // 双栏必须允许 Loader 被压到可用行高，否则它的隐式高度（=整页内容高）
                // 会被当成最小高度，挤不下就把卡片顶出视口。
                Layout.minimumHeight: root.twoPane ? 0 : implicitHeight
                sourceComponent: root.tab === root.tabClock ? clockPage
                               : root.tab === root.tabTimer ? timerPage
                               : root.tab === root.tabStopwatch ? stopwatchPage : alarmPage
            }
            Component {
                id: clockPage
                GridLayout {
                    objectName: "timeClockTab"
                    width: tabLoader.width
                    height: root.twoPane ? tabLoader.height : implicitHeight
                    columns: root.twoPane ? 2 : 1
                    columnSpacing: Theme.gap
                    rowSpacing: Theme.gap

            TimePane {
                objectName: "clockLocalPane"
                Layout.fillWidth: !root.twoPane
                Layout.preferredWidth: root.twoPane ? root.leftPaneWidth : -1
                Layout.fillHeight: root.twoPane
                scrollBody: root.twoPane
                iconName: "schedule"
                accentColor: "#8f7bff"
                title: I18n.t("中国时间")
                subtitle: I18n.t("系统时钟") + " · " + TimeMath.formatOffset(root.localOffset)

                // 表盘、数字钟、日期直接作为正文列的居中对齐子项，不再套一层
                // "Item + 锚定 Column"：那种写法里父项高度来自子项隐式高度、子项又
                // 跟随父项宽度换行，首帧容易互相推（Qt 报 polish loop）。
                AnalogClock {
                    objectName: "analogClock"
                    Layout.alignment: Qt.AlignHCenter
                    // 布局子项必须用 Layout.preferredHeight：直接写 height 会被布局覆盖成 0，
                    // 而 AnalogClock 是 width = height 的方盘，结果就是「表盘缩成一个点」。
                    Layout.preferredHeight: root.height < 420
                                            ? Math.max(56 * root.uiScale, root.height * 0.16)
                                            : 112 * root.uiScale
                    hours: root.clockHour
                    minutes: root.clockMinute
                    seconds: root.clockSecond
                }
                Row {
                    id: digitalClockRow
                    Layout.alignment: Qt.AlignHCenter
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
                                    text: index === 0
                                          ? (root.timeFormat === "12"
                                             ? String(TimeMath.hour12(root.clockHour))
                                             : root.clockTime.substring(0, 2))
                                        : index === 1 ? root.clockTime.substring(3, 5)
                                        : root.clockTime.substring(6, 8)
                                    Accessible.name: index === 0 ? I18n.t("时")
                                                     : index === 1 ? I18n.t("分") : I18n.t("秒")
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
                CarText {
                    Layout.alignment: Qt.AlignHCenter
                    maximumLineCount: 1
                    // 12 小时制时把 上午/下午 放在日期行前面，不挤数字钟的宽度。
                    text: (root.timeFormat === "12"
                           ? (root.clockAfternoon ? I18n.t("下午") : I18n.t("上午")) + "  " : "")
                          + root.clockDate + "  " + root.clockWeekday
                }
                // 报时按钮：原来整卡可点，但热区压在样式按钮上面，点一下只会播报。
                // 现在是表盘旁的 48px 小喇叭按钮。
                IconButton {
                    objectName: "clockAnnounce"
                    Layout.alignment: Qt.AlignRight
                    iconName: "volume_up"
                    label: I18n.t("报时")
                    filled: true
                    accentColor: "#8f7bff"
                    onClicked: root.announceRequested(
                        I18n.t("中国时间") + " " + root.clockTime + " " +
                        TimeMath.formatOffset(root.localOffset))
                }
            }

            TimePane {
                objectName: "clockWorldPane"
                Layout.fillWidth: true
                Layout.fillHeight: root.twoPane
                scrollBody: root.twoPane
                iconName: "public"
                accentColor: "#4fd4a4"
                title: I18n.t("世界时钟")
                subtitle: I18n.t("各城市当前时间")

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.timeSync !== null
                    spacing: Theme.gap / 2
                    CarText {
                        Layout.fillWidth: true
                        maximumLineCount: 1
                        fontPixelSize: Theme.fontStrong
                        text: I18n.t("城市") + " " + root.cities.length + "/" + root.maxCities
                    }
                    CarButton {
                        objectName: "timeCityEdit"
                        text: root.editingCities ? I18n.t("完成") : I18n.t("管理城市")
                        checked: root.editingCities
                        onClicked: root.editingCities = !root.editingCities
                    }
                }
                CarText {
                    objectName: "worldClockHint"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    subdued: true
                    text: I18n.t("夏令时未知 · 校时见设置")
                }
                CarText {
                    objectName: "worldClockUnavailable"
                    Layout.fillWidth: true
                    visible: root.timeSync === null
                    wrapMode: Text.Wrap
                    text: I18n.t("世界时钟不可用：校时模块未连接。")
                }
                // 每个城市一块：指针小时钟在上、城市名在下，下面再跟当天的 UTC 偏移。
                GridLayout {
                    objectName: "worldClockGrid"
                    Layout.fillWidth: true
                    columns: (root.worldCardWidth - Theme.gap * 2) < 360 * root.uiScale ? 2 : 3
                    columnSpacing: Theme.gap / 2
                    rowSpacing: Theme.gap / 2
                    Repeater {
                        model: root.cities
                        delegate: ColumnLayout {
                            id: cityTile
                            required property var modelData
                            required property int index
                            readonly property var parts:
                                TimeMath.cityParts(root.cityTickMs, root.zoneOffsets[index])
                            spacing: 2 * root.uiScale
                            AnalogClock {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredHeight: Math.min(80 * root.uiScale, 76)
                                hours: cityTile.parts.hour
                                minutes: cityTile.parts.minute
                                seconds: cityTile.parts.second
                            }
                            CarText {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                maximumLineCount: 1
                                elide: Text.ElideRight
                                fontPixelSize: Theme.fontLabel
                                text: I18n.t(cityTile.modelData.label)
                            }
                            CarText {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                maximumLineCount: 1
                                subdued: true
                                fontPixelSize: Theme.fontCaption
                                text: root.timeFormat === "12"
                                      ? TimeMath.formatHm12(cityTile.parts) + " " +
                                        (TimeMath.isAfternoon(cityTile.parts.hour)
                                         ? I18n.t("下午") : I18n.t("上午"))
                                      : TimeMath.formatHm(cityTile.parts)
                            }
                        }
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
                            checked: root.timeSync ? root.timeSync.hasZone(modelData.id) : false
                            onClicked: root.toggleCity(modelData.id)
                            Accessible.name: I18n.t(modelData.label) + " " +
                                (checked ? I18n.t("已添加") : I18n.t("未添加"))
                        }
                    }
                }
                CarText {
                    objectName: "timeCityNotice"
                    Layout.fillWidth: true
                    visible: root.cityNotice !== ""
                    wrapMode: Text.Wrap
                    text: root.cityNotice
                }
            }
            }
            }
            // ---- 计时器
            Component {
                id: timerPage
                GridLayout {
                    objectName: "timeTimerTab"
                    width: tabLoader.width
                    height: root.twoPane ? tabLoader.height : implicitHeight
                    columns: root.twoPane ? 2 : 1
                    columnSpacing: Theme.gap
                    rowSpacing: Theme.gap
            TimePane {
                Layout.fillWidth: !root.twoPane
                Layout.preferredWidth: root.twoPane ? root.leftPaneWidth : -1
                Layout.fillHeight: root.twoPane
                scrollBody: root.twoPane
                objectName: "timerInputPane"
                iconName: "hourglass_empty"
                accentColor: "#ffbd54"
                title: I18n.t("计时器")
                subtitle: I18n.t("到点后显示通知横幅并语音播报")

                // 固定三列而不是 Flow：Flow 的高度依赖宽度，在嵌套 ColumnLayout 里会
                // 触发 polish 循环（实机 log 可复现），网格布局行为确定。
                GridLayout {
                    Layout.fillWidth: true
                    columns: 6      // 一行 6 个、只到 48% 高度；给下面的轮盘让出空间
                    columnSpacing: Theme.gap / 2
                    rowSpacing: Theme.gap / 2
                    Repeater {
                        model: [1, 3, 5, 10, 15, 30]
                        delegate: CarButton {
                            required property int modelData
                            Layout.fillWidth: true
                            implicitHeight: Math.max(38 * root.uiScale, 38)
                            fontPixelSize: Theme.fontLabel
                            topPadding: 0
                            bottomPadding: 0
                            leftPadding: Theme.gap * 0.4
                            rightPadding: Theme.gap * 0.4
                            text: modelData + I18n.t("分")
                            checked: root.timerPresetMinutes === modelData && !root.timerRunning
                            onClicked: root.applyPreset(modelData)
                        }
                    }
                }

                // iOS 式轮盘：时/分/秒 三个 Tumbler 并排一行，每个上方标注单位。
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap / 2
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        CarText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            maximumLineCount: 1
                            subdued: true
                            fontPixelSize: Theme.fontLabel
                            text: I18n.t("时")
                        }
                        Wheel {
                            objectName: "timerHourWheel"
                            Layout.fillWidth: true
                            caption: I18n.t("时")
                            maximum: 23
                            value: root.timerHours
                            onScrolled: function(value) { root.timerHours = value }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        CarText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            maximumLineCount: 1
                            subdued: true
                            fontPixelSize: Theme.fontLabel
                            text: I18n.t("分")
                        }
                        Wheel {
                            objectName: "timerMinuteWheel"
                            Layout.fillWidth: true
                            caption: I18n.t("分")
                            maximum: 59
                            value: root.timerMinutes
                            onScrolled: function(value) { root.timerMinutes = value }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        CarText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            maximumLineCount: 1
                            subdued: true
                            fontPixelSize: Theme.fontLabel
                            text: I18n.t("秒")
                        }
                        Wheel {
                            objectName: "timerSecondWheel"
                            Layout.fillWidth: true
                            caption: I18n.t("秒")
                            maximum: 59
                            value: root.timerSeconds
                            onScrolled: function(value) { root.timerSeconds = value }
                        }
                    }
                }

            }
            TimePane {
                Layout.fillWidth: true
                Layout.fillHeight: root.twoPane
                scrollBody: root.twoPane
                objectName: "timerRunPane"
                iconName: "av_timer"
                accentColor: "#ff9d37"
                title: I18n.t("倒计时")
                subtitle: root.timerRunning ? I18n.t("倒计时进行中")
                          : root.timerFinished ? I18n.t("计时结束")
                          : I18n.t("准备就绪")

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
                        fontPixelSize: Math.max(40, Math.min(64 * root.uiScale, root.width / 8))
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
                GridLayout {
                    objectName: "timeStopwatchTab"
                    width: tabLoader.width
                    height: root.twoPane ? tabLoader.height : implicitHeight
                    columns: root.twoPane ? 2 : 1
                    columnSpacing: Theme.gap
                    rowSpacing: Theme.gap
            TimePane {
                Layout.fillWidth: !root.twoPane
                Layout.preferredWidth: root.twoPane ? root.leftPaneWidth : -1
                Layout.fillHeight: root.twoPane
                scrollBody: root.twoPane
                objectName: "stopwatchRunPane"
                iconName: "av_timer"
                accentColor: "#36c8db"
                title: I18n.t("秒表")
                subtitle: ""

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
            }
            TimePane {
                Layout.fillWidth: true
                Layout.fillHeight: root.twoPane
                scrollBody: root.twoPane
                objectName: "stopwatchLapPane"
                iconName: "schedule"
                accentColor: "#36c8db"
                title: I18n.t("计次")
                subtitle: ""

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
                                fontPixelSize: Theme.fontCaption
                                text: I18n.t("计次")
                            }
                            CarText {
                                Layout.fillWidth: true
                                maximumLineCount: 1
                                horizontalAlignment: Text.AlignRight
                                fontPixelSize: Theme.fontCaption
                                text: I18n.t("分段")
                            }
                            CarText {
                                Layout.preferredWidth: 120 * root.uiScale
                                maximumLineCount: 1
                                horizontalAlignment: Text.AlignRight
                                fontPixelSize: Theme.fontCaption
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
                GridLayout {
                    objectName: "timeAlarmTab"
                    width: tabLoader.width
                    height: root.twoPane ? tabLoader.height : implicitHeight
                    columns: root.twoPane ? 2 : 1
                    columnSpacing: Theme.gap
                    rowSpacing: Theme.gap

                    TimePane {
                        Layout.fillWidth: !root.twoPane
                        Layout.preferredWidth: root.twoPane ? root.leftPaneWidth : -1
                        Layout.fillHeight: root.twoPane
                        scrollBody: root.twoPane
                        visible: root.alarmEngine !== null
                        objectName: "alarmEditPane"
                        iconName: "alarm"
                        accentColor: "#ff6584"
                        title: root.editingAlarm ? I18n.t("编辑闹钟") : I18n.t("新建闹钟")
                        subtitle: root.editingAlarm
                                  ? I18n.t("保存会覆盖这一条，启用状态保持不变")
                                  : I18n.t("到点弹通知并语音播报；闹钟会保存到本机")

                        // 闹钟时间：一行两列（时 | 分），每列「标签在上、紧凑步进器在下」。
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap / 2
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                CarText {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    maximumLineCount: 1
                                    subdued: true
                                    fontPixelSize: Theme.fontCaption
                                    text: I18n.t("时")
                                }
                                StepperBox {
                                    objectName: "alarmHourStepper"
                                    Layout.alignment: Qt.AlignHCenter
                                    compact: true
                                    value: root.alarmHour
                                    maximum: 23
                                    caption: I18n.t("时")
                                    onAdjusted: function(delta) {
                                        root.alarmHour = (root.alarmHour + delta + 24) % 24
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                CarText {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    maximumLineCount: 1
                                    subdued: true
                                    fontPixelSize: Theme.fontCaption
                                    text: I18n.t("分")
                                }
                                StepperBox {
                                    objectName: "alarmMinuteStepper"
                                    Layout.alignment: Qt.AlignHCenter
                                    compact: true
                                    value: root.alarmMinute
                                    maximum: 59
                                    caption: I18n.t("分")
                                    onAdjusted: function(delta) {
                                        root.alarmMinute = (root.alarmMinute + delta + 60) % 60
                                    }
                                }
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

                        CarText {
                            objectName: "alarmRingtoneName"
                            Layout.fillWidth: true
                            maximumLineCount: 1
                            elide: Text.ElideMiddle
                            text: I18n.t("铃声") + " · " +
                                (root.alarmEngine ? root.alarmEngine.ringtoneLabel(root.alarmRingtone)
                                                  : I18n.t("未选择（仅语音播报）"))
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap / 2
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
                                onClicked: if (root.alarmEngine)
                                               root.alarmEngine.togglePreview(root.alarmRingtone,
                                                                              root.alarmRingtoneStartMs)
                            }
                            CarButton {
                                objectName: "alarmRingtoneButton"
                                text: I18n.t("选择铃声")
                                onClicked: root.openRingtonePicker()
                            }
                        }
                        // 自选铃声的 15 秒片段：起点以 5 秒步进，范围 0–5 分钟。
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap / 2
                            visible: root.alarmRingtone !== "" &&
                                     (!root.alarmEngine ||
                                      root.alarmRingtone !== root.alarmEngine.defaultRingtone)
                            CarText {
                                Layout.fillWidth: true
                                maximumLineCount: 1
                                text: I18n.t("片段起点") + " · " + I18n.t("15 秒")
                            }
                            IconButton {
                                objectName: "alarmClipBack"
                                iconName: "remove"
                                label: I18n.t("片段起点提前")
                                onClicked: root.alarmRingtoneStartMs =
                                    Math.max(0, root.alarmRingtoneStartMs - 5000)
                            }
                            CarText {
                                objectName: "alarmClipStartLabel"
                                heading: true
                                maximumLineCount: 1
                                horizontalAlignment: Text.AlignHCenter
                                Layout.preferredWidth: 76 * root.uiScale
                                text: root.alarmEngine
                                      ? root.alarmEngine.clipStartLabel(
                                            { ringtoneStartMs: root.alarmRingtoneStartMs }) : "00:00"
                            }
                            IconButton {
                                objectName: "alarmClipForward"
                                iconName: "add"
                                label: I18n.t("片段起点推后")
                                onClicked: root.alarmRingtoneStartMs =
                                    Math.min(300000, root.alarmRingtoneStartMs + 5000)
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
                                objectName: "alarmCancelEditButton"
                                visible: root.editingAlarm
                                text: I18n.t("取消")
                                onClicked: root.cancelAlarmEdit()
                            }
                            CarButton {
                                objectName: "alarmAddButton"
                                accent: true
                                text: root.editingAlarm ? I18n.t("保存修改") : I18n.t("添加闹钟")
                                onClicked: root.saveAlarm()
                            }
                        }
                    }

                    TimePane {
                        Layout.fillWidth: true
                        Layout.fillHeight: root.twoPane
                        scrollBody: root.twoPane
                        visible: root.alarmEngine !== null
                        objectName: "alarmListPane"
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
                            delegate: Item {
                                id: alarmRow
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

                                // 点整行回填到左栏编辑。MouseArea 垫在下面（z 最低），
                                // 开关与删除按钮盖子上面，仍然各自响应自己的点击。
                                MouseArea {
                                    objectName: "alarmRowTap" + alarmRow.index
                                    anchors.fill: parent
                                    z: -1
                                    acceptedButtons: Qt.LeftButton
                                    onClicked: root.beginEditAlarm(alarmRow.index)
                                    Accessible.name: I18n.t("编辑这个闹钟")
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: Theme.gap
                                    Rectangle {
                                        Layout.preferredWidth: 104 * root.uiScale
                                        implicitHeight: 44 * root.uiScale
                                        radius: Theme.shapeMedium
                                        color: Theme.surfaceContainerHigh
                                        border.width: 1
                                        border.color: alarmRow.index === root.editingAlarmIndex
                                                      ? Theme.accent : Theme.outlineVariant
                                        CarText {
                                            anchors.centerIn: parent
                                            maximumLineCount: 1
                                            fontPixelSize: Theme.fontStrong
                                            text: TimeMath.alarmTimeLabel(
                                                      { hour: alarmRow.hour, minute: alarmRow.minute })
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        CarText {
                                            Layout.fillWidth: true
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                            text: alarmRow.name !== "" ? alarmRow.name : I18n.t("闹钟")
                                        }
                                        CarText {
                                            Layout.fillWidth: true
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                            fontPixelSize: Theme.fontCaption
                                            text: (alarmRow.repeatDaily ? I18n.t("每天") : I18n.t("仅一次")) + " · " +
                                                  alarmRow.ringSeconds + I18n.t("秒") + " · " +
                                                  I18n.t("再响") + " " + alarmRow.snoozeMinutes + I18n.t("分")
                                        }
                                    }
                                    Switch {
                                        id: alarmRowSwitch
                                        implicitHeight: Theme.hit
                                        Accessible.name: I18n.t("启用闹钟")
                                        Binding {
                                            target: alarmRowSwitch
                                            property: "checked"
                                            value: alarmRow.alarmEnabled
                                        }
                                        onClicked: root.alarmEngine.setEnabled(alarmRow.index,
                                                                                !alarmRow.alarmEnabled)
                                    }
                                    IconButton {
                                        iconName: "delete"
                                        label: I18n.t("删除闹钟")
                                        onClicked: root.alarmEngine.removeAlarm(alarmRow.index)
                                    }
                                }
                            }
                        }
                    }

                    TimePane {
                        objectName: "alarmUnavailablePane"
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
    // 由 Loader 延迟实例化：覆盖层即使 visible=false 也会参与布局（内部有 ListView +
    // Layout.fillHeight），页面启动初期只有几十像素时会让 ColumnLayout 反复 polish，
    // 而 ui_responsive_touch 的 UI_AUDIT 把这条警告算作错误。
    Loader {
        id: ringtoneLayer
        anchors.fill: parent
        z: 60
        active: root.ringtonePickerOpen
        sourceComponent: ringtoneLayerComponent
    }
    Component {
        id: ringtoneLayerComponent
        Rectangle {
            objectName: "ringtonePicker"
            anchors.fill: parent
            color: "#99040b14"
            MouseArea {
                anchors.fill: parent
                onClicked: root.ringtonePickerOpen = false
            }
            Rectangle {
                anchors.centerIn: parent
                // 页面可能被压到只有几十像素，给尺寸下限，保证内框不会变成负数。
                width: Math.max(240 * root.uiScale, Math.min(parent.width - Theme.gap * 4, 640 * root.uiScale))
                height: Math.max(200 * root.uiScale, Math.min(parent.height - Theme.gap * 4, 520 * root.uiScale))
                radius: Theme.shapeLarge
                color: Theme.surface
                border.color: Theme.outline
                border.width: 1
                ColumnLayout {
                    // 显式尺寸 + clamp，代替 anchors.margins：负的内框尺寸同样会触发 polish。
                    x: Theme.gap
                    y: Theme.gap
                    width: Math.max(1, parent.width - Theme.gap * 2)
                    height: Math.max(1, parent.height - Theme.gap * 2)
                    spacing: Theme.gap / 2
                    RowLayout {
                        Layout.fillWidth: true
                        CarText {
                            heading: true
                            Layout.fillWidth: true
                            maximumLineCount: 1
                            text: I18n.t("选择铃声")
                        }
                        IconButton {
                            iconName: "close"
                            label: I18n.t("关闭")
                            onClicked: root.ringtonePickerOpen = false
                        }
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
                            fontPixelSize: Theme.fontCaption
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
                            fontPixelSize: Theme.fontCaption
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
    }

    // 可复用的数值步进器：48px 加减按钮 + 大数字，触屏优先。
    // 圆角数字框：数字与 ± 同框；点数字聚焦并请求内置键盘。
    component StepperBox: Rectangle {
        id: box
        property int value: 0
        property int maximum: 59
        property string caption: ""
        property bool compact: false      // 紧凑形态：按钮 42、数字框 48
        readonly property string paddedValue: value < 10 ? "0" + value : String(value)
        signal adjusted(int delta)
        function applyText(raw) {
            const digits = String(raw).replace(/[^0-9]/g, "")
            if (digits === "") return
            value = TimeMath.clamp(Number(digits), 0, maximum)
        }
        implicitHeight: Math.max((box.compact ? 44 : 56) * root.uiScale,
                                 stepperRow.implicitHeight + 4 * root.uiScale)
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
                implicitWidth: box.compact ? 42 * root.uiScale : Theme.hit
                implicitHeight: implicitWidth
                onClicked: box.adjusted(-1)
            }
            TextInput {
                id: stepperInput
                objectName: "timeNumberInput"
                Layout.preferredWidth: (box.compact ? 48 : 72) * root.uiScale
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
                implicitWidth: box.compact ? 42 * root.uiScale : Theme.hit
                implicitHeight: implicitWidth
                onClicked: box.adjusted(1)
            }
        }
    }

    // 类 iOS 的数值轮盘：三行可见、两端环绕，选中项最亮，中间一条选择带。
    // 用 QtQuick.Controls 的 Tumbler，不引入第三方库，也不自绘惯性滚动。
    component Wheel: Tumbler {
        id: wheel
        property int maximum: 59
        property int value: 0
        property string caption: ""
        signal scrolled(int value)
        readonly property real itemHeight: Math.max(40 * root.uiScale, 40)
        Layout.preferredHeight: 3 * itemHeight
        visibleItemCount: 3
        wrap: true
        model: maximum + 1
        // 初值走绑定，用户滚动后绑定会断，再由 onValueChanged 补上程序性变化。
        currentIndex: value
        // 轮盘首次布局时会自己滚一下（currentIndex 抖动），那不是用户操作，
        // 不能回写模型——否则预设 5 分钟会被改成 3 分钟。
        property bool ready: false
        Component.onCompleted: currentIndex = value
        Timer {
            interval: 600
            running: true
            onTriggered: wheel.ready = true
        }
        onValueChanged: if (currentIndex !== value) currentIndex = value
        // 只有用户真的在滚（moving/flicking）才回写模型：首次布局的自动居中、
        // 卡片尺寸变化引起的重新居中都不是用户操作，回写会把 5 分钟改成乱值。
        onCurrentIndexChanged: if (wheel.ready && (wheel.moving || wheel.flicking))
                                   wheel.scrolled(currentIndex)
        background: Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: (parent.height - wheel.itemHeight) / 2
            width: parent.width
            height: wheel.itemHeight
            radius: Theme.shapeSmall
            color: Theme.surfaceContainerHigh
        }
        delegate: CarText {
            required property int index
            width: wheel.width
            height: wheel.itemHeight
            text: (index < 10 ? "0" : "") + index
            fontPixelSize: Theme.fontWheel
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            // 距离中心越远越淡，模拟轮盘的纵深感。
            opacity: 1 - Math.min(1, Math.abs(Tumbler.displacement) / 1.5) * 0.7
            Accessible.name: wheel.caption + " " + index
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
        // 表盘跟着当地时刻走：06:00–17:59 白天用浅色盘面 + 深色指针，
        // 其余时间用深色盘面 + 浅色指针。城市钟用各自城市的小时数。
        readonly property bool daytime: root.clockFace === "light" ? true
                                                      : root.clockFace === "dark" ? false
                                                      : (hours >= 6 && hours < 18)
        readonly property color faceColor: daytime ? "#f1f4f8" : "#0a1119"
        readonly property color inkColor: daytime ? "#101a26" : "#e8f1ff"
        readonly property color rimColor: daytime ? "#b9c6d4" : Theme.outlineVariant
        Rectangle {
            objectName: "analogFace"
            anchors.fill: parent
            radius: width / 2
            color: face.faceColor
            border.width: 2
            border.color: face.rimColor
            Behavior on color { ColorAnimation { duration: 400 } }
        }
        Repeater {
            model: 12
            delegate: Rectangle {
                required property int index
                width: 2 * root.uiScale
                height: 7 * root.uiScale
                radius: width / 2
                color: face.inkColor
                opacity: 0.75
                x: face.radius + (face.radius - 10 * root.uiScale) * Math.sin(index * Math.PI / 6) - width / 2
                y: face.radius - (face.radius - 10 * root.uiScale) * Math.cos(index * Math.PI / 6) - height / 2
                rotation: index * 30
            }
        }
        Rectangle {
            width: 5 * root.uiScale
            height: face.radius * 0.5
            radius: width / 2
            color: face.inkColor
            x: face.radius - width / 2
            y: face.radius - height
            transformOrigin: Item.Bottom
            rotation: (face.hours % 12) * 30 + face.minutes * 0.5
        }
        Rectangle {
            width: 4 * root.uiScale
            height: face.radius * 0.72
            radius: width / 2
            color: face.inkColor
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
