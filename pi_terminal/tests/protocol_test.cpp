#include "protocol/pibridge_protocol.h"
#include "protocol/control_event.h"
#include "protocol/data_checksum.h"
#include <cstdlib>
#include <iostream>

void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
void pbrControl() {
    using PbrControl::Verdict;
    const QJsonObject base{{"schema", "p0-control-v1"}, {"bridge_id", "bridge-1"}, {"key_id", "a1"},
        {"boot_id", "0123456789abcdef0123456789abcdef"},
        {"session_id", "fedcba9876543210fedcba9876543210"},
        {"uptime_ms", 1000}, {"event_id", 5}, {"ttl_ms", 3000}};
    auto with = [&](const QJsonObject &extra) { QJsonObject e = base; for (auto it = extra.begin(); it != extra.end(); ++it) e[it.key()] = it.value(); return e; };

    require(PbrControl::validate({{"event", "attach"}, {"module_id", 80}, {"module_instance", "m-1"},
        {"module_config", QJsonObject{}}}).verdict == Verdict::Legacy, "schema-less attach is legacy");
    require(PbrControl::validate({{"event", "detach"}, {"module_id", 80}, {"module_instance", "m-1"}}).verdict == Verdict::Legacy,
            "schema-less detach is legacy");
    require(PbrControl::validate({{"event", "rfid_card"}, {"module_id", 80}, {"module_instance", "m-1"}}).verdict == Verdict::Rejected,
            "schema-less rfid rejected");
    require(PbrControl::validate(with({{"event", "attach"}, {"module_id", 80}, {"module_instance", "m-1"},
        {"slot_id", 0}, {"binding_generation", "0123456789abcdef0123456789abcdef"}})).verdict == Verdict::Trusted,
        "valid attach");
    require(PbrControl::validate(with({{"event", "attach"}, {"module_id", 80}, {"module_instance", "m-1"},
        {"slot_id", 2}, {"binding_generation", "0123456789abcdef0123456789abcdef"}})).reason == "SLOT_MISMATCH",
        "slot out of range");
    // RC522 is bridge-level: rfid_service.c sends no module_id/module_instance,
    // so requiring them would reject the real bridge's own events.
    require(PbrControl::validate(with({{"event", "rfid_card"},
        {"reader_id", "rc522-1"}, {"uid_len", 4}, {"uid", "a1b2c3d4"}})).verdict == Verdict::Trusted,
        "bridge-level rfid without module fields accepted");
    require(PbrControl::validate(with({{"event", "rfid_card"},
        {"reader_id", "rc522-1"}, {"uid_len", 4}, {"uid", "A1B2C3D4"}})).verdict == Verdict::Trusted,
        "uppercase uid accepted: rc522.c emits 0-9A-F, the real bridge would be rejected otherwise");
    require(PbrControl::validate(with({{"event", "rfid_card"},
        {"reader_id", "rc522-1"}, {"uid_len", 4}, {"uid", "a1b2c3d"}})).verdict == Verdict::Rejected,
        "uid length mismatch rejected");
    require(PbrControl::validate(with({{"event", "rfid_card"}, {"module_id", 80}, {"module_instance", "m-1"},
        {"reader_id", "r1"}, {"uid_len", 4}, {"uid", "a1b2c3d4"}})).verdict == Verdict::Trusted, "rfid with extra module fields tolerated");
    require(PbrControl::validate(with({{"event", "rfid_card"},
        {"reader_id", "r1"}, {"uid_len", 4}, {"uid", "a1b2c3"}})).verdict == Verdict::Rejected, "uid length mismatch");
    require(PbrControl::validate(with({{"event", "ack"}, {"ack_event_id", 5}, {"result", "duplicate"}})).verdict == Verdict::Trusted,
            "valid ack");
    require(PbrControl::validate(with({{"event", "ack"}, {"ack_event_id", 5}, {"result", "maybe"}})).verdict == Verdict::Rejected,
            "invalid ack result");
    // spec §6.5: diagnostics are keyed by request_sequence and have no
    // event_id/ttl; they must not be smuggled through the event validator.
    require(PbrControl::validate(with({{"event", "pong"}})).verdict == Verdict::Rejected,
            "pong is not a p0-control-v1 event");
    require(PbrControl::validate(with({{"event", "bridge_status"}})).verdict == Verdict::Rejected,
            "bridge_status is not a p0-control-v1 event");
    require(PbrControl::validate(with({{"event", "attach"}, {"module_id", 80}, {"module_instance", "m-1"},
        {"slot_id", 0}, {"binding_generation", "0123456789abcdef0123456789abcdef"},
        {"key_id", "key-1"}})).reason == "IDENTITY_REJECTED",
        "non-hex key_id rejected (spec 2.1 requires 2 lowercase hex)");
    require(PbrControl::validate(with({{"event", "status"}, {"module_id", 80}, {"module_instance", "m-1"},
        {"slot_id", 1}, {"binding_generation", "0123456789abcdef0123456789abcdef"}, {"ttl_ms", 2999}})).reason == "EVENT_EXPIRED",
        "ttl must be 3000");
    require(PbrControl::validate(with({{"event", "status"}, {"module_id", 80}, {"module_instance", "m-1"},
        {"slot_id", 1}, {"binding_generation", "0123456789abcdef0123456789abcdef"}, {"boot_id", "xyz"}})).reason == "INVALID_MESSAGE",
        "boot_id must be 32 lowercase hex");
    require(PbrControl::validate(with({{"event", "attach"}, {"module_id", 80}, {"module_instance", "m-1"},
        {"slot_id", 0}, {"binding_generation", "0123456789abcdef0123456789abcdef"}, {"schema", "terminal-rfid-v1"}})).reason == "UNSUPPORTED_VERSION",
        "legacy schema version not trusted");
    require(PbrControl::validate({{"event", "unknown"}}).verdict == Verdict::Rejected, "unknown event rejected");

    // Raw-byte guard: Qt's parser keeps the last duplicate key and has no depth
    // limit, so these must be caught before QJsonDocument ever sees them.
    require(PbrControl::payloadAcceptable(R"({"a":1,"a":2})") == false, "duplicate key rejected");
    // Qt decodes \u0061 to 'a', so a raw-byte comparison would let this through.
    require(PbrControl::payloadAcceptable(R"({"a":1,"\u0061":2})") == false, "unicode-escaped duplicate rejected");
    require(PbrControl::payloadAcceptable(R"({"a":1,"A":2})") == true, "case-different keys accepted");
    require(PbrControl::payloadAcceptable(R"({"a":1,"b":{"c":2}})") == true, "nested unique keys accepted");
    require(PbrControl::payloadAcceptable(R"({"a":1,"b":{"a":2}})") == true, "same key in sibling object accepted");
    require(PbrControl::payloadAcceptable(R"({"a":"x,\"y\":1"})") == true, "escaped quote inside string accepted");
    {
        QByteArray deep = R"({"a":)";
        for (int i = 0; i < 8; ++i) deep += '[';
        deep += '1';
        for (int i = 0; i < 8; ++i) deep += ']';
        deep += '}';
        // 9 levels of container total (object + 8 arrays) exceeds the limit of 8.
        require(PbrControl::payloadAcceptable(deep) == false, "depth 9 rejected");
    }
    require(PbrControl::payloadAcceptable(R"({"a":[1,2,{"b":"c"}]})") == true, "depth 3 accepted");
    require(PbrControl::payloadAcceptable(R"({"a":1)") == false, "truncated object rejected");
    require(PbrControl::payloadAcceptable(R"([1,2})") == false, "mismatched bracket rejected");
}
int main() {
    pbrControl();
    require(dataChecksum("123456789").toHex() == "2639f4cb", "independent CRC32 vector");
    const auto golden = QByteArray::fromHex("5042523101010700000002007b7db634");
    require(PiBridge::encode(1, PiBridge::ControlEvent, 7, "{}") == golden, "independent CRC vector");
    quint8 direction = 0; PiBridge::Kind kind{}; quint32 seq = 0; QByteArray payload;
    for (int split = 0; split < golden.size(); ++split) {
        auto buffer = golden.left(split);
        require(!PiBridge::decode(buffer, direction, kind, seq, payload), "premature partial decode");
        buffer += golden.mid(split);
        require(PiBridge::decode(buffer, direction, kind, seq, payload), "fragment lost");
        require(direction == 1 && seq == 7 && payload == "{}" && buffer.isEmpty(), "decoded fields");
    }
    auto bad = golden; bad[bad.size() - 1] = char(0);
    auto buffer = QByteArray("noise") + bad + golden + golden;
    require(PiBridge::decode(buffer, direction, kind, seq, payload), "bad CRC recovery");
    require(PiBridge::decode(buffer, direction, kind, seq, payload), "coalesced frame");
    require(buffer.isEmpty(), "buffer not consumed");
    require(PiBridge::encode(1, PiBridge::ControlEvent, 0, QByteArray(2049, 'x')).isEmpty(), "oversize accepted");
    std::cout << "protocol tests passed\n";
}
