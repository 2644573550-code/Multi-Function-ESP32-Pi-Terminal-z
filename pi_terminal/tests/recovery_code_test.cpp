// 报告 §6.2 / §7.2：离线恢复码是丢 PIN 后唯一的自救路径。
#include "security/KeyWrap.h"
#include "security/PinPolicy.h"
#include "security/RecoveryCode.h"
#include "security/UserService.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace security;

class RecoveryCodeTest : public QObject {
    Q_OBJECT

private slots:
    // 生成的恢复码必须符合格式，且每次不同。
    void generatedCodeIsValidAndUnique()
    {
        const QString first = RecoveryCode::generate();
        const QString second = RecoveryCode::generate();
        QVERIFY(RecoveryCode::isValidFormat(first));
        QVERIFY(RecoveryCode::isValidFormat(second));
        QVERIFY(first != second);
        // 分组展示：6 组 4 字符，用 - 连接。
        QCOMPARE(first.count(QLatin1Char('-')), 5);
    }

    // 规范化：空格/小写/缺连字符都要能接受。
    void formatToleratesCommonTypos()
    {
        const QString generated = RecoveryCode::generate();
        const QString noDashes = QString(generated).remove(QLatin1Char('-'));
        const QString lower = generated.toLower();
        QVERIFY(RecoveryCode::isValidFormat(noDashes));
        QVERIFY(RecoveryCode::isValidFormat(lower));
        QVERIFY(RecoveryCode::isValidFormat(QStringLiteral(" ") + generated + QStringLiteral(" ")));
        // 长度不对、含非法字符必须拒绝。
        QVERIFY(!RecoveryCode::isValidFormat(QString()));
        QVERIFY(!RecoveryCode::isValidFormat(QString(23, QLatin1Char('A'))));
        QVERIFY(!RecoveryCode::isValidFormat(QStringLiteral("AAAAAAAA-AAAAAAAA-AAAAAAAA-OOOO")));
    }

    // 核心：用恢复码解出同一个 dbKey，且能打开同一个库。
    void recoveryCodeUnlocksTheSameDatabase()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserService users(dir.filePath("users.json"));
        const QString code = RecoveryCode::generate();
        const QString userId = users.createInitialAdmin(QStringLiteral("T"),
                                                        QStringLiteral("246810"), code);
        QVERIFY(!userId.isEmpty());
        QVERIFY(users.hasRecoveryCode(userId));

        UserRecord record;
        QVERIFY(users.lookup(userId, &record));
        // PIN 路径与恢复码路径必须解出同一个 DB key。
        const QByteArray kek = PinPolicy::deriveKek(
            QStringLiteral("246810"), record.salt,
            PinPolicy::effectiveIterations(record.iterations));
        const QByteArray byPin = KeyWrap::open(kek, record.wrappedDbKey, record.dbKeyNonce);
        QByteArray byCode;
        QVERIFY(users.openWithRecoveryCode(userId, code, &byCode));
        QCOMPARE(byCode, byPin);
        QCOMPARE(byCode.size(), 32);
    }

    // 错误的恢复码必须失败，不能解出任何 key。
    void wrongRecoveryCodeFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserService users(dir.filePath("users.json"));
        const QString userId = users.createInitialAdmin(
            QStringLiteral("T"), QStringLiteral("246810"), RecoveryCode::generate());
        QVERIFY(!userId.isEmpty());

        QByteArray dbKey;
        QVERIFY(!users.openWithRecoveryCode(userId, RecoveryCode::generate(), &dbKey));
        QVERIFY(dbKey.isEmpty());
        // 格式非法也拒绝。
        QVERIFY(!users.openWithRecoveryCode(userId, QStringLiteral("short"), &dbKey));
    }

    // 没有恢复码的账户必须明确失败（rather than 假装成功）。
    void accountWithoutRecoveryCodeReportsFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserService users(dir.filePath("users.json"));
        const QString userId = users.createInitialAdmin(QStringLiteral("T"),
                                                        QStringLiteral("246810"));
        QVERIFY(!userId.isEmpty());
        QVERIFY(!users.hasRecoveryCode(userId));

        QByteArray dbKey;
        QVERIFY(!users.openWithRecoveryCode(userId, RecoveryCode::generate(), &dbKey));
    }

    // 恢复码持久化：重开注册表后仍然可用。
    void recoveryCodeSurvivesReload()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString registry = dir.filePath("users.json");
        const QString code = RecoveryCode::generate();
        QString userId;
        {
            UserService users(registry);
            userId = users.createInitialAdmin(QStringLiteral("T"), QStringLiteral("246810"), code);
            QVERIFY(!userId.isEmpty());
        }
        // 新实例从磁盘读回。
        UserService reloaded(registry);
        QVERIFY(reloaded.hasRecoveryCode(userId));
        QByteArray dbKey;
        QVERIFY(reloaded.openWithRecoveryCode(userId, code, &dbKey));
        QCOMPARE(dbKey.size(), 32);
    }

    // 补发恢复码：为已有账户新增恢复路径。
    void setRecoveryCodeAddsPathToExistingUser()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserService users(dir.filePath("users.json"));
        const QString userId = users.createInitialAdmin(QStringLiteral("T"),
                                                        QStringLiteral("246810"));
        QVERIFY(!users.hasRecoveryCode(userId));

        UserRecord record;
        QVERIFY(users.lookup(userId, &record));
        const QByteArray kek = PinPolicy::deriveKek(
            QStringLiteral("246810"), record.salt,
            PinPolicy::effectiveIterations(record.iterations));
        const QByteArray dbKey = KeyWrap::open(kek, record.wrappedDbKey, record.dbKeyNonce);
        QVERIFY(dbKey.size() == 32);

        const QString code = RecoveryCode::generate();
        QVERIFY(users.setRecoveryCode(userId, dbKey, code));
        QVERIFY(users.hasRecoveryCode(userId));

        QByteArray recovered;
        QVERIFY(users.openWithRecoveryCode(userId, code, &recovered));
        QCOMPARE(recovered, dbKey);
    }

    // 恢复码不得以明文形式出现在注册表里。
    void recoveryCodeIsNeverStoredInPlaintext()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString registry = dir.filePath("users.json");
        UserService users(registry);
        const QString code = RecoveryCode::generate();
        QVERIFY(!users.createInitialAdmin(QStringLiteral("T"), QStringLiteral("246810"), code).isEmpty());

        QFile file(registry);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray content = file.readAll();
        file.close();
        QVERIFY2(!content.contains(code.toUtf8()), "recovery code must never be stored");
        QVERIFY2(!content.contains(RecoveryCode::normalize(code).toUtf8()),
                 "normalized recovery code must never be stored");
    }
};

QTEST_GUILESS_MAIN(RecoveryCodeTest)
#include "recovery_code_test.moc"
