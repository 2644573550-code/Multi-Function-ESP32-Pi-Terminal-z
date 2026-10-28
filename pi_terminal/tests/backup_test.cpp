// 报告 §7.1 / §7.2：个人备份必须能真正恢复，且不得跨用户。
#include "security/Backup.h"
#include "security/UserDatabase.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace security;

class BackupTest : public QObject {
    Q_OBJECT

private:
    static QByteArray keyOf(char c) { return QByteArray(32, c); }
    static constexpr qint64 kDay = 24LL * 60 * 60 * 1000;

private slots:
    // 备份再恢复：数据回来，且备份文件本身是密文。
    void createAndRestoreRoundTrips()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString backupPath = dir.filePath("backup.optbk");
        const QString password = QStringLiteral("correct horse battery");

        // 用当前时间：备份导出按「最近 365 天」筛选，固定旧的 2024 年时间戳
        // 会被合法地过滤掉，导致恢复后为空（那是正确行为，不是 bug）。
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        {
            UserDatabase db(dir.filePath("users"));
            QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
            QVERIFY(db.addHistorySample(now - kDay, 21.5));
            QVERIFY(db.addHistorySample(now - 60000, 23.0));
            QCOMPARE(Backup::create(db, backupPath, password), Backup::Result::Ok);
            db.close();
        }

        // 备份文件必须是密文：不含明文数值，也不是 SQLite 头。
        QFile file(backupPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray raw = file.readAll();
        file.close();
        QVERIFY2(raw.startsWith(QByteArray("OPTBK001", 8)), "backup must carry the magic");
        QVERIFY2(!raw.contains("21.5"), "backup must not contain plaintext values");
        QVERIFY(!raw.contains("SQLite format"));

        // 恢复进一个新的空库。
        {
            UserDatabase db(dir.filePath("restore"));
            QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
            QCOMPARE(Backup::restore(&db, backupPath, password, QStringLiteral("userA")),
                     Backup::Result::Ok);
            const auto series = db.historySeries(now, 7 * kDay, 60000);
            QCOMPARE(series.size(), 2);
        }
    }

    // 错误密码必须失败，且不能写进库里。
    void wrongPasswordIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString backupPath = dir.filePath("backup.optbk");

        {
            UserDatabase db(dir.filePath("users"));
            QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
            QVERIFY(db.addHistorySample(QDateTime::currentMSecsSinceEpoch() - 60000, 20.0));
            QCOMPARE(Backup::create(db, backupPath, QStringLiteral("right password")),
                     Backup::Result::Ok);
        }

        UserDatabase db(dir.filePath("restore"));
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
        QCOMPARE(Backup::restore(&db, backupPath, QStringLiteral("wrong password"),
                                 QStringLiteral("userA")),
                 Backup::Result::BadPassword);
        QVERIFY(db.historySeries(QDateTime::currentMSecsSinceEpoch(), 7 * kDay, 60000).isEmpty());
    }

    // §7.2：A 的备份不得恢复进 B 的库。
    void backupCannotBeRestoredIntoAnotherUser()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString backupPath = dir.filePath("backup.optbk");
        const QString password = QStringLiteral("shared password");

        {
            UserDatabase db(dir.filePath("users"));
            QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
            QVERIFY(db.addHistorySample(QDateTime::currentMSecsSinceEpoch() - 60000, 25.0));
            QCOMPARE(Backup::create(db, backupPath, password), Backup::Result::Ok);
        }

        UserDatabase other(dir.filePath("other"));
        QVERIFY(other.openForSession(QStringLiteral("userB"), keyOf('b')));
        QCOMPARE(Backup::restore(&other, backupPath, password, QStringLiteral("userB")),
                 Backup::Result::WrongUser);
        QVERIFY(other.historySeries(QDateTime::currentMSecsSinceEpoch(), 7 * kDay, 60000).isEmpty());
    }

    // 损坏文件与截断文件必须拒绝，不能部分恢复。
    void corruptFileIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString bad = dir.filePath("bad.optbk");
        QFile file(bad);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("OPTBK001\x01\x00 not really a backup");
        file.close();

        UserDatabase db(dir.filePath("users"));
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
        const auto result = Backup::restore(&db, bad, QStringLiteral("password long enough"),
                                            QStringLiteral("userA"));
        QVERIFY(result == Backup::Result::BadPassword || result == Backup::Result::CorruptFile);
    }

    // 参数校验：短密码 / 空路径一律拒绝。
    void invalidArgumentsAreRejected()
    {
        QTemporaryDir dir;
        UserDatabase db(dir.filePath("users"));
        QVERIFY(db.openForSession(QStringLiteral("userA"), keyOf('a')));
        QCOMPARE(Backup::create(db, dir.filePath("x.optbk"), QStringLiteral("short")),
                 Backup::Result::InvalidArgument);
        QCOMPARE(Backup::create(db, QString(), QStringLiteral("long enough password")),
                 Backup::Result::InvalidArgument);
        QCOMPARE(Backup::restore(&db, dir.filePath("missing.optbk"),
                                 QStringLiteral("long enough password"), QStringLiteral("userA")),
                 Backup::Result::StorageUnavailable);
    }
};

QTEST_GUILESS_MAIN(BackupTest)
#include "backup_test.moc"
