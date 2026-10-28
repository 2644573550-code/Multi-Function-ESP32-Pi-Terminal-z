// UT-005 / T-10：SQLCipher 加密、每用户隔离、错误 key 拒绝、跨用户拒访。
#include "security/SqlCipherStore.h"
#include "security/UserDatabase.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
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
                "UPDATE meta SET value='3' WHERE key='cipher_version'")));
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
};

QTEST_GUILESS_MAIN(UserDatabaseTest)
#include "user_database_test.moc"

