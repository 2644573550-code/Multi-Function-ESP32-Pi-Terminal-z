#include "security/SqlCipherStore.h"


#include <sqlite3.h>

namespace security {


SqlCipherStore::SqlCipherStore() = default;

SqlCipherStore::~SqlCipherStore() { close(); }

bool SqlCipherStore::open(const QString &path, const QByteArray &key) {
    close();
    errorCode_.clear();
    lastError_.clear();
    if (key.size() != 32) {
        errorCode_ = StorageCode::Unavailable;
        lastError_ = QStringLiteral("key must be 32 bytes");
        return false;
    }

    int rc = sqlite3_open(path.toUtf8().constData(), &db_);
    if (rc != SQLITE_OK) {
        lastCode_ = rc;
        lastError_ = db_ ? QString::fromUtf8(sqlite3_errmsg(db_)) : QStringLiteral("open failed");
        errorCode_ = StorageCode::Unavailable;
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }

    // 先设 key 再第一次访问；顺序反了会拿到明文错误。
    sqlite3_key(db_, key.constData(), key.size());
    sqlite3_exec(db_, "PRAGMA cipher_memory_security = ON", nullptr, nullptr, nullptr);

    // 必须真正读一页才知道 key 对不对：sqlite3_open 本身永远成功。
    rc = sqlite3_exec(db_, "SELECT count(*) FROM sqlite_master", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        lastCode_ = rc;
        // 错误 key -> 26 (SQLITE_NOTADB)。不允许在此退回明文。
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        errorCode_ = StorageCode::Unavailable;
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    return true;
}

void SqlCipherStore::close() {
    if (!db_) return;
    sqlite3_close(db_);
    db_ = nullptr;
}

bool SqlCipherStore::exec(const QString &sql) {
    if (!db_) return false;
    char *message = nullptr;
    const int rc = sqlite3_exec(db_, sql.toUtf8().constData(), nullptr, nullptr, &message);
    lastCode_ = rc;
    if (rc != SQLITE_OK) {
        lastError_ = message ? QString::fromUtf8(message) : QString::fromUtf8(sqlite3_errmsg(db_));
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool SqlCipherStore::prepare(const QString &sql, void **statement) {
    if (!db_ || !statement) return false;
    sqlite3_stmt *stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.toUtf8().constData(), -1, &stmt, nullptr);
    lastCode_ = rc;
    if (rc != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }
    *statement = stmt;
    return true;
}

bool SqlCipherStore::bindText(void *statement, int index, const QString &value) {
    return sqlite3_bind_text(static_cast<sqlite3_stmt *>(statement), index, value.toUtf8().constData(),
                             -1, SQLITE_TRANSIENT) == SQLITE_OK;
}
bool SqlCipherStore::bindInt(void *statement, int index, qint64 value) {
    return sqlite3_bind_int64(static_cast<sqlite3_stmt *>(statement), index, value) == SQLITE_OK;
}
bool SqlCipherStore::bindDouble(void *statement, int index, double value) {
    return sqlite3_bind_double(static_cast<sqlite3_stmt *>(statement), index, value) == SQLITE_OK;
}
bool SqlCipherStore::bindNull(void *statement, int index) {
    return sqlite3_bind_null(static_cast<sqlite3_stmt *>(statement), index) == SQLITE_OK;
}

bool SqlCipherStore::step(void *statement) {
    if (!statement) return false;
    const int rc = sqlite3_step(static_cast<sqlite3_stmt *>(statement));
    lastCode_ = rc;
    // SQLITE_ROW 才算有行；DONE 与任何错误都返回 false。
    return rc == SQLITE_ROW;
}

void SqlCipherStore::finalize(void *statement) {
    if (statement) sqlite3_finalize(static_cast<sqlite3_stmt *>(statement));
}

QString SqlCipherStore::columnText(void *statement, int index) const {
    const auto *text = sqlite3_column_text(static_cast<sqlite3_stmt *>(statement), index);
    return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}
qint64 SqlCipherStore::columnInt(void *statement, int index) const {
    return sqlite3_column_int64(static_cast<sqlite3_stmt *>(statement), index);
}
double SqlCipherStore::columnDouble(void *statement, int index) const {
    return sqlite3_column_double(static_cast<sqlite3_stmt *>(statement), index);
}
bool SqlCipherStore::columnIsNull(void *statement, int index) const {
    return sqlite3_column_type(static_cast<sqlite3_stmt *>(statement), index) == SQLITE_NULL;
}

QString SqlCipherStore::cipherVersion() const {
    if (!db_) return {};
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA cipher_version", -1, &stmt, nullptr) != SQLITE_OK) return {};
    QString version;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *text = sqlite3_column_text(stmt, 0);
        if (text) version = QString::fromUtf8(reinterpret_cast<const char *>(text));
    }
    sqlite3_finalize(stmt);
    return version;
}

}  // namespace security
