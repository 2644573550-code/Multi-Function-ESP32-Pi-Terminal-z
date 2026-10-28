#include "security/UserDatabase.h"

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
-- 卡索引：只存 UID 索引（HMAC 输出），不存 raw UID。
-- 放在每用户库里按 §0.6 原文；代价见 CardIndex::findOwner 的注释。
CREATE TABLE IF NOT EXISTS card_index(
    uid_index BLOB PRIMARY KEY,
    added_at_ms INTEGER NOT NULL,
    revoked INTEGER NOT NULL DEFAULT 0
);
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

bool UserDatabase::openForeign(const QString &userId, const QByteArray &dbKey) {
    // 已经打开 A 的会话时，不得切到 B —— 跨用户访问必须失败。
    if (isOpen() && openUserId_ != userId) return false;
    return openForSession(userId, dbKey);
}

QString UserDatabase::cipherVersion() const { return store_ ? store_->cipherVersion() : QString(); }

bool UserDatabase::bindCard(const QByteArray &uidIndex) {
    if (!isOpen() || uidIndex.size() != 32) return false;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "INSERT INTO card_index(uid_index, added_at_ms, revoked) VALUES(?,?,0)"
            " ON CONFLICT(uid_index) DO UPDATE SET revoked=0"), &stmt))
        return false;
    // sqlite3_bind_blob 需要原始指针，这里经 exec 风格的封装不可行，直接调用。
    struct BindBlob {
        static bool apply(void *statement, int index, const QByteArray &value) {
            auto *stmt = static_cast<sqlite3_stmt *>(statement);
            return sqlite3_bind_blob(stmt, index, value.constData(), value.size(), SQLITE_TRANSIENT)
                == SQLITE_OK;
        }
    };
    if (!BindBlob::apply(stmt, 1, uidIndex)) { store_->finalize(stmt); return false; }
    store_->bindInt(stmt, 2, QDateTime::currentMSecsSinceEpoch());
    const bool ok = !store_->step(stmt) && store_->lastSqliteCode() == 101;
    store_->finalize(stmt);
    return ok;
}

bool UserDatabase::revokeCard(const QByteArray &uidIndex) {
    if (!isOpen() || uidIndex.size() != 32) return false;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "UPDATE card_index SET revoked=1 WHERE uid_index=?"), &stmt))
        return false;
    auto *raw = static_cast<sqlite3_stmt *>(stmt);
    if (sqlite3_bind_blob(raw, 1, uidIndex.constData(), uidIndex.size(), SQLITE_TRANSIENT) != SQLITE_OK) {
        store_->finalize(stmt);
        return false;
    }
    const bool ok = !store_->step(stmt) && store_->lastSqliteCode() == 101;
    store_->finalize(stmt);
    return ok;
}

bool UserDatabase::hasCard(const QByteArray &uidIndex) const {
    if (!isOpen() || uidIndex.size() != 32) return false;
    void *stmt = nullptr;
    if (!store_->prepare(QStringLiteral(
            "SELECT 1 FROM card_index WHERE uid_index=? AND revoked=0"), &stmt))
        return false;
    auto *raw = static_cast<sqlite3_stmt *>(stmt);
    if (sqlite3_bind_blob(raw, 1, uidIndex.constData(), uidIndex.size(), SQLITE_TRANSIENT) != SQLITE_OK) {
        store_->finalize(stmt);
        return false;
    }
    const bool found = store_->step(stmt);
    store_->finalize(stmt);
    return found;
}

}  // namespace security
