#include "security/AuditLog.h"

#include "security/SqlCipherStore.h"

#include <sqlite3.h>

#include <QDateTime>

namespace security {

namespace {

// 审计字段的允许字符集：不透明 ID、动作名、结果名。
// 明确禁止：数字串（可能是 PIN）、含空格的自由文本（可能是读数或正文）。
bool charIsSafe(QChar c) {
    if (c.isLetterOrNumber() && c.unicode() < 128) return true;
    switch (c.unicode()) {
    case '.': case '_': case '-': case ':': case '@':
        return true;
    default:
        return false;
    }
}

}  // namespace

bool auditFieldIsSafe(const QString &value) {
    if (value.isEmpty() || value.size() > 128) return false;
    for (const QChar c : value)
        if (!charIsSafe(c)) return false;
    // 纯数字串一律拒绝：那可能是 PIN，也可能是卡号。
    bool allDigits = true;
    for (const QChar c : value)
        if (!c.isDigit()) { allDigits = false; break; }
    return !allDigits;
}

bool AuditLog::append(const AuditEntry &entry) {
    if (!store_ || !store_->isOpen()) return false;
    // 写入前逐字段校验：宁可拒写，也不让敏感值进审计表。
    if (!auditFieldIsSafe(entry.actor) || !auditFieldIsSafe(entry.action)
        || !auditFieldIsSafe(entry.target) || !auditFieldIsSafe(entry.result))
        return false;

    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "INSERT INTO audit(at_ms, actor, action, target, result) VALUES(?,?,?,?,?)"),
            &stmt))
        return false;
    store_->bindInt(stmt, 1, entry.atMs > 0 ? entry.atMs : QDateTime::currentMSecsSinceEpoch());
    // 依次绑定字符串。SqlCipherStore 的 bind 接口以 void* 语句为准。
    struct Binder {
        static bool apply(void *statement, int index, const QString &value) {
            auto *raw = static_cast<sqlite3_stmt *>(statement);
            return sqlite3_bind_text(raw, index, value.toUtf8().constData(), -1,
                                     SQLITE_TRANSIENT) == SQLITE_OK;
        }
    };
    const bool bound = Binder::apply(stmt, 2, entry.actor)
        && Binder::apply(stmt, 3, entry.action)
        && Binder::apply(stmt, 4, entry.target)
        && Binder::apply(stmt, 5, entry.result);
    if (!bound) { store_->finalize(stmt); return false; }

    const bool ok = !store_->step(stmt) && store_->lastSqliteCode() == 101;   // SQLITE_DONE
    store_->finalize(stmt);
    return ok;
}

bool AuditLog::record(const QString &actor, const QString &action, const QString &target,
                      const QString &result) {
    AuditEntry entry;
    entry.atMs = QDateTime::currentMSecsSinceEpoch();
    entry.actor = actor;
    entry.action = action;
    entry.target = target;
    entry.result = result;
    return append(entry);
}

QVector<AuditEntry> AuditLog::recent(int limit) const {
    QVector<AuditEntry> out;
    if (!store_ || !store_->isOpen() || limit <= 0) return out;
    const int bounded = limit > 500 ? 500 : limit;

    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "SELECT at_ms, actor, action, target, result FROM audit"
            " ORDER BY at_ms DESC, id DESC LIMIT ?"), &stmt))
        return out;
    store_->bindInt(stmt, 1, bounded);
    while (store_->step(stmt)) {
        AuditEntry entry;
        entry.atMs = store_->columnInt(stmt, 0);
        entry.actor = store_->columnText(stmt, 1);
        entry.action = store_->columnText(stmt, 2);
        entry.target = store_->columnText(stmt, 3);
        entry.result = store_->columnText(stmt, 4);
        out.append(entry);
    }
    store_->finalize(stmt);
    return out;
}

int AuditLog::prune(qint64 beforeMs) {
    if (!store_ || !store_->isOpen()) return 0;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral("DELETE FROM audit WHERE at_ms < ?"), &stmt)) return 0;
    store_->bindInt(stmt, 1, beforeMs);
    const bool ok = !store_->step(stmt) && store_->lastSqliteCode() == 101;
    store_->finalize(stmt);
    return ok ? store_->changes() : 0;
}

int AuditLog::count() const {
    if (!store_ || !store_->isOpen()) return 0;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral("SELECT count(*) FROM audit"), &stmt)) return 0;
    int total = 0;
    if (store_->step(stmt)) total = int(store_->columnInt(stmt, 0));
    store_->finalize(stmt);
    return total;
}

}  // namespace security
