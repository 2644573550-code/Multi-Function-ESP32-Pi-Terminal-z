import QtQuick
import "TimeMath.js" as TimeMath

// 校时状态拥有者：在 Main.qml 实例化一次，设置页的「时间与报时」分区与时钟页的
// 世界时钟共用同一份状态（城市选择、同步到的偏移、线路状态）。
//
// 边界：
// - 只负责网络校时、时区偏移与城市选择；不渲染、不建通知层、不改主题。
// - 只有用户点「网络校时」才发请求。自动模式按 线路一 → 线路二 回退；自定义
//   模式严格只用填写的地址，失败不回退到公共接口。
// - 选中城市、接口模式与自定义地址都通过 SystemTools 的 "time/" 前缀持久化。
// - 世界时钟默认用内置标准时区表（IANA 标准偏移，不含夏令时规则），未校时的
//   城市如实按标准偏移显示，并由 UI 说明夏令时未知。
// - primaryTimeHost / fallbackTimeHost 是给测试替换接口地址的接缝，正常运行为空。
Item {
    id: sync

    readonly property var catalog: TimeMath.cityCatalog
    readonly property int maxCities: 8

    // 默认 6 个城市（北京/东京/新加坡/伦敦/纽约/洛杉矶），与时间页历史默认一致。
    property var zones: ["Asia/Shanghai", "Asia/Tokyo", "Asia/Singapore",
                         "Europe/London", "America/New_York", "America/Los_Angeles"]
    // 只记住同步成功的偏移；城市增删不会丢掉已经查到的时区。
    property var syncedOffsets: ({})

    property bool syncing: false
    property int syncedCount: 0
    property bool syncedOnce: false
    property string provider: ""
    property string error: ""
    property double skewMs: 0
    property string syncedAt: ""
    // 需要用户确认的一句提示（自定义地址格式不对等），由 UI 侧清除。
    property string alertText: ""

    property string primaryTimeHost: "https://worldtimeapi.org"
    property string fallbackTimeHost: "https://timeapi.io"
    property string apiMode: "auto"                  // "auto" | "custom"
    property string customHost: ""
    property var providerQueue: []
    property var providerState: ({})                 // id -> 0 未测 / 1 可用 / -1 不可用
    property int providerIndex: -1
    property int syncIndex: -1
    property int syncToken: 0
    property double baseUtcMs: 0
    property var xhr: null
    // 初值就是 true：QML 在 Component.onCompleted 之前就会把 zones/apiMode 的初值写进
    // 属性，若此时允许持久化，构造出的默认值会先覆盖磁盘上的真实设置，load() 再读回
    // 默认值——表现为“重启后城市/线路设置全丢”。load() 结束时才放开写入。
    property bool loading: true

    Timer {
        id: watchdog
        interval: 8000
        repeat: false
        onTriggered: sync.syncRequestFailed(sync.syncIndex, sync.syncToken, I18n.t("请求超时"))
    }

    Component.onCompleted: sync.load()

    // 界面只显示“线路一/线路二”，不暴露真实服务地址。
    readonly property var providerIds: apiMode === "custom" && customHost.trim() !== ""
        ? ["custom", "worldtimeapi", "timeapi"] : ["worldtimeapi", "timeapi"]

    readonly property var cities: {
        const list = []
        for (let i = 0; i < zones.length; ++i) {
            const entry = cityById(zones[i])
            if (entry) list.push(entry)
        }
        return list
    }

    readonly property string status: {
        if (alertText !== "") return alertText
        if (syncing) {
            const current = providerIndex >= 0 && providerIndex < providerQueue.length
                ? providerLabel(providerQueue[providerIndex].id) : ""
            return I18n.t("同步中") + " " + syncedCount + "/" + cities.length +
                (current === "" ? "" : " · " + current)
        }
        if (error !== "") return error
        if (syncedOnce) {
            return I18n.t("已校时") + " · " + provider + " · " + I18n.t("偏差") + " " +
                TimeMath.formatSkew(skewMs) + " · " + syncedAt
        }
        if (apiMode === "custom")
            return I18n.t("自定义模式：只使用你填写的服务器，失败不会自动改用公共接口。")
        return I18n.t("未校时使用内置标准时区，伦敦/纽约/洛杉矶可能有夏令时偏差。")
    }

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

    function load() {
        loading = true
        apiMode = settingRead("time/api-mode", "auto") === "custom" ? "custom" : "auto"
        customHost = settingRead("time/api-host", "")
        const raw = settingRead("time/cities", "")
        if (raw !== "") {
            try {
                const list = JSON.parse(raw)
                const kept = []
                for (let i = 0; i < list.length && kept.length < maxCities; ++i)
                    if (cityById(list[i])) kept.push(list[i])
                if (kept.length > 0) zones = kept
            } catch (error) {
                console.warn("time sync: ignoring unreadable city list")
            }
        }
        loading = false
    }

    function persistCities() {
        settingWrite("time/cities", JSON.stringify(zones))
    }

    function persistApiSettings() {
        settingWrite("time/api-mode", apiMode)
        settingWrite("time/api-host", customHost.trim())
    }

    onZonesChanged: if (!loading) persistCities()
    onApiModeChanged: if (!loading) persistApiSettings()
    onCustomHostChanged: if (!loading) persistApiSettings()

    // ------------------------------------------------------------ 城市

    function cityById(id) {
        for (let i = 0; i < catalog.length; ++i)
            if (catalog[i].id === id) return catalog[i]
        return null
    }

    function hasZone(id) {
        return zones.indexOf(id) >= 0
    }

    // 返回 false 表示这次点击没有改变选择（超出上限或删到只剩一个）。
    function toggleCity(id) {
        const list = zones.slice()
        const at = list.indexOf(id)
        if (at >= 0) {
            if (list.length <= 1) return false        // 至少保留一个城市
            list.splice(at, 1)
        } else {
            if (list.length >= maxCities) return false
            list.push(id)
        }
        zones = list
        return true
    }

    // 已校时用同步值，否则用内置标准偏移。
    function offsetOf(id) {
        const offset = syncedOffsets[id]
        if (offset !== undefined) return offset
        const zone = cityById(id)
        return zone ? zone.offset : 0
    }

    function isSyncedZone(id) {
        return syncedOffsets[id] !== undefined
    }

    // 恢复内置时区：清掉同步结果与线路状态，城市选择保留。
    function resetZones() {
        syncedOffsets = ({})
        syncedCount = 0
        syncedOnce = false
        provider = ""
        error = ""
        alertText = ""
        skewMs = 0
        syncedAt = ""
        providerState = ({})
        providerQueue = []
        providerIndex = -1
    }

    function clearAlert() {
        alertText = ""
    }

    // ------------------------------------------------------------ 校时

    function buildProviderQueue() {
        if (apiMode === "custom") {
            const host = customHost.trim()
            if (!/^https?:\/\//i.test(host)) return []
            return [{ id: "custom", host: host, kind: "worldtimeapi" }]
        }
        return [{ id: "worldtimeapi", host: primaryTimeHost, kind: "worldtimeapi" },
                { id: "timeapi", host: fallbackTimeHost, kind: "timeapi" }]
    }

    function providerLabel(id) {
        if (id === "custom") return I18n.t("自定义服务器")
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

    function syncTime() {
        if (syncing) return
        if (cities.length === 0) {
            alertText = I18n.t("先在时钟页添加至少一个城市再校时。")
            return
        }
        const queue = buildProviderQueue()
        if (queue.length === 0) {
            alertText = I18n.t("自定义服务器地址需以 http:// 或 https:// 开头，例如 https://time.example.com")
            return
        }
        providerQueue = queue
        providerIndex = -1
        syncedOffsets = ({})
        providerState = ({})          // 红点只在本次同步内有效
        syncing = true
        error = ""
        alertText = ""
        syncedCount = 0
        syncedOnce = false
        provider = ""
        requestNextProvider()
    }

    function requestNextProvider() {
        if (!syncing) return
        ++providerIndex
        if (providerIndex >= providerQueue.length) { finishSync(); return }
        syncIndex = -1
        baseUtcMs = 0
        requestSyncStep()
    }

    function requestSyncStep() {
        if (!syncing) return
        const current = providerQueue[providerIndex]
        // timeapi.io 的返回只有城市墙上时间，没有偏移量，先取一次 UTC 基准。
        if (current.kind === "timeapi" && baseUtcMs === 0) { sendSyncRequest(-1); return }
        ++syncIndex
        if (syncIndex >= cities.length) {
            setProviderState(current.id, 1)
            finishSync()
            return
        }
        sendSyncRequest(syncIndex)
    }

    function sendSyncRequest(index) {
        const current = providerQueue[providerIndex]
        const token = ++syncToken
        const zone = index < 0 ? "UTC" : cities[index].id
        const url = current.kind === "timeapi"
            ? TimeMath.buildTimeApiUrl(current.host, zone)
            : TimeMath.buildWorldTimeUrl(current.host, zone)
        const request = new XMLHttpRequest()
        xhr = request
        request.onreadystatechange = function() {
            if (request.readyState !== XMLHttpRequest.DONE) return
            handleSyncResponse(index, request, token)
        }
        watchdog.restart()
        request.open("GET", url)
        request.send()
    }

    function handleSyncResponse(index, request, token) {
        if (token !== syncToken) return
        watchdog.stop()
        if (request.status < 200 || request.status >= 300) {
            syncRequestFailed(index, token,
                request.status === 0 ? I18n.t("网络不可用或服务不可达") : ("HTTP " + request.status))
            return
        }
        const current = providerQueue[providerIndex]
        let parsed = null
        try {
            parsed = current.kind === "timeapi" ? TimeMath.parseTimeApiZone(request.responseText)
                                                : TimeMath.parseWorldTimeApi(request.responseText)
        } catch (error) {
            syncRequestFailed(index, token, I18n.t("数据格式无效"))
            return
        }
        if (!parsed || !parsed.ok) {
            syncRequestFailed(index, token,
                (parsed && parsed.error) ? parsed.error : I18n.t("数据格式无效"))
            return
        }
        const now = Date.now()
        if (index < 0) {
            baseUtcMs = parsed.wallUtcMs
            skewMs = TimeMath.skewMs(baseUtcMs, now)
            requestSyncStep()
            return
        }
        const offset = current.kind === "timeapi"
            ? TimeMath.offsetFromBaselines(parsed.wallUtcMs, baseUtcMs)
            : parsed.offsetMinutes
        if (offset === null) {
            syncRequestFailed(index, token, I18n.t("数据格式无效"))
            return
        }
        applyZoneOffset(index, offset)
        if (current.kind === "worldtimeapi") skewMs = TimeMath.skewMs(parsed.serverUtcMs, now)
        requestSyncStep()
    }

    function applyZoneOffset(index, offset) {
        if (index < 0 || index >= cities.length) return
        const next = Object.assign({}, syncedOffsets)
        next[cities[index].id] = offset
        syncedOffsets = next
        ++syncedCount
    }

    function cancelRequest() {
        watchdog.stop()
        ++syncToken
        if (xhr) { try { xhr.abort() } catch (error) {} }
        xhr = null
    }

    function syncRequestFailed(index, token, reason) {
        if (token !== syncToken) return
        cancelRequest()
        const current = providerQueue[providerIndex]
        if (!current) { syncing = false; return }
        setProviderState(current.id, -1)
        const more = providerIndex + 1 < providerQueue.length
        const label = providerLabel(current.id)
        if (more) {
            // 换线路时要清掉这一路的半截结果，避免半新半旧的偏移混用。
            syncedOffsets = ({})
            syncedCount = 0
            error = label + " · " + reason
            // 每条线路失效都要有明确提示（弹窗），不能只在状态行里悄悄换线。
            alertText = label + " · " + reason + "\n" + I18n.t("已切换到下一个校时接口。")
            requestNextProvider()
            return
        }
        syncedCount = 0
        error = I18n.t("网络校时失败：") + reason
        syncing = false
        alertText = label + " · " + reason + "\n" +
            (apiMode === "custom" ? I18n.t("自定义模式不会自动改用公共接口，可切回自动后重试。")
                                  : I18n.t("所有校时接口均不可用，仍显示内置时区。"))
    }

    function finishSync() {
        cancelRequest()
        syncing = false
        if (syncedCount <= 0) {
            syncedOnce = false
            error = I18n.t("网络校时失败：") + I18n.t("网络不可用或服务不可达")
            return
        }
        syncedOnce = true
        provider = providerIndex >= 0 && providerIndex < providerQueue.length
            ? providerLabel(providerQueue[providerIndex].id) : ""
        syncedAt = TimeMath.formatHm(TimeMath.localParts(Date.now()))
        error = ""
        alertText = ""
    }
}
