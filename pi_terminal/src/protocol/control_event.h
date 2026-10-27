#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Validates a decrypted PBR control event against docs/devdocs/04-dev-tasks-f-day1.md §4.2.
// "p0-control-v1" is the only trusted schema. A schema-less attach/detach is the
// pre-P0 bridge frame: accepted for compatibility but reported as Legacy, never
// as authenticated by this layer.
namespace PbrControl {
enum class Verdict { Rejected, Legacy, Trusted };
struct Result {
    Verdict verdict = Verdict::Rejected;
    QString reason; // f-day1 §7 error code
};
Result validate(const QJsonObject &event);

// f-day1 §4.2 requires rejecting duplicate JSON keys and nesting deeper than 8,
// but Qt's parser silently keeps the last duplicate and accepts any depth. This
// scans the raw bytes before parsing; validate() cannot see what was dropped.
bool payloadAcceptable(const QByteArray &payload);
}
