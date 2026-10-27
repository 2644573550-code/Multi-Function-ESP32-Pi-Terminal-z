.pragma library

// Pure helpers for the Time page: clock formatting, city wall clocks, countdown
// and stopwatch formatting, alarm matching, and the world-time API parsers.
//
// No Qt/QML types live here on purpose: pi_terminal/tests/time_math_test.cjs
// executes this exact file in Node, so the shipping code is what gets tested.
// Keep it ES5.1 + let/const/arrow only; the Qt 6.4 QML engine is not a browser.

// ---------------------------------------------------------------- formatting

function pad2(value) {
    var n = Math.floor(Math.abs(Number(value) || 0));
    return n < 10 ? "0" + n : String(n);
}

function clamp(value, minimum, maximum) {
    var n = Number(value) || 0;
    return n < minimum ? minimum : (n > maximum ? maximum : n);
}

// Device wall clock. getTimezoneOffset() is inverted (UTC - local), so negate.
function localOffsetMinutes(epochMs) {
    return -new Date(Number(epochMs)).getTimezoneOffset();
}

function localParts(epochMs) {
    var date = new Date(Number(epochMs));
    return {
        year: date.getFullYear(), month: date.getMonth() + 1, day: date.getDate(),
        hour: date.getHours(), minute: date.getMinutes(), second: date.getSeconds(),
        weekday: date.getDay()
    };
}

// A city's wall clock is the UTC fields of (epoch + offset); this never depends
// on the device timezone, and avoids toLocaleString's timeZone support, which
// Qt 6.4's JS engine ignores.
function cityParts(epochMs, offsetMinutes) {
    var shifted = new Date(Number(epochMs) + Number(offsetMinutes) * 60000);
    return {
        year: shifted.getUTCFullYear(), month: shifted.getUTCMonth() + 1, day: shifted.getUTCDate(),
        hour: shifted.getUTCHours(), minute: shifted.getUTCMinutes(), second: shifted.getUTCSeconds(),
        weekday: shifted.getUTCDay()
    };
}

function formatHms(parts, withSeconds) {
    return pad2(parts.hour) + ":" + pad2(parts.minute) +
        (withSeconds === false ? "" : ":" + pad2(parts.second));
}

function formatHm(parts) {
    return pad2(parts.hour) + ":" + pad2(parts.minute);
}

function formatYmd(parts) {
    return parts.year + "-" + pad2(parts.month) + "-" + pad2(parts.day);
}

// 480 -> "UTC+08:00", -210 -> "UTC-03:30".
function formatOffset(minutes) {
    var total = Math.round(Number(minutes) || 0);
    var sign = total < 0 ? "-" : "+";
    var absolute = Math.abs(total);
    return "UTC" + sign + pad2(Math.floor(absolute / 60)) + ":" + pad2(absolute % 60);
}

// ------------------------------------------------------------ durations

// Countdown and stopwatch readouts. Countdown is "HH:MM:SS" once an hour is
// involved, otherwise "MM:SS"; the stopwatch always keeps tenths.
function durationParts(ms) {
    var total = Math.max(0, Math.round(Number(ms) || 0));
    return {
        hours: Math.floor(total / 3600000),
        minutes: Math.floor(total % 3600000 / 60000),
        seconds: Math.floor(total % 60000 / 1000),
        tenths: Math.floor(total % 1000 / 100)
    };
}

function formatCountdown(ms) {
    var parts = durationParts(ms);
    if (parts.hours > 0) return pad2(parts.hours) + ":" + pad2(parts.minutes) + ":" + pad2(parts.seconds);
    return pad2(parts.minutes) + ":" + pad2(parts.seconds);
}

function formatStopwatch(ms) {
    var parts = durationParts(ms);
    var hours = parts.hours > 0 ? pad2(parts.hours) + ":" : "";
    return hours + pad2(parts.minutes) + ":" + pad2(parts.seconds) + "." + parts.tenths;
}

function hoursMinutesSecondsToMs(hours, minutes, seconds) {
    return (clamp(hours, 0, 23) * 3600 + clamp(minutes, 0, 59) * 60 + clamp(seconds, 0, 59)) * 1000;
}

// ------------------------------------------------------------------ alarms

function minuteOfDay(parts) {
    return parts.hour * 60 + parts.minute;
}

// An alarm is due when its minute-of-day matches and that minute has not fired
// yet. lastFiredMinute holds "hour * 60 + minute" of the last fired minute.
// QML ListModel roles cannot be called "enabled" (it clashes with Item), so the
// helper accepts both spellings and never treats a missing flag as disabled.
function alarmDue(parts, alarm, lastFiredMinute) {
    if (!alarm) return false;
    if (alarm.enabled === false || alarm.alarmEnabled === false) return false;
    var now = minuteOfDay(parts);
    if (now === lastFiredMinute) return false;
    return now === alarm.hour * 60 + alarm.minute;
}

