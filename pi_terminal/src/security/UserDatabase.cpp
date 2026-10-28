#include "security/UserDatabase.h"
#include "security/AuditLog.h"

#include <cmath>

#include "security/SqlCipherStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <sqlite3.h>

namespace security {


namespace {

// 不放 AppLocalDataLocation：那是 ModuleManager::dataDirectory() 交给模块的目录。
QString defaultRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/users");
}

// schema 版本随库文件走，便于将来迁移时不猜兼容。
constexpr int kSchemaVersion = 1;

const char *kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS samples(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    received_at_ms INTEGER NOT NULL,
    sample_seq INTEGER NOT NULL,
    device_id TEXT NOT NULL,
    temperature_c REAL,
    humidity_rh REAL,
    pressure_hpa REAL,
    light_lux REAL,
    battery_v REAL,
    battery_pct INTEGER
);
CREATE INDEX IF NOT EXISTS samples_by_time ON samples(received_at_ms);
-- 模块历史的可信聚合（替代模块侧明文 JSON，报告 §6.1）。
-- 一条记录 = 一个按分钟对齐的桶；模块只发原始观测，聚合在宿主完成。
CREATE TABLE IF NOT EXISTS module_history(
    bucket_ms INTEGER PRIMARY KEY,
    sum_value REAL NOT NULL,
    low_value REAL NOT NULL,
    high_value REAL NOT NULL,
    sample_count INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS module_history_by_time ON module_history(bucket_ms);
-- 审计（报告 §3.2）：只记动作与结果，不记任何值。
-- 禁止此表出现 PIN、token、卡号、健康数值；写入侧有白名单校验。
CREATE TABLE IF NOT EXISTS audit(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    at_ms INTEGER NOT NULL,
    actor TEXT NOT NULL,
    action TEXT NOT NULL,
    target TEXT NOT NULL,
    result TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS audit_by_time ON audit(at_ms);
)SQL";

}  // namespace

UserDatabase::UserDatabase(QString root) : root_(root.isEmpty() ? defaultRoot() : std::move(root)) {}

UserDatabase::~UserDatabase() = default;

QString UserDatabase::databasePathFor(const QString &userId) const {
    return root_ + QLatin1Char('/') + userId + QStringLiteral("/data.db");
}

bool UserDatabase::openForSession(const QString &userId, const QByteArray &dbKey) {
    close();
    // userId 不透明且由服务端给定；仍做一次字符集检查，防止路径穿越。
    if (userId.isEmpty()) return false;
    for (const QChar c : userId) {
        if (!(c.isLetterOrNumber() && c.unicode() < 128)) return false;
    }
    const QString path = databasePathFor(userId);
    QDir().mkpath(QFileInfo(path).absolutePath());

    store_ = new SqlCipherStore;
    if (!store_->open(path, dbKey)) {
        delete store_;
        store_ = nullptr;
        return false;
    }
    if (!store_->exec(QString::fromLatin1(kSchema))) {
        close();
        return false;
    }
    // 记录 schema 版本与锁定的 SQLCipher 大版本（§0.1：DB 不跨大版本互换）。
    store_->exec(QStringLiteral("INSERT OR IGNORE INTO meta(key, value) VALUES('schema_version', '%1')")
                     .arg(kSchemaVersion));
    const QString cipher = store_->cipherVersion();
    if (!cipher.isEmpty()) {
        const QString major = cipher.section(QLatin1Char(' '), 0, 0).section(QLatin1Char('.'), 0, 0);
        // 已有库记着别的大版本就必须拒绝打开：4.x 与 3.x 的默认 KDF 参数不同，
        // 强行打开会得到乱码而不是错误，比失败更危险。
        if (!checkCipherMajor(major)) {
            close();
            return false;
        }
        store_->exec(QStringLiteral("INSERT OR IGNORE INTO meta(key, value) VALUES('cipher_version', '%1')")
                         .arg(major));
    }
    openUserId_ = userId;
    return true;
}

bool UserDatabase::checkCipherMajor(const QString &currentMajor) {
    if (currentMajor.isEmpty()) return true;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral("SELECT value FROM meta WHERE key='cipher_version'"), &stmt))
        return true;   // 新库还没有这条记录
    bool ok = true;
    if (store_->step(stmt)) {
        const QString stored = store_->columnText(stmt, 0).section(QLatin1Char('.'), 0, 0);
        // 只比较大版本；小版本差异由 SQLCipher 自己兼容。
        if (!stored.isEmpty() && stored != currentMajor) ok = false;
    }
    store_->finalize(stmt);
    return ok;
}

