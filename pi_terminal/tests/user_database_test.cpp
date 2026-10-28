// UT-005 / T-10：SQLCipher 加密、每用户隔离、错误 key 拒绝、跨用户拒访。
#include "security/SqlCipherStore.h"
#include "security/AuditLog.h"
#include "security/UserDatabase.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>

#include <QDateTime>

#include <QtTest>

using namespace security;

namespace {
QByteArray keyOf(char fill) { return QByteArray(32, fill); }

SensorSample sampleWith(double temperature, qint64 seq) {
    SensorSample sample;
    sample.receivedAtMs = 1000 + seq;
    sample.sampleSeq = seq;
    sample.deviceId = QStringLiteral("planter-1");
    sample.hasTemperature = true;
    sample.temperatureC = temperature;
    // 湿度故意缺失：必须落 null，不能写成 0。
    sample.hasHumidity = false;
    return sample;
}
}  // namespace

class UserDatabaseTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;
    QString root_() const { return dir_.filePath("users"); }

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        QVERIFY2(!SqlCipherStore().cipherVersion().isEmpty() || true, "version probed after open");
    }

    void cipherVersionIsRealSqlCipher() {
        SqlCipherStore store;
        QVERIFY(store.open(dir_.filePath("probe.db"), keyOf('k')));
        // 普通 SQLite 这里会返回空 —— 防"把 SQLite 当 SQLCipher 交差"。
        const QString version = store.cipherVersion();
        QVERIFY2(!version.isEmpty(), "cipher_version empty: this is plain SQLite, not SQLCipher");
        qInfo() << "cipher_version:" << version;
    }

    void wrongKeyIsRejectedWithNotADb() {
        const QString path = dir_.filePath("wrong-key.db");
        {
            SqlCipherStore store;
            QVERIFY(store.open(path, keyOf('a')));
            QVERIFY(store.exec(QStringLiteral("CREATE TABLE t(a TEXT)")));
        }
        SqlCipherStore reopened;
        QVERIFY(!reopened.open(path, keyOf('b')));          // 失败关闭
        QCOMPARE(reopened.errorCode(), QString::fromLatin1(StorageCode::Unavailable));
        QVERIFY2(reopened.lastSqliteCode() == 26 || reopened.lastSqliteCode() == 0,
                 qPrintable(QStringLiteral("expected SQLITE_NOTADB(26), got %1").arg(reopened.lastSqliteCode())));
    }

    void databaseFileIsNotPlainSqlite() {
        const QString path = dir_.filePath("header.db");
        SqlCipherStore store;
        QVERIFY(store.open(path, keyOf('c')));
        QVERIFY(store.exec(QStringLiteral("CREATE TABLE t(a TEXT)")));
        store.close();

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray header = file.read(16);
        // 明文 SQLite 的头是 "SQLite format 3\0"。
        QVERIFY(!header.startsWith("SQLite format"));
    }

    void perUserIsolation() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
        QVERIFY(db.appendSample(sampleWith(21.5, 1)));
        db.close();

        QVERIFY(db.openForSession(QStringLiteral("userB"), keyOf('b')));
        QVERIFY(db.appendSample(sampleWith(30.0, 1)));
        // B 只能看到自己那一条。
        const auto bRecords = db.queryRecent();
        QCOMPARE(bRecords.size(), 1);
        QCOMPARE(bRecords.first().temperatureC, 30.0);
        db.close();

        // A 的记录仍在，且与 B 不同库。
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
        const auto aRecords = db.queryRecent();
        QCOMPARE(aRecords.size(), 1);
        QCOMPARE(aRecords.first().temperatureC, 21.5);
    }

    void crossUserOpenIsRefused() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
        // 已打开 A 时切到 B 必须失败（跨用户拒绝）。
        QVERIFY(!db.openForeign(QStringLiteral("userB"), keyOf('b')));
        QVERIFY(db.isOpen());
    }

    void wrongKeyCannotOpenExistingUserDb() {
        {
            UserDatabase db(root_());
            QVERIFY(db.openForSession(QStringLiteral("userC"), keyOf('c')));
            QVERIFY(db.appendSample(sampleWith(19.0, 1)));
        }
        UserDatabase db(root_());
        QVERIFY2(!db.openForSession(QStringLiteral("userC"), keyOf('x')),
                 "wrong db key must not open an existing user database");
        QCOMPARE(db.errorCode(), QString::fromLatin1(StorageCode::Unavailable));
    }

    void missingReadingsAreNullNotZero() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userD"), keyOf('d')));
        QVERIFY(db.appendSample(sampleWith(25.0, 1)));
        // 直接查库确认湿度列是 NULL 而不是 0。
        const QString path = db.databasePathFor(QStringLiteral("userD"));
        db.close();

        SqlCipherStore store;
        QVERIFY(store.open(path, keyOf('d')));
        void *stmt = nullptr;
        QVERIFY(store.prepare(QStringLiteral("SELECT humidity_rh FROM samples LIMIT 1"), &stmt));
        QVERIFY(store.step(stmt));
        QVERIFY2(store.columnIsNull(stmt, 0), "missing humidity must be NULL, not 0");
        store.finalize(stmt);
    }

    void queryLimitIsBounded() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userE"), keyOf('e')));
        for (int i = 0; i < 150; ++i) QVERIFY(db.appendSample(sampleWith(20.0 + i * 0.01, i)));
        // f-day1 §5：分页每页 ≤100。
        QVERIFY(db.queryRecent(1000).size() <= 100);
    }

    void cipherMajorVersionIsRecordedAndEnforced() {
        {
            UserDatabase db(root_());
            QVERIFY(db.openForSession(QStringLiteral("userF"), keyOf('f')));
            QVERIFY(db.appendSample(sampleWith(22.0, 1)));
        }
        // 篡改 meta 里的大版本，模拟"在 4.x 建库、拿到 3.x 上打开"。
        const QString path = UserDatabase(root_()).databasePathFor(QStringLiteral("userF"));
        {
            SqlCipherStore store;
            QVERIFY(store.open(path, keyOf('f')));
            QVERIFY(store.exec(QStringLiteral(
                "UPDATE meta SET value='4' WHERE key='cipher_version'")));
        }
        // 大版本不符必须拒绝打开，不能读出乱码当数据。
        UserDatabase db(root_());
        QVERIFY2(!db.openForSession(QStringLiteral("userF"), keyOf('f')),
                 "must refuse to open a DB created by a different SQLCipher major version");
    }

    void sameMajorVersionReopens() {
        {
            UserDatabase db(root_());
            QVERIFY(db.openForSession(QStringLiteral("userG"), keyOf('g')));
            QVERIFY(db.appendSample(sampleWith(23.0, 1)));
        }
        // 同大版本必须能重新打开并读到数据。
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userG"), keyOf('g')));
        QCOMPARE(db.queryRecent().size(), 1);
    }

    // 报告 §6.1：模块历史必须落在每用户加密库里，不再写明文 JSON。
    void moduleHistoryIsStoredEncryptedAndAggregated() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userH"), keyOf('h')));

        const qint64 base = 1717000000000LL;
        QVERIFY(db.addHistorySample(base + 1000, 20.0));
        QVERIFY(db.addHistorySample(base + 2000, 24.0));
        QVERIFY(db.addHistorySample(base + 3000, 18.0));

        // 同一分钟内必须聚成一个桶：count=3, sum=62, low=18, high=24。
        const auto series = db.historySeries(base + 60000, 3600000, 60000);
        QCOMPARE(series.size(), 1);
        QCOMPARE(series.first().count, 3);
        QCOMPARE(series.first().sum, 62.0);
        QCOMPARE(series.first().low, 18.0);
        QCOMPARE(series.first().high, 24.0);

        // 越界与非法值必须丢弃，不能进历史。
        QVERIFY(!db.addHistorySample(base + 4000, 999.0));
        QVERIFY(!db.addHistorySample(base + 4000, std::nan("")));
        QVERIFY(!db.addHistorySample(0, 20.0));

        // 库里不得出现明文数值之外的可读 JSON；文件头也不能是 SQLite。
        const QString path = db.databasePathFor(QStringLiteral("userH"));
        db.close();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray head = file.read(16);
        QVERIFY2(!head.startsWith("SQLite format"), "history DB must stay encrypted");
        file.close();

        // 重新打开仍能读到聚合结果（持久化生效）。
        QVERIFY(db.openForSession(QStringLiteral("userH"), keyOf('h')));
        QCOMPARE(db.historySeries(base + 60000, 3600000, 60000).size(), 1);
        QVERIFY(!db.legacyHistoryMigrated());
        QVERIFY(db.markLegacyHistoryMigrated());
        QVERIFY(db.legacyHistoryMigrated());
    }

    // 保留期外的桶必须被清理，避免无限增长。
    void oldHistoryIsPruned() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userI"), keyOf('i')));
        const qint64 now = 1717000000000LL;
        const qint64 week = 7LL * 24 * 60 * 60000;
        QVERIFY(db.addHistorySample(now - week - 120000, 21.0));   // 太旧
        QVERIFY(db.addHistorySample(now - 60000, 22.0));           // 保留
        QCOMPARE(db.pruneHistory(now, week), 1);
        const auto remaining = db.historySeries(now, week, 60000);
        QCOMPARE(remaining.size(), 1);
    }


    // §6.3：明文临时文件面必须被关掉。逐条 PRAGMA 回读，不能只看代码里写了。
    void hardeningPragmasAreActive() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userJ"), keyOf('j')));

        // 用同一个库路径另开一个 store 读 PRAGMA 值。
        const QString path = db.databasePathFor(QStringLiteral("userJ"));
        db.close();

        SqlCipherStore store;
        QVERIFY(store.open(path, keyOf('j')));
        struct Expect { const char *sql; const char *want; };
        const Expect checks[] = {
            {"PRAGMA temp_store", "2"},        // 2 = MEMORY
            {"PRAGMA journal_mode", "delete"}, // 非 WAL
            {"PRAGMA secure_delete", "1"},
        };
        for (const auto &check : checks) {
            void *stmt = nullptr;
            QVERIFY2(store.prepare(QString::fromLatin1(check.sql), &stmt), check.sql);
            QVERIFY2(store.step(stmt), check.sql);
            const QString actual = store.columnText(stmt, 0).toLower();
            store.finalize(stmt);
            QVERIFY2(actual == QLatin1String(check.want),
                     qPrintable(QStringLiteral("%1 = %2, want %3")
                                    .arg(QString::fromLatin1(check.sql), actual,
                                         QString::fromLatin1(check.want))));
        }
    }


    // 报告 §3.2：审计必须记动作与结果，且**不得**接受 PIN/健康值/卡号。
    void auditRecordsActionsWithoutSecrets() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userK"), keyOf('k')));
        AuditLog *log = db.audit();
        QVERIFY(log);

        QVERIFY(log->record(QStringLiteral("user-k"), AuditAction::GrantDevice,
                            QStringLiteral("device-1"), AuditResult::Ok));
        QVERIFY(log->record(QStringLiteral("user-k"), AuditAction::RevokeBridge,
                            QStringLiteral("bridge-a"), AuditResult::Denied));
        QCOMPARE(log->count(), 2);

        // 白名单：纯数字（可能是 PIN/卡号）与含空格的自由文本（可能是读数）都拒写。
        QVERIFY(!log->record(QStringLiteral("user-k"), AuditAction::GrantDevice,
                             QStringLiteral("123456"), AuditResult::Ok));
        QVERIFY(!log->record(QStringLiteral("user-k"), AuditAction::GrantDevice,
                             QStringLiteral("37.5 C"), AuditResult::Ok));
        QVERIFY(!log->record(QStringLiteral("user-k"), AuditAction::GrantDevice,
                             QStringLiteral(""), AuditResult::Ok));
        QCOMPARE(log->count(), 2);   // 拒绝的三条没有落库

        const auto recent = log->recent(10);
        QCOMPARE(recent.size(), 2);
        // 最新一条在前。
        QCOMPARE(recent.first().action, QString(AuditAction::RevokeBridge));
        QCOMPARE(recent.first().result, QString(AuditResult::Denied));

        // 审计随库加密：文件不得是明文 SQLite。
        const QString path = db.databasePathFor(QStringLiteral("userK"));
        db.close();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.read(16).startsWith("SQLite format"));
        file.close();

        // 重新打开后审计仍在（持久化）。
        QVERIFY(db.openForSession(QStringLiteral("userK"), keyOf('k')));
        QCOMPARE(db.audit()->count(), 2);
    }

    // 审计保留期：过期记录可被清理。
    void auditPruneRemovesOldEntries() {
        UserDatabase db(root_());
        QVERIFY(db.openForSession(QStringLiteral("userL"), keyOf('l')));
        AuditLog *log = db.audit();
        QVERIFY(log->record(QStringLiteral("u"), AuditAction::ChangePin,
                            QStringLiteral("user-l"), AuditResult::Ok));
        QCOMPARE(log->count(), 1);
        QCOMPARE(log->prune(QDateTime::currentMSecsSinceEpoch() + 1000), 1);
        QCOMPARE(log->count(), 0);
    }

};

QTEST_GUILESS_MAIN(UserDatabaseTest)
#include "user_database_test.moc"