function alarmTimeLabel(alarm) {
    return pad2(alarm.hour) + ":" + pad2(alarm.minute);
}

// ------------------------------------------------------------------ zones

// 世界时钟城市目录。offset 是 IANA 标准偏移（分钟）；dst 标记该时区有夏令时，
// 内置表不含 DST 规则，所以未校时的城市必须由 UI 说明“夏令时未知”。
// 数据与界面分离：TimeSync.qml 用它校验城市 id 并按 id 查标准偏移。
var cityCatalog = [
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
];

// ------------------------------------------------------------------ world

// "+08:00" / "-03:30" / "Z" -> minutes. Returns null for anything unexpected.
function parseUtcOffset(text) {
    if (typeof text !== "string") return null;
    var value = text.trim();
    if (value === "Z" || value === "z" || value === "UTC" || value === "GMT") return 0;
    var match = /^([+-])(\d{1,2}):?(\d{2})$/.exec(value);
    if (!match) return null;
    var minutes = Number(match[2]) * 60 + Number(match[3]);
    if (!isFinite(minutes)) return null;
    return match[1] === "-" ? -minutes : minutes;
}

// Real zones stay inside -12h..+14h and use whole quarter hours. Anything else
// is treated as a broken response instead of a plausible-looking offset.
function plausibleOffset(minutes) {
    if (typeof minutes !== "number" || !isFinite(minutes)) return false;
    if (minutes < -12 * 60 || minutes > 14 * 60) return false;
    return Math.abs(minutes % 15) < 0.001;
}

function buildWorldTimeUrl(host, zone) {
    return host.replace(/\/+$/, "") + "/api/timezone/" + encodeURIComponent(zone);
}

function buildTimeApiUrl(host, zone) {
    return host.replace(/\/+$/, "") + "/api/time/current/zone?timeZone=" + encodeURIComponent(zone);
}

// worldtimeapi.org: {"utc_offset":"+08:00","unixtime":1793000443,"dst":false,...}
// One request per zone; the offset and the server epoch arrive together.
function parseWorldTimeApi(body) {
    var data = JSON.parse(body);
    if (!data || typeof data !== "object") return { ok: false, error: "empty response" };
    var offset = parseUtcOffset(data.utc_offset);
    if (!plausibleOffset(offset)) return { ok: false, error: "invalid utc_offset" };
    var seconds = Number(data.unixtime);
    if (!isFinite(seconds) || seconds <= 0) return { ok: false, error: "invalid unixtime" };
    return { ok: true, offsetMinutes: offset, dst: data.dst === true, serverUtcMs: seconds * 1000 };
}

// timeapi.io: {year,month,day,hour,minute,seconds,milliSeconds,dstActive,...}
// The payload carries the zone's wall clock but not its offset, so the caller
// pairs one UTC reading with one reading per city.
function parseTimeApiZone(body) {
    var data = JSON.parse(body);
    if (!data || typeof data !== "object") return { ok: false, error: "empty response" };
    var year = Number(data.year), month = Number(data.month), day = Number(data.day);
    var hour = Number(data.hour), minute = Number(data.minute), second = Number(data.seconds);
    if (!isFinite(year) || !isFinite(month) || !isFinite(day) ||
        !isFinite(hour) || !isFinite(minute) || !isFinite(second)) {
        return { ok: false, error: "incomplete time fields" };
    }
    if (year < 2000 || year > 2200 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return { ok: false, error: "time fields out of range" };
    }
    var milli = Number(data.milliSeconds);
    return {
        ok: true,
        wallUtcMs: Date.UTC(year, month - 1, day, hour, minute, second, isFinite(milli) ? milli : 0),
        dst: data.dstActive === true
    };
}

// offset = city wall clock - UTC baseline, both read from the same service.
function offsetFromBaselines(wallUtcMs, baseUtcMs) {
    if (!isFinite(wallUtcMs) || !isFinite(baseUtcMs)) return null;
    var minutes = Math.round((wallUtcMs - baseUtcMs) / 60000);
    return plausibleOffset(minutes) ? minutes : null;
}

// Clock drift in whole milliseconds; positive means the device clock is behind.
function skewMs(serverUtcMs, localAtReceiptMs) {
    if (!isFinite(serverUtcMs) || !isFinite(localAtReceiptMs)) return 0;
    return Math.round(serverUtcMs - localAtReceiptMs);
}

function formatSkew(ms) {
    var value = Math.round(Number(ms) || 0);
    if (value === 0) return "0 s";
    var sign = value > 0 ? "+" : "-";
    var absolute = Math.abs(value);
    if (absolute < 1000) return sign + absolute + " ms";
    return sign + (absolute / 1000).toFixed(1) + " s";
}

// Days with an offset table entry built from the IANA tz database cannot know
// DST transitions, so the UI must say so instead of showing a wrong hour.
function zoneNeedsSync(zone, offsetOverridden) {
    return !offsetOverridden && zone.dst === true;
}
