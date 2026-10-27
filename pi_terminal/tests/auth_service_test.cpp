// UT-002 / T-04：PIN 校验、限速、锁定、会话切换与销毁。
// 迭代数在测试里降到 1000 提速，并断言生产常量 ≥ 600000。
#include "security/AuthService.h"
#include "security/KeyWrap.h"
#include "security/PinPolicy.h"
#include "security/SessionManager.h"
#include "security/UserService.h"

#include <QDir>
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
};

QTEST_GUILESS_MAIN(AuthTest)
#include "auth_service_test.moc"
