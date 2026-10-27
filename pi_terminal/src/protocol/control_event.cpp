#include "control_event.h"

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVarLengthArray>
#include <cmath>

namespace {
const QString kSchema = QStringLiteral("p0-control-v1");

bool integer(const QJsonValue &v, double low, double high, double *out = nullptr) {
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d < low || d > high || d != std::floor(d)) return false;
    if (out) *out = d;
    return true;
}
bool label(const QJsonValue &v) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return v.isString() && re.match(v.toString()).hasMatch();
}
// spec §2.1: 1 byte on the wire, 2 lowercase hex in JSON and event fields.
bool keyId(const QJsonValue &v) {
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{2}$"));
    return v.isString() && re.match(v.toString()).hasMatch();
}
bool hex128(const QJsonValue &v) {
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{32}$"));
    return v.isString() && re.match(v.toString()).hasMatch();
}
PbrControl::Result fail(const QString &code) { return {PbrControl::Verdict::Rejected, code}; }
}

namespace PbrControl {
Result validate(const QJsonObject &event) {
    const auto name = event.value("event").toString();
    // pong/bridge_status are responses to kind=2 diagnostics keyed by
    // request_sequence; they carry no event_id/ttl/schema and belong to the
    // envelope layer (spec §6.5), never to this validator.
    static const QSet<QString> kinds{"attach", "status", "detach", "rfid_card", "ack"};
    if (!kinds.contains(name)) return fail("INVALID_MESSAGE");

    // The pre-P0 bridge sends attach/detach without any security fields. It is
    // reported as legacy so callers cannot mistake it for an authenticated event.
    // shortcut: unauthenticated entry point, delete this branch in T-07.
    if (!event.contains("schema"))
        return (name == "attach" || name == "detach") ? Result{Verdict::Legacy, {}} : fail("INVALID_MESSAGE");
    if (event.value("schema").toString() != kSchema) return fail("UNSUPPORTED_VERSION");

    if (!label(event.value("bridge_id")) || !keyId(event.value("key_id"))) return fail("IDENTITY_REJECTED");
    if (!hex128(event.value("boot_id")) || !hex128(event.value("session_id"))) return fail("INVALID_MESSAGE");
    if (!integer(event.value("uptime_ms"), 0, 9e15)) return fail("INVALID_MESSAGE");

    if (name == "attach" || name == "status" || name == "detach") {
        // Module position events carry slot, binding generation and module ids.
        if (!label(event.value("module_instance")) ||
            !integer(event.value("module_id"), 1, 255)) return fail("INVALID_MESSAGE");
        if (!integer(event.value("slot_id"), 0, 1)) return fail("SLOT_MISMATCH");
        if (!hex128(event.value("binding_generation"))) return fail("INVALID_MESSAGE");
    }
    if (name == "rfid_card") {
        // RC522 is a bridge-level event: f-day1 §4.2 forbids inventing
        // slot_id/device_id/module_id here, and rfid_service.c sends none.
        if (!label(event.value("reader_id"))) return fail("INVALID_MESSAGE");
        double length = 0;
        if (!integer(event.value("uid_len"), 4, 10, &length) || (length != 4 && length != 7 && length != 10))
            return fail("INVALID_MESSAGE");
        // The bridge emits UPPERCASE hex (rc522.c uses "0123456789ABCDEF"), so
        // accepting only lowercase would reject every real swipe. Case is folded
        // once here and never leaks into the UID index: deriveUidIndex takes
        // raw bytes, not this text.
        static const QRegularExpression hex(QStringLiteral("^[0-9A-Fa-f]+$"));
        const auto uid = event.value("uid").toString();
        if (!event.value("uid").isString() || uid.size() != int(length) * 2 || !hex.match(uid).hasMatch())
            return fail("INVALID_MESSAGE");
    }
    if (name == "ack") {
        if (!integer(event.value("ack_event_id"), 1, 4294967295.0)) return fail("INVALID_MESSAGE");
        static const QSet<QString> results{"accepted", "duplicate", "expired", "rejected"};
        if (!results.contains(event.value("result").toString())) return fail("INVALID_MESSAGE");
        return {Verdict::Trusted, {}}; // ACK carries no event_id/TTL of its own.
    }

    if (!integer(event.value("event_id"), 1, 4294967295.0)) return fail("INVALID_MESSAGE");
    double ttl = 0;
    if (!integer(event.value("ttl_ms"), 3000, 3000, &ttl)) return fail("EVENT_EXPIRED");
    return {Verdict::Trusted, {}};
}

bool payloadAcceptable(const QByteArray &payload) {
    // Qt's JSON parser is permissive in two ways f-day1 §4.2 forbids: duplicate
    // object keys silently keep the last value, and there is no depth limit
    // (verified: depth 100 parses fine). Both must be rejected at the raw byte
    // level, because a parsed object no longer shows what was discarded.
    //
    // Keys are compared after escape decoding: "\u0061" and "a" are the same key
    // to Qt, so comparing raw bytes here would let a duplicate through.
    constexpr int MaxDepth = 8;
    struct Frame { char bracket; bool expectKey; QSet<QString> keys; };
    QVarLengthArray<Frame, MaxDepth + 1> stack;
    QString token;
    QByteArray unicode;   // hex digits seen so far after \u
    int unicodeNeeded = 0; // >0 while collecting a \uXXXX escape
    bool quoted = false, escaped = false;
    bool capturing = false; // inside a string that may be an object key

    auto flushKey = [&]() -> bool {
        if (!capturing || stack.isEmpty() || !stack.last().expectKey) return true;
        if (stack.last().keys.contains(token)) return false; // duplicate key
        stack.last().keys.insert(token);
        stack.last().expectKey = false;
        return true;
    };

    for (const char ch : payload) {
        if (quoted) {
            if (unicodeNeeded > 0) {
                unicode.append(ch);
                if (--unicodeNeeded == 0) {
                    bool ok = false;
                    const uint code = unicode.toUInt(&ok, 16);
                    if (!ok) return false;
                    token.append(QChar(ushort(code)));
                    unicode.clear();
                }
                continue;
            }
            if (escaped) {
                escaped = false;
                if (capturing) {
                    switch (ch) {
                    case 'n': token.append('\n'); break;
                    case 't': token.append('\t'); break;
                    case 'r': token.append('\r'); break;
                    case 'b': token.append('\b'); break;
                    case 'f': token.append('\f'); break;
                    case 'u': unicodeNeeded = 4; unicode.clear(); break;
                    default: token.append(QLatin1Char(ch)); break; // \" \\ \/
                    }
                } else if (ch == 'u') {
                    unicodeNeeded = 4; // still must consume the digits
                    unicode.clear();
                }
                continue;
            }
            if (ch == '\\') { escaped = true; continue; }
            if (ch == '"') {
                quoted = false;
                if (!flushKey()) return false;
                token.clear();
                capturing = false;
                continue;
            }
            if (capturing) token.append(QLatin1Char(ch));
            continue;
        }
        switch (ch) {
        case '"':
            quoted = true;
            capturing = !stack.isEmpty() && stack.last().bracket == '{' && stack.last().expectKey;
            token.clear();
            unicode.clear();
            break;
        case '{': case '[':
            if (stack.size() + 1 > MaxDepth) return false;
            stack.append({ch, ch == '{', {}});
            break;
        case '}': case ']':
            if (stack.isEmpty()) return false;
            if ((ch == '}') != (stack.last().bracket == '{')) return false;
            stack.removeLast();
            break;
        case ',':
            if (!stack.isEmpty() && stack.last().bracket == '{') stack.last().expectKey = true;
            break;
        default:
            break;
        }
    }
    return !quoted && stack.isEmpty();
}
}
