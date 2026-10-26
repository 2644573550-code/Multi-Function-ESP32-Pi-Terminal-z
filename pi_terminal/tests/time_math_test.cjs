// Executes the shipping TimeMath.js instead of a second implementation.
// Run: node pi_terminal/tests/time_math_test.cjs
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');

const source = fs.readFileSync(path.join(__dirname, '../qml/TimeMath.js'), 'utf8')
    // ".pragma library" is a QML-JS preprocessor line, not JavaScript.
    .replace(/^\s*\.pragma[^\n]*$/gm, '');
const context = { Date, Math, Number, String, isFinite, JSON, encodeURIComponent, RegExp };
vm.createContext(context);
vm.runInContext(source, context);

// --- formatting -----------------------------------------------------------
assert.equal(context.pad2(7), '07');
assert.equal(context.pad2(42), '42');
assert.equal(context.formatHms({hour: 9, minute: 5, second: 3}), '09:05:03');
assert.equal(context.formatHms({hour: 9, minute: 5, second: 3}, false), '09:05');
assert.equal(context.formatHm({hour: 23, minute: 59}), '23:59');
assert.equal(context.formatYmd({year: 2026, month: 1, day: 2}), '2026-01-02');

// --- offsets --------------------------------------------------------------
assert.equal(context.formatOffset(480), 'UTC+08:00');
assert.equal(context.formatOffset(-210), 'UTC-03:30');
assert.equal(context.formatOffset(0), 'UTC+00:00');
assert.equal(context.formatOffset(345), 'UTC+05:45'); // Kathmandu

// --- city wall clock (independent of the host timezone) -------------------
const epoch = Date.UTC(2026, 9, 26, 14, 0, 0); // 14:00 UTC
assert.equal(context.formatHms(context.cityParts(epoch, 480)), '22:00:00');
assert.equal(context.formatHms(context.cityParts(epoch, 0)), '14:00:00');
assert.equal(context.formatHms(context.cityParts(epoch, -300)), '09:00:00');
assert.equal(context.formatYmd(context.cityParts(epoch, 480)), '2026-10-26');
// Crossing midnight in the next day, and backwards across a date line.
assert.equal(context.formatYmd(context.cityParts(epoch, 600)), '2026-10-27');
assert.equal(context.formatHms(context.cityParts(epoch, 600)), '00:00:00');
assert.equal(context.formatYmd(context.cityParts(epoch, -720)), '2026-10-26');
assert.equal(context.cityParts(epoch, 480).weekday, 1); // Monday

// --- durations ------------------------------------------------------------
assert.equal(context.formatCountdown(0), '00:00');
assert.equal(context.formatCountdown(65 * 1000), '01:05');
assert.equal(context.formatCountdown(3661 * 1000), '01:01:01');
assert.equal(context.formatCountdown(-500), '00:00');
assert.equal(context.formatStopwatch(0), '00:00.0');
assert.equal(context.formatStopwatch(12345), '00:12.3');
assert.equal(context.formatStopwatch(3723456), '01:02:03.4');
assert.equal(context.hoursMinutesSecondsToMs(1, 2, 3), 3723000);
assert.equal(context.hoursMinutesSecondsToMs(-1, 99, 99), 59 * 60000 + 59 * 1000); // clamped
assert.equal(context.hoursMinutesSecondsToMs(23, 59, 59), 86399000);

// --- alarms ---------------------------------------------------------------
const alarm = { hour: 7, minute: 30, enabled: true };
assert.equal(context.alarmDue({hour: 7, minute: 30}, alarm, -1), true);
assert.equal(context.alarmDue({hour: 7, minute: 30}, alarm, 450), false);  // same minute twice
assert.equal(context.alarmDue({hour: 7, minute: 31}, alarm, -1), false);
assert.equal(context.alarmDue({hour: 7, minute: 30}, {hour: 7, minute: 30, enabled: false}, -1), false);
// ListModel delegates use an "alarmEnabled" role; the helper must honour it too.
assert.equal(context.alarmDue({hour: 7, minute: 30}, {hour: 7, minute: 30, alarmEnabled: false}, -1), false);
assert.equal(context.alarmDue({hour: 7, minute: 30}, {hour: 7, minute: 30, alarmEnabled: true}, -1), true);
assert.equal(context.alarmTimeLabel(alarm), '07:30');
assert.equal(context.minuteOfDay({hour: 0, minute: 0}), 0);
assert.equal(context.minuteOfDay({hour: 23, minute: 59}), 1439);