void UserDatabase::close() {
    delete audit_;
    audit_ = nullptr;
    delete store_;
    store_ = nullptr;
    openUserId_.clear();
}

bool UserDatabase::isOpen() const { return store_ && store_->isOpen(); }

QString UserDatabase::errorCode() const {
    return store_ ? store_->errorCode() : QString::fromLatin1(StorageCode::Unavailable);
}

bool UserDatabase::appendSample(const SensorSample &sample) {
    if (!isOpen()) return false;
    // 归属由打开的会话决定，不接受 sample 里自报的 userId（本类没有这个字段）。
    const QString sql = QStringLiteral(
        "INSERT INTO samples(received_at_ms, sample_seq, device_id, temperature_c, humidity_rh,"
        " pressure_hpa, light_lux, battery_v, battery_pct)"
        " VALUES(?,?,?,?,?,?,?,?,?)");
    void *stmt = nullptr;
    if (!store_->prepare(sql, &stmt)) return false;
    store_->bindInt(stmt, 1, sample.receivedAtMs);
    store_->bindInt(stmt, 2, sample.sampleSeq);
    store_->bindText(stmt, 3, sample.deviceId);
    // 缺失值必须写 null，不能写 0：0 是有效读数。
    if (sample.hasTemperature) store_->bindDouble(stmt, 4, sample.temperatureC); else store_->bindNull(stmt, 4);
    if (sample.hasHumidity) store_->bindDouble(stmt, 5, sample.humidityRh); else store_->bindNull(stmt, 5);
    if (sample.hasPressure) store_->bindDouble(stmt, 6, sample.pressureHpa); else store_->bindNull(stmt, 6);
    if (sample.hasLight) store_->bindDouble(stmt, 7, sample.lightLux); else store_->bindNull(stmt, 7);
    if (sample.hasBatteryV) store_->bindDouble(stmt, 8, sample.batteryV); else store_->bindNull(stmt, 8);
    if (sample.hasBatteryPct) store_->bindInt(stmt, 9, sample.batteryPct); else store_->bindNull(stmt, 9);
    const bool ok = !store_->step(stmt) && store_->lastSqliteCode() == 101;  // 101 = SQLITE_DONE
    store_->finalize(stmt);
    return ok;
}

QVector<UserDatabase::Record> UserDatabase::queryRecent(int limit) const {
    QVector<Record> records;
    if (!isOpen() || limit <= 0) return records;
    // 分页上限 100（f-day1 §5）。
    const int bounded = limit > 100 ? 100 : limit;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "SELECT received_at_ms, sample_seq, device_id, temperature_c FROM samples"
            " ORDER BY received_at_ms DESC LIMIT ?"), &stmt))
        return records;
    store_->bindInt(stmt, 1, bounded);
    while (store_->step(stmt)) {
        Record record;
        record.receivedAtMs = store_->columnInt(stmt, 0);
        record.sampleSeq = store_->columnInt(stmt, 1);
        record.deviceId = store_->columnText(stmt, 2);
        record.hasTemperature = !store_->columnIsNull(stmt, 3);
        if (record.hasTemperature) record.temperatureC = store_->columnDouble(stmt, 3);
        records.append(record);
    }
    store_->finalize(stmt);
    return records;
}

AuditLog *UserDatabase::audit() {
    if (!isOpen()) return nullptr;
    if (!audit_) audit_ = new AuditLog(store_);
    return audit_;
}

