// 登入门禁端到端：无用户 / 有用户未登录 / 登录成功 / 锁屏 / 解锁 / 退出。
// 走真实 SessionManager + UserService + AuthService，不 mock 认证。
#include "security/AuthService.h"
#include "security/SessionManager.h"
#include "security/UserService.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace security;

class SessionGateTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;
    QString registry_() const { return dir_.filePath("users.json"); }
    QString locks_() const { return dir_.filePath("locks.json"); }

private slots:
    void init() {
        QFile::remove(registry_());
        QFile::remove(locks_());
    }

    void noUsersMeansCannotLogin() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        SessionManager session(&users, &auth);

        // 空注册表：hasUsers 必须为 false，QML 据此显示"请先创建管理员"。
        QVERIFY(!session.hasUsers());
        QVERIFY(session.users().isEmpty());
        QVERIFY(!session.loggedIn());
        // 没有用户时任何登录尝试都失败，且不泄漏原因。
        QCOMPARE(session.login(QStringLiteral("nobody"), QStringLiteral("246810")),
                 QString(AuthResult::AuthFailed));
    }

    void loginGateBlocksUntilAuthenticated() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        SessionManager session(&users, &auth);
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        session.refreshUsers();
        QVERIFY(session.hasUsers());
        // 未登录：QML 的 needLogin 依赖这两个状态。
        QVERIFY(!session.loggedIn());
        QVERIFY(!session.locked());

        // 错误 PIN：仍然未登录。
        QCOMPARE(session.login(id, QStringLiteral("000000")), QString(AuthResult::AuthFailed));
        QVERIFY(!session.loggedIn());
        QVERIFY(!session.message().isEmpty());   // QML 用它显示错误

        // 正确 PIN：建立会话，门禁放开。
        QTest::qWait(1100);   // 终端级 1 次/秒
        QCOMPARE(session.login(id, QStringLiteral("246810")), QString());
        QVERIFY(session.loggedIn());
        QCOMPARE(session.activeUserId(), id);
        QVERIFY(session.message().isEmpty());
    }

    void lockRequiresPinNotCard() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        SessionManager session(&users, &auth);
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        session.refreshUsers();
        QCOMPARE(session.login(id, QStringLiteral("246810")), QString());

        QSignalSpy lockSpy(&session, &SessionManager::lockStateChanged);
        session.lockNow();
        QVERIFY(session.locked());
        QVERIFY(session.loggedIn());       // 会话还在，只是锁住
        QVERIFY(session.dbKey().isEmpty()); // 前台拿不到 DB key
        QCOMPARE(lockSpy.count(), 1);

        // 解锁必须输 PIN，且错误 PIN 不解锁。
        QTest::qWait(1100);
        QCOMPARE(session.unlockWithPin(QStringLiteral("000000")), QString(AuthResult::AuthFailed));
        QVERIFY(session.locked());

        QTest::qWait(1100);
        QCOMPARE(session.unlockWithPin(QStringLiteral("246810")), QString());
        QVERIFY(!session.locked());
        QVERIFY(!session.dbKey().isEmpty());
    }

    void signOutReturnsToLogin() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        SessionManager session(&users, &auth);
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        session.refreshUsers();
        QCOMPARE(session.login(id, QStringLiteral("246810")), QString());

        QSignalSpy invalidated(&session, &SessionManager::sessionInvalidated);
        session.signOut();
        QVERIFY(!session.loggedIn());
        QVERIFY(!session.locked());
        QVERIFY(session.activeUserId().isEmpty());
        QCOMPARE(invalidated.count(), 1);
        // 退出后用户列表还在（可以重新登录），但会话没了。
        QVERIFY(session.hasUsers());
        QVERIFY(!session.users().isEmpty());
    }

    void loginRefusedWhileTasksUnfinished() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        SessionManager session(&users, &auth);
        const QString id = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        session.refreshUsers();
        QCOMPARE(session.login(id, QStringLiteral("246810")), QString());

        const QString task = session.startTask(QStringLiteral("planter-1"));
        QVERIFY(!task.isEmpty());
        // 有未完成任务时不能切用户/重新登录（f-day1 §2）。
        QTest::qWait(1100);
        QCOMPARE(session.login(id, QStringLiteral("246810")), QString("BUSY"));
        QVERIFY(session.finishTask(task));
    }

    void userListIsLowSensitivity() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        SessionManager session(&users, &auth);
        users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        session.refreshUsers();

        QCOMPARE(session.users().size(), 1);
        const QVariantMap user = session.users().first().toMap();
        // 只允许显示名与不透明 ID；不能泄漏 salt/verifier/DB key。
        QVERIFY(user.contains("displayName"));
        QVERIFY(user.contains("userId"));
        QVERIFY(!user.contains("salt"));
        QVERIFY(!user.contains("verifier"));
        QVERIFY(!user.contains("wrappedDbKey"));
        QVERIFY(!user.contains("pin"));
    }

    void disabledUserIsHiddenFromList() {
        UserService users(registry_());
        AuthService auth(users, locks_());
        SessionManager session(&users, &auth);
        const QString admin = users.createInitialAdmin(QStringLiteral("Z"), QStringLiteral("246810"));
        const QString member = users.createUser(admin, QStringLiteral("M"), QStringLiteral("111222"));
        QVERIFY(!member.isEmpty());
        session.refreshUsers();
        QCOMPARE(session.users().size(), 2);

        QVERIFY(users.disableUser(admin, member));
        session.refreshUsers();
        // 停用用户不出现在登录选择列表里。
        QCOMPARE(session.users().size(), 1);
        QCOMPARE(session.users().first().toMap().value("displayName").toString(), QStringLiteral("Z"));
    }
};

QTEST_GUILESS_MAIN(SessionGateTest)
#include "session_gate_test.moc"

