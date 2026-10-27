#pragma once
#include <QByteArray>
#include <QString>

// SQLCipher C API 的最小封装。不走 QSqlDatabase（Qt 6.5.3 SDK 无 SQLCipher 驱动）。
//
// 失败语义：
//   - 找不到库/打不开 -> STORAGE_UNAVAILABLE
//   - key 不对        -> SQLITE_NOTADB(26)，映射 STORAGE_UNAVAILABLE
//   - 任何情况都不得 fallback 到明文 SQLite
struct sqlite3;

namespace StorageCode {
constexpr auto Ok = "OK";
constexpr auto Unavailable = "STORAGE_UNAVAILABLE";
}  // namespace StorageCode

class SqlCipherStore {
public:
    SqlCipherStore();
    ~SqlCipherStore();
    SqlCipherStore(const SqlCipherStore &) = delete;
    SqlCipherStore &operator=(const SqlCipherStore &) = delete;

    // key 必须是 32 字节随机值，且与 PIN 不同（PIN 只用于封装它）。
    // 打不开或 key 不对返回 false，errorCode 给出 STORAGE_UNAVAILABLE。
    bool open(const QString &path, const QByteArray &key);
    void close();

    bool isOpen() const { return db_ != nullptr; }
    QString errorCode() const { return errorCode_; }
    QString lastError() const { return lastError_; }

    // 只接受本类构造的 SQL；调用方不得把外来的 userId/表名拼进来。
    bool exec(const QString &sql);
    bool prepare(const QString &sql, void **statement);

    // 绑定/取值的最小封装，避免把 sqlite3_stmt 泄漏给上层。
    bool bindText(void *statement, int index, const QString &value);
    bool bindInt(void *statement, int index, qint64 value);
    bool bindDouble(void *statement, int index, double value);
    bool bindNull(void *statement, int index);
    bool step(void *statement);            // true = 有行；false = 结束或出错
    void finalize(void *statement);
    QString columnText(void *statement, int index) const;
    qint64 columnInt(void *statement, int index) const;
    double columnDouble(void *statement, int index) const;
    bool columnIsNull(void *statement, int index) const;

    // 供测试断言：库实际是不是 SQLCipher，而不是被冒充成 SQLite。
    QString cipherVersion() const;
    int lastSqliteCode() const { return lastCode_; }

private:
    sqlite3 *db_ = nullptr;
    QString errorCode_;
    QString lastError_;
    int lastCode_ = 0;
};