bool UserDatabase::addHistorySample(qint64 observedAtMs, double value) {
    if (!isOpen() || observedAtMs <= 0) return false;
    // 与模块侧相同的物理合理区间；越界样本丢弃而不是写进历史。
    if (!std::isfinite(value) || value < -50.0 || value > 100.0) return false;

    const qint64 bucket = observedAtMs / 60000 * 60000;
    void *stmt = nullptr;
    // 一条 UPSERT 完成"并入当前分钟桶"。SQLCipher 3.4.1 基于 SQLite 3.15.2，
    // 不支持 ON CONFLICT DO UPDATE，因此走"先查后写"两步。
    if (!store_->prepare(QStringLiteral(
            "SELECT sum_value, low_value, high_value, sample_count FROM module_history"
            " WHERE bucket_ms = ?"), &stmt))
        return false;
    store_->bindInt(stmt, 1, bucket);
    bool exists = false;
    double sum = 0, low = value, high = value;
    int count = 0;
    if (store_->step(stmt)) {
        exists = true;
        sum = store_->columnDouble(stmt, 0);
        low = store_->columnDouble(stmt, 1);
        high = store_->columnDouble(stmt, 2);
        count = int(store_->columnInt(stmt, 3));
    }
    store_->finalize(stmt);

    sum += value;
    low = qMin(low, value);
    high = qMax(high, value);
    ++count;

    const QString sql = exists
        ? QStringLiteral("UPDATE module_history SET sum_value=?, low_value=?, high_value=?,"
                         " sample_count=? WHERE bucket_ms=?")
        : QStringLiteral("INSERT INTO module_history(bucket_ms, sum_value, low_value,"
                         " high_value, sample_count) VALUES(?,?,?,?,?)");
    if (!store_->prepare(sql, &stmt)) return false;

    const bool update = sql.startsWith(QLatin1String("UPDATE"));
    int index = 1;
    if (!update) store_->bindInt(stmt, index++, bucket);
    store_->bindDouble(stmt, index++, sum);
    store_->bindDouble(stmt, index++, low);
    store_->bindDouble(stmt, index++, high);
    store_->bindInt(stmt, index++, count);
    if (update) store_->bindInt(stmt, index, bucket);

    const bool ok = !store_->step(stmt) && store_->lastSqliteCode() == 101;   // SQLITE_DONE
    store_->finalize(stmt);
    return ok;
}

QVector<UserDatabase::HistoryBucket> UserDatabase::historySeries(
    qint64 nowMs, qint64 spanMs, qint64 stepMs) const {
    QVector<HistoryBucket> out;
    if (!isOpen() || stepMs <= 0 || spanMs <= 0) return out;

    const qint64 from = nowMs - spanMs;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "SELECT bucket_ms, sum_value, low_value, high_value, sample_count"
            " FROM module_history WHERE bucket_ms >= ? AND bucket_ms <= ?"
            " ORDER BY bucket_ms"), &stmt))
        return out;
    store_->bindInt(stmt, 1, from);
    store_->bindInt(stmt, 2, nowMs);

    // 按 stepMs 分组聚合，与模块侧 History::series 的语义保持一致。
    QMap<qint64, HistoryBucket> groups;
    while (store_->step(stmt)) {
        const qint64 bucket = store_->columnInt(stmt, 0);
        const qint64 groupKey = bucket / stepMs * stepMs;
        HistoryBucket &g = groups[groupKey];
        g.bucketMs = groupKey;
        g.sum += store_->columnDouble(stmt, 1);
        const double low = store_->columnDouble(stmt, 2);
        const double high = store_->columnDouble(stmt, 3);
        if (g.count == 0) { g.low = low; g.high = high; }
        else { g.low = qMin(g.low, low); g.high = qMax(g.high, high); }
        g.count += int(store_->columnInt(stmt, 4));
    }
    store_->finalize(stmt);

    out.reserve(groups.size());
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) out.append(it.value());
    return out;
}

int UserDatabase::pruneHistory(qint64 nowMs, qint64 retentionMs) {
    if (!isOpen() || retentionMs <= 0) return 0;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral("DELETE FROM module_history WHERE bucket_ms < ?"), &stmt))
        return 0;
    store_->bindInt(stmt, 1, nowMs - retentionMs);
    const bool ok = !store_->step(stmt) && store_->lastSqliteCode() == 101;
    store_->finalize(stmt);
    return ok ? store_->changes() : 0;
}

bool UserDatabase::legacyHistoryMigrated() const {
    if (!isOpen()) return false;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "SELECT value FROM meta WHERE key='legacy_history_migrated'"), &stmt))
        return false;
    const bool migrated = store_->step(stmt) && store_->columnText(stmt, 0) == QLatin1String("1");
    store_->finalize(stmt);
    return migrated;
}

bool UserDatabase::markLegacyHistoryMigrated() {
    if (!isOpen()) return false;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "INSERT OR REPLACE INTO meta(key, value) VALUES('legacy_history_migrated','1')"), &stmt))
        return false;
    const bool ok = !store_->step(stmt) && store_->lastSqliteCode() == 101;
    store_->finalize(stmt);
    return ok;
}

bool UserDatabase::openForeign(const QString &userId, const QByteArray &dbKey) {
    // 已经打开 A 的会话时，不得切到 B —— 跨用户访问必须失败。
    if (isOpen() && openUserId_ != userId) return false;
    return openForSession(userId, dbKey);
}

QString UserDatabase::cipherVersion() const { return store_ ? store_->cipherVersion() : QString(); }


}  // namespace security