// --- offset parsing -------------------------------------------------------
assert.equal(context.parseUtcOffset('+08:00'), 480);
assert.equal(context.parseUtcOffset('-03:30'), -210);
assert.equal(context.parseUtcOffset('+00:00'), 0);
assert.equal(context.parseUtcOffset('Z'), 0);
assert.equal(context.parseUtcOffset('UTC'), 0);
assert.equal(context.parseUtcOffset('+8:00'), 480);
assert.equal(context.parseUtcOffset(''), null);
assert.equal(context.parseUtcOffset('+8'), null);
assert.equal(context.parseUtcOffset(null), null);
assert.equal(context.plausibleOffset(480), true);
assert.equal(context.plausibleOffset(-210), true);
assert.equal(context.plausibleOffset(840), true);   // +14:00
assert.equal(context.plausibleOffset(-720), true);  // -12:00
assert.equal(context.plausibleOffset(900), false);  // out of range
assert.equal(context.plausibleOffset(7), false);    // not a quarter hour
assert.equal(context.plausibleOffset(NaN), false);

// --- URL construction -----------------------------------------------------
assert.equal(context.buildWorldTimeUrl('https://worldtimeapi.org/', 'Asia/Shanghai'),
    'https://worldtimeapi.org/api/timezone/Asia%2FShanghai');
assert.equal(context.buildTimeApiUrl('https://timeapi.io', 'America/New_York'),
    'https://timeapi.io/api/time/current/zone?timeZone=America%2FNew_York');

// --- provider payloads (fixtures captured from the live services) ---------
const worldBody = JSON.stringify({
    abbreviation: 'CST', datetime: '2026-10-26T22:00:43.123456+08:00', day_of_week: 1,
    dst: false, raw_offset: 28800, timezone: 'Asia/Shanghai',
    unixtime: 1793000443, utc_offset: '+08:00'
});
const world = context.parseWorldTimeApi(worldBody);
assert.equal(world.ok, true);
assert.equal(world.offsetMinutes, 480);
assert.equal(world.dst, false);
assert.equal(world.serverUtcMs, 1793000443000);
assert.equal(context.parseWorldTimeApi(JSON.stringify({utc_offset: 'bogus'})).ok, false);
assert.equal(context.parseWorldTimeApi(JSON.stringify({utc_offset: '+08:00'})).ok, false); // no unixtime
assert.throws(() => context.parseWorldTimeApi('<html>not json</html>'));

const zoneBody = JSON.stringify({
    year: 2026, month: 10, day: 26, hour: 22, minute: 0, seconds: 43, milliSeconds: 679,
    dateTime: '2026-10-26T22:00:43.6792572', date: '10/26/2026', time: '22:00',
    timeZone: 'Asia/Shanghai', dayOfWeek: 'Monday', dstActive: false
});
const utcBody = JSON.stringify({
    year: 2026, month: 10, day: 26, hour: 14, minute: 0, seconds: 43, milliSeconds: 679,
    dateTime: '2026-10-26T14:00:43.6792572', timeZone: 'UTC', dstActive: false
});
const zone = context.parseTimeApiZone(zoneBody);
const base = context.parseTimeApiZone(utcBody);
assert.equal(zone.ok, true);
assert.equal(base.ok, true);
assert.equal(context.offsetFromBaselines(zone.wallUtcMs, base.wallUtcMs), 480);
assert.equal(context.offsetFromBaselines(zone.wallUtcMs, zone.wallUtcMs), 0);
const london = context.parseTimeApiZone(JSON.stringify({
    year: 2026, month: 10, day: 26, hour: 14, minute: 0, seconds: 44, milliSeconds: 392,
    timeZone: 'Europe/London', dstActive: false
}));
assert.equal(context.offsetFromBaselines(london.wallUtcMs, base.wallUtcMs), 0);
assert.equal(context.parseTimeApiZone(JSON.stringify({year: 2026})).ok, false);
assert.equal(context.parseTimeApiZone(JSON.stringify({year: 2026, month: 13, day: 1,
    hour: 1, minute: 1, seconds: 0})).ok, false);

// --- drift ----------------------------------------------------------------
assert.equal(context.skewMs(1793000443000, 1793000442500), 500);
assert.equal(context.skewMs(1793000442500, 1793000443000), -500);
assert.equal(context.formatSkew(0), '0 s');
assert.equal(context.formatSkew(500), '+500 ms');
assert.equal(context.formatSkew(-500), '-500 ms');
assert.equal(context.formatSkew(1234), '+1.2 s');

// --- DST honesty ----------------------------------------------------------
assert.equal(context.zoneNeedsSync({dst: true}, false), true);
assert.equal(context.zoneNeedsSync({dst: true}, true), false);
assert.equal(context.zoneNeedsSync({dst: false}, false), false);

console.log('time_math_test: all assertions passed');
