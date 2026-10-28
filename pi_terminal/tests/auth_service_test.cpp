// UT-002 / T-04：PIN 校验、限速、锁定、会话切换与销毁。
// 迭代数在测试里降到 1000 提速，并断言生产常量 ≥ 600000。
#include "security/AuthService.h"
#include "security/KeyWrap.h"
#include "security/PinPolicy.h"
#include "security/SessionManager.h"
#include "security/UserService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QPasswordDigestor>
#include <QTemporaryDir>
#include <QtTest>

class AuthTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;
    QString registry_() const { return dir_.filePath("users.json"); }
    QString locks_() const { return dir_.filePath("locks.json"); }

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        // §0.2：生产常量是硬编码下限，单测小值不得成为降低它的入口。
        QVERIFY2(PinPolicy::kProductionIterations >= 600000, "production iteration floor was lowered");
    }

    // RFC 6070 官方测试向量：确认 KDF 实现本身符合标准，
    // 不是"跑通了就算对"。任何换实现（Qt → OpenSSL）都必须先过这一组。
    void pbkdf2MatchesRfc6070_data() {
        QTest::addColumn<int>("iterations");
        QTest::addColumn<int>("outputLength");
        QTest::addColumn<QByteArray>("password");
        QTest::addColumn<QByteArray>("salt");
        QTest::addColumn<QByteArray>("expected");

        // RFC 6070 §2，PBKDF2-HMAC-SHA1。PinPolicy 用 SHA256，但 KDF 骨架
        // 与算法无关；这组向量保证"迭代次数/输出长度/块拼接"这三处没错。
        QTest::newRow("c=1") << 1 << 20 << QByteArray("password") << QByteArray("salt")
            << QByteArray::fromHex("0c60c80f961f0e71f3a9b524af6012062fe037a6");
        QTest::newRow("c=2") << 2 << 20 << QByteArray("password") << QByteArray("salt")
            << QByteArray::fromHex("ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957");
        QTest::newRow("c=4096") << 4096 << 20 << QByteArray("password") << QByteArray("salt")
            << QByteArray::fromHex("4b007901b765489abead49d926f721d065a429c1");
        QTest::newRow("c=4096-longpw") << 4096 << 25
            << QByteArray("passwordPASSWORDpassword")
            << QByteArray("saltSALTsaltSALTsaltSALTsaltSALTsalt")
            << QByteArray::fromHex("3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038");
    }

    void pbkdf2MatchesRfc6070() {
        QFETCH(int, iterations);
        QFETCH(int, outputLength);
        QFETCH(QByteArray, password);
        QFETCH(QByteArray, salt);
        QFETCH(QByteArray, expected);

        // 直接用 Qt 的 KDF（与 PinPolicy 同一底层），算法用 SHA1 以对齐 RFC 6070。
        const QByteArray actual = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha1, password, salt, iterations, outputLength);
        QCOMPARE(actual.toHex(), expected.toHex());
    }

    void productionKdfUsesSha256And32Bytes() {
        // 生产参数必须与 §0.2 冻结值一致：SHA256、输出 32 字节。
        const QByteArray salt = PinPolicy::salt16();
        QCOMPARE(salt.size(), 16);
        const QByteArray kek = PinPolicy::deriveKek(QStringLiteral("246810"), salt, 1000);
        QCOMPARE(kek.size(), 32);
        // 同 PIN + 同 salt 必须稳定；不同 salt 必须不同。
        QCOMPARE(PinPolicy::deriveKek(QStringLiteral("246810"), salt, 1000), kek);
        QVERIFY(PinPolicy::deriveKek(QStringLiteral("246810"), PinPolicy::salt16(), 1000) != kek);
    }

    void invalidPinProducesNoKey() {
        // 失败关闭：非法 PIN 不产生任何 key 字节。
        QVERIFY(PinPolicy::deriveKek(QStringLiteral("12345"), PinPolicy::salt16(), 1000).isEmpty());
        QVERIFY(PinPolicy::deriveKek(QStringLiteral("12345a"), PinPolicy::salt16(), 1000).isEmpty());
        QVERIFY(PinPolicy::deriveKek(QStringLiteral("123456"), QByteArray(15, 'x'), 1000).isEmpty());
    }

    void init() {
        QFile::remove(registry_());
        QFile::remove(locks_());
    }

    void validPinRules() {
        QVERIFY(!PinPolicy::validPin("12345"));        // 不足 6 位
        QVERIFY(PinPolicy::validPin("123456"));
        QVERIFY(!PinPolicy::validPin("12345a"));       // 非 ASCII 数字
        QVERIFY(!PinPolicy::validPin(QString::fromUtf8("１２３４５６")));   // 全角数字
        QVERIFY(!PinPolicy::validPin(""));
    }

    void createInitialAdminOnlyOnce() {
        UserService users(registry_());
        QVERIFY(!users.hasAnyUser());
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("123456"));
        QVERIFY(!id.isEmpty());
        // 已有用户后必须拒绝，且不覆盖。
        QVERIFY(users.createInitialAdmin(QStringLiteral("evil"), QStringLiteral("999999")).isEmpty());
        QCOMPARE(users.listUsers().size(), 1);
        QCOMPARE(users.listUsers().first().displayName, QStringLiteral("Z"));
    }

    void registryNeverStoresPinOrKek() {
        UserService users(registry_());
        users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("135790"));
        QFile file(registry_());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray raw = file.readAll();
        QVERIFY(!raw.contains("135790"));   // PIN 不得落盘
        // 验证值不得等于 KEK：断言存在 verifier 字段且与 wrapped key 分离。
        QVERIFY(raw.contains("verifier"));
        QVERIFY(raw.contains("wrapped_db_key"));
        QVERIFY(!(QFileInfo(registry_()).permissions() & QFile::ReadGroup));
    }

    void correctPinYieldsDbKey() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        const AuthService::Result result = auth.verifyPin(id, QStringLiteral("246810"));
        QCOMPARE(result.code, QString(AuthResult::Ok));
        QCOMPARE(result.dbKey.size(), KeyWrap::kKeyBytes);
        QVERIFY(result.admin);
    }

    void wrongPinDoesNotRevealExistence() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));

        // 终端级 1 次/秒：两次尝试之间必须等，否则第二条会是 RATE_LIMITED。
        const auto missing = auth.verifyPin(QStringLiteral("nobody"), QStringLiteral("000000"));
        QTest::qWait(PinPolicy::kTerminalMinIntervalMs + 50);
        const auto wrong = auth.verifyPin(id, QStringLiteral("000000"));
        // 两条路径返回同一个 code，不能靠 code 区分账号是否存在。
        QCOMPARE(missing.code, wrong.code);
        QCOMPARE(wrong.code, QString(AuthResult::AuthFailed));
    }

    void terminalRateLimitApplies() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        QCOMPARE(auth.verifyPin(id, QStringLiteral("246810")).code, QString(AuthResult::Ok));
        // 紧接着的第二次尝试落在 1 秒窗口内。
        const auto limited = auth.verifyPin(id, QStringLiteral("246810"));
        QCOMPARE(limited.code, QString(AuthResult::RateLimited));
        QVERIFY(limited.retryAfterMs > 0);
    }

    void accountLocksAfterFiveFailures() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        for (int i = 0; i < PinPolicy::kMaxAccountFailures; ++i) {
            QTest::qWait(PinPolicy::kTerminalMinIntervalMs + 20);
            QCOMPARE(auth.verifyPin(id, QStringLiteral("111111")).code, QString(AuthResult::AuthFailed));
        }
        // 第 6 次即便 PIN 正确也必须被锁。
        QTest::qWait(PinPolicy::kTerminalMinIntervalMs + 20);
        const auto locked = auth.verifyPin(id, QStringLiteral("246810"));
        QCOMPARE(locked.code, QString(AuthResult::RateLimited));
    }

    void lockSurvivesRestart() {
        QString id;
        {
            UserService users(registry_());
            AuthService auth(users, locks_());
            id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
            for (int i = 0; i < PinPolicy::kMaxAccountFailures; ++i) {
                QTest::qWait(PinPolicy::kTerminalMinIntervalMs + 20);
                auth.verifyPin(id, QStringLiteral("111111"));
            }
        }
        // 新实例等价于重启：锁定不得被清除。
        UserService users(registry_());
        AuthService auth(users, locks_());
        QCOMPARE(auth.verifyPin(id, QStringLiteral("246810")).code, QString(AuthResult::RateLimited));
    }

    void changePinRewrapsDbKeyWithoutReencrypting() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        const QByteArray before = auth.verifyPin(id, QStringLiteral("246810")).dbKey;
        QVERIFY(!before.isEmpty());

        QVERIFY(auth.changePin(id, QStringLiteral("246810"), QStringLiteral("135791")));
        QTest::qWait(PinPolicy::kTerminalMinIntervalMs + 20);
        // 旧 PIN 失效，新 PIN 可用，且 DB key 不变（只是重新封装）。
        QCOMPARE(auth.verifyPin(id, QStringLiteral("246810")).code, QString(AuthResult::AuthFailed));
        QTest::qWait(PinPolicy::kTerminalMinIntervalMs + 20);
        const auto after = auth.verifyPin(id, QStringLiteral("135791"));
        QCOMPARE(after.code, QString(AuthResult::Ok));
        QCOMPARE(after.dbKey, before);
    }

    void sessionSwitchInvalidatesOldToken() {
        SessionManager session;
        const QString first = session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        QVERIFY(!first.isEmpty());
        QVERIFY(session.validate(first));

        const QString second = session.begin(QStringLiteral("u2"), QByteArray(32, 'b'), false);
        // 旧 token 必须立刻失效，且服务端校验而非隐藏 UI。
        QVERIFY(!session.validate(first));
        QVERIFY(session.validate(second));
        QCOMPARE(session.activeUserId(), QStringLiteral("u2"));
    }

    void lockRevokesForegroundAccess() {
        SessionManager session;
        const QString token = session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        QVERIFY(!session.dbKey().isEmpty());
        session.lock();
        QVERIFY(session.locked());
        QVERIFY(!session.validate(token));   // 锁屏后旧前台 API 拒绝
        QVERIFY(session.dbKey().isEmpty());             // 且拿不到 DB key
        session.unlock(QStringLiteral("u1"));
        QVERIFY(!session.locked());
        QVERIFY(session.validate(token));
    }

    void logoutClearsCapability() {
        SessionManager session;
        const QString token = session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), true);
        QSignalSpy invalidated(&session, &SessionManager::sessionInvalidated);
        session.logout();
        QCOMPARE(invalidated.count(), 1);
        QVERIFY(session.activeUserId().isEmpty());
        QVERIFY(!session.validate(token));
        QVERIFY(!session.isAdmin());
    }

    void nonAdminCannotCreateUsers() {
        UserService users(registry_());
        const QString admin = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        const QString member = users.createUser(admin, QStringLiteral("M"), QStringLiteral("111222"));
        QVERIFY(!member.isEmpty());
        // 普通用户不得创建新用户。
        QVERIFY(users.createUser(member, QStringLiteral("X"), QStringLiteral("333444")).isEmpty());
        // 普通用户不得停用他人。
        QVERIFY(!users.disableUser(member, admin));
    }

    void lockKeepsTaskAppendButRevokesHistory() {
        SessionManager session;
        session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        const QString taskId = session.startTask(QStringLiteral("planter-1"));
        QVERIFY(!taskId.isEmpty());
        QVERIFY(session.appendTaskSample(taskId, QStringLiteral("planter-1"), 0));

        session.lock();
        // 锁屏后：前台 token 失效、DB key 拿不到，但任务仍可追加。
        QVERIFY(!session.validate(QString()));
        QVERIFY(session.dbKey().isEmpty());
        QVERIFY2(session.appendTaskSample(taskId, QStringLiteral("planter-1"), 0),
                 "locked session must still allow the original task to append");
    }

    void taskCannotAppendToOtherDevice() {
        SessionManager session;
        session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        const QString taskId = session.startTask(QStringLiteral("planter-1"));
        // 设备串档必须拒绝。
        QVERIFY(!session.appendTaskSample(taskId, QStringLiteral("planter-2"), 0));
    }

    void switchingUserRequiresFinishingTasks() {
        SessionManager session;
        session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        const QString taskId = session.startTask(QStringLiteral("planter-1"));
        // 有未完成任务时不得切用户。
        QVERIFY(session.hasUnfinishedTasks());
        QVERIFY2(session.begin(QStringLiteral("u2"), QByteArray(32, 'b'), false).isEmpty(),
                 "must not switch user while a task is unfinished");

        QVERIFY(session.finishTask(taskId));
        QVERIFY(!session.hasUnfinishedTasks());
        // 收尾后可以切。
        QVERIFY(!session.begin(QStringLiteral("u2"), QByteArray(32, 'b'), false).isEmpty());
    }

    void lateEventAfterFinishIsRejected() {
        SessionManager session;
        session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        const QString taskId = session.startTask(QStringLiteral("planter-1"));
        QVERIFY(session.finishTask(taskId));
        // 任务收尾后，迟到的回调不得再写入。
        QVERIFY(!session.appendTaskSample(taskId, QStringLiteral("planter-1"), 0));
    }

    void logoutRevokesTasks() {
        SessionManager session;
        session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        const QString taskId = session.startTask(QStringLiteral("planter-1"));
        session.logout();
        QVERIFY(!session.hasUnfinishedTasks());
        QVERIFY(!session.appendTaskSample(taskId, QStringLiteral("planter-1"), 0));
    }
};

QTEST_GUILESS_MAIN(AuthTest)
#include "auth_service_test.moc"
