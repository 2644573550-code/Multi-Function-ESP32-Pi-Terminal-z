#include "security/CardMatcher.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace security {


namespace {
// 未注入派生函数时一律失败关闭，绝不退回明文 UID 比较。
QByteArray noDerive(const QByteArray &) { return {}; }

bool validUidLength(const QByteArray &rawUid) {
    return rawUid.size() == 4 || rawUid.size() == 7 || rawUid.size() == 10;
}
}  // namespace

CardIndex::CardIndex(QString mapPath)
    : mapPath_(mapPath.isEmpty() ? defaultMapPath() : std::move(mapPath)) {}

QString CardIndex::defaultMapPath() {
    // AppConfigLocation 而非 AppLocalDataLocation：后者是交给模块的目录。
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/uid_map.json");
}

QVector<CardIndex::Entry> CardIndex::load() const {
    QFile file(mapPath_);
    if (!file.open(QIODevice::ReadOnly)) return {};

    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    // 损坏的映射表不作为空表处理：那等于静默解除全部绑卡关系。
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return {};
    const QJsonObject root = document.object();
    if (root.value("schema").toString() != QLatin1String("p0-uid-map-v1")) return {};

    QVector<Entry> entries;
    for (const QJsonValue &value : root.value("cards").toArray()) {
        const QJsonObject object = value.toObject();
        Entry entry;
        entry.index = QByteArray::fromBase64(object.value("index").toString().toLatin1());
        entry.userId = object.value("user_id").toString();
        entry.boundAtMs = static_cast<qint64>(object.value("bound_at_ms").toDouble());
        entry.revoked = object.value("revoked").toBool();
        // 缺字段/长度不对的记录直接丢弃，不做猜测修复。
        if (entry.index.size() != 32 || entry.userId.isEmpty()) continue;
        entries.append(entry);
    }
    return entries;
}

bool CardIndex::save(const QVector<Entry> &entries) const {
    QJsonArray cards;
    for (const Entry &entry : entries) {
        cards.append(QJsonObject{
            {"index", QString::fromLatin1(entry.index.toBase64())},
            {"user_id", entry.userId},
            {"bound_at_ms", static_cast<double>(entry.boundAtMs)},
            {"revoked", entry.revoked}});
    }
    const QJsonObject root{{"schema", "p0-uid-map-v1"}, {"cards", cards}};

    QDir().mkpath(QFileInfo(mapPath_).absolutePath());
    QSaveFile file(mapPath_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) <= 0) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) return false;
    QFile::setPermissions(mapPath_, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

CardMatch CardIndex::findOwner(const QByteArray &rawUid, Derive derive) {
    CardMatch result;
    if (!validUidLength(rawUid)) return result;

    const QByteArray index = (derive ? derive : noDerive)(rawUid);
    if (index.size() != 32) return result;   // 派生失败保持 IndexRejected

    // 只读整机映射表：不需要任何用户 DB key，未登录时也能选出用户。
    for (const Entry &entry : load()) {
        if (entry.revoked || entry.index != index) continue;
        result.verdict = CardMatch::Verdict::Matched;
        result.userId = entry.userId;
        return result;
    }
    result.verdict = CardMatch::Verdict::NoMatch;
    return result;
}

bool CardIndex::bind(const QString &userId, const QByteArray &rawUid, Derive derive) {
    if (userId.isEmpty() || !validUidLength(rawUid)) return false;
    const QByteArray index = (derive ? derive : noDerive)(rawUid);
    if (index.size() != 32) return false;

    QVector<Entry> entries = load();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (Entry &entry : entries) {
        if (entry.index != index) continue;
        // 已绑他人的卡拒绝：必须先由原用户或管理员挂失。
        if (!entry.revoked && entry.userId != userId) return false;
        // 同一张卡重绑：覆盖归属并清除挂失标记。
        entry.userId = userId;
        entry.boundAtMs = now;
        entry.revoked = false;
        return save(entries);
    }
    entries.append(Entry{index, userId, now, false});
    return save(entries);
}

bool CardIndex::revoke(const QString &userId, const QByteArray &rawUid, Derive derive) {
    if (userId.isEmpty() || !validUidLength(rawUid)) return false;
    const QByteArray index = (derive ? derive : noDerive)(rawUid);
    if (index.size() != 32) return false;

    QVector<Entry> entries = load();
    bool found = false;
    for (Entry &entry : entries) {
        if (entry.index != index || entry.userId != userId) continue;
        entry.revoked = true;
        found = true;
    }
    // 未命中时不写文件：避免把"没这张卡"变成一次成功的空操作。
    return found && save(entries);
}

QVector<CardIndex::BoundCard> CardIndex::boundCards(const QString &userId) const {
    QVector<BoundCard> out;
    if (userId.isEmpty()) return out;
    for (const Entry &entry : load()) {
        if (entry.revoked || entry.userId != userId) continue;
        out.append(BoundCard{entry.userId, entry.boundAtMs});
    }
    return out;
}

int CardIndex::revokeAll(const QString &userId) {
    if (userId.isEmpty()) return 0;
    QVector<Entry> entries = load();
    int count = 0;
    for (Entry &entry : entries) {
        if (entry.userId != userId || entry.revoked) continue;
        entry.revoked = true;
        ++count;
    }
    if (count > 0 && !save(entries)) return 0;
    return count;
}

// ---- CardBindingWindow ----

void CardBindingWindow::open(qint64 nowMs) { openedAtMs_ = nowMs; }

bool CardBindingWindow::isOpen(qint64 nowMs) const {
    if (openedAtMs_ < 0) return false;
    return nowMs - openedAtMs_ < kWindowMs;
}

bool CardBindingWindow::consume(qint64 nowMs) {
    // 单次：即使窗口未到期，用过一次也要关，防止连绑多张。
    if (!isOpen(nowMs)) return false;
    openedAtMs_ = -1;
    return true;
}

}  // namespace security
