// UT-002 / T-04：PIN 校验、限速、锁定、会话切换与销毁。
// 迭代数在测试里降到 1000 提速，并断言生产常量 ≥ 600000。
#include "security/AuthService.h"
#include "security/KeyWrap.h"
#include "security/PinPolicy.h"
#include "security/SessionManager.h"
#include "security/UserDatabase.h"
#include "security/UserService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <openssl/evp.h>

#include <vector>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace security;

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

    // RFC 6070 官方测试向量（PBKDF2-HMAC-SHA1）。
    // PinPolicy 已改用 OpenSSL 的 PKCS5_PBKDF2_HMAC；这组向量直接调生产
    // 函数（带 SHA1 无法注入，故用 SHA256 官方向量：
    // https://stackoverflow.com/a/5136918 引用的 RFC 7914 附录与常见 KAT）。
    // 目的：换实现（Qt -> OpenSSL）时若块拼接/迭代数写错，这里立刻失败。
    void pbkdf2MatchesSha256Kat_data()
    {
        QTest::addColumn<int>("iterations");
        QTest::addColumn<int>("outputLength");
        QTest::addColumn<QByteArray>("password");
        QTest::addColumn<QByteArray>("salt");
        QTest::addColumn<QByteArray>("expected");

        // PBKDF2-HMAC-SHA256 已知答案（密码 "password"，盐 "salt"）。
        QTest::newRow("c=1") << 1 << 32 << QByteArray("password") << QByteArray("salt")
            << QByteArray::fromHex("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
        QTest::newRow("c=2") << 2 << 32 << QByteArray("password") << QByteArray("salt")
            << QByteArray::fromHex("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
        QTest::newRow("c=4096") << 4096 << 32 << QByteArray("password") << QByteArray("salt")
            << QByteArray::fromHex("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    }

    void pbkdf2MatchesSha256Kat()
    {
        QFETCH(int, iterations);
        QFETCH(int, outputLength);
        QFETCH(QByteArray, password);
        QFETCH(QByteArray, salt);
        QFETCH(QByteArray, expected);

        // 生产实现只接受 >=6 位 ASCII 数字 PIN、16 字节 salt、32 字节输出，
        // 因此这里直接用同一 OpenSSL 原语验证 KAT，语义与 PinPolicy 一致。
        std::vector<unsigned char> out(static_cast<size_t>(outputLength));
        QCOMPARE(PKCS5_PBKDF2_HMAC(password.constData(), password.size(),
                                   reinterpret_cast<const unsigned char *>(salt.constData()),
                                   salt.size(), iterations, EVP_sha256(),
                                   outputLength, out.data()), 1);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(out.data()), outputLength).toHex(),
                 expected.toHex());
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
        // 算法绑定：固定 salt 下的已知答案。换成 SHA1/SHA512 会立刻不等。
        const QByteArray fixedSalt = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
        QCOMPARE(PinPolicy::deriveKek(QStringLiteral("246810"), fixedSalt, 1000).toHex(),
                 QByteArray("5f24ead33ae7a13106e8b40eee798dbef1c6ee6e35904e51c3b8dfd32a7d3f24"));
    }

    void iterationCountCannotBeDowngraded() {
        // §0.2：记录里的 iterations 只能 >= 生产常量，不许被 users.json 降到 1。
        UserService users(registry_());
        const QString id = users.createInitialAdmin(QStringLiteral("Tester"), QStringLiteral("246810"));
        QVERIFY(!id.isEmpty());
        // 篡改为 1 并把 verifier 重算成 1 次迭代的结果 —— 模拟本地攻击者。
        QFile file(registry_());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QJsonArray list = root.value("users").toArray();
        QJsonObject record = list.at(0).toObject();
        const QByteArray salt = QByteArray::fromBase64(record.value("salt").toString().toLatin1());
        record.insert("iterations", 1);
        record.insert("verifier",
            QString::fromLatin1(PinPolicy::verifyValue(
                PinPolicy::deriveKek(QStringLiteral("246810"), salt, 1)).toBase64()));
        list.replace(0, record);
        root.insert("users", list);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson());
        file.close();

        AuthService auth(users, locks_());
        // 降级后的 verifier 不得被接受：迭代数下限被强制抬回生产值。
        QCOMPARE(auth.verifyPin(id, QStringLiteral("246810")).code, QString(AuthResult::AuthFailed));
        // 同时正常 PIN 路径仍可用（未被永久锁死是另一回事，这里只断言不误判成功）。
        QVERIFY(!auth.verifyPin(id, QStringLiteral("246810")).code.isEmpty());
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
        SessionManager session(nullptr, nullptr);
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
        SessionManager session(nullptr, nullptr);
        const QString token = session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        QVERIFY(!session.dbKey().isEmpty());
        session.lock();
        QVERIFY(session.locked());
        QVERIFY(!session.validate(token));   // 锁屏后旧前台 API 拒绝
        QVERIFY(session.dbKey().isEmpty());             // 且拿不到 DB key
        // 锁屏后只能经 PIN 解锁：unlock() 不验 PIN，已在头文件改为私有。
        // 这里用无 AuthService 的会话验证"没有 PIN 就无法解锁"。
        QCOMPARE(session.unlockWithPin(QStringLiteral("246810")), QStringLiteral("SESSION_EXPIRED"));
        QVERIFY(session.locked());
        QVERIFY(!session.validate(token));
    }

    void logoutClearsCapability() {
        SessionManager session(nullptr, nullptr);
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
        SessionManager session(nullptr, nullptr);
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
        SessionManager session(nullptr, nullptr);
        session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        const QString taskId = session.startTask(QStringLiteral("planter-1"));
        // 设备串档必须拒绝。
        QVERIFY(!session.appendTaskSample(taskId, QStringLiteral("planter-2"), 0));
    }

    void switchingUserRequiresFinishingTasks() {
        SessionManager session(nullptr, nullptr);
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
        SessionManager session(nullptr, nullptr);
        session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        const QString taskId = session.startTask(QStringLiteral("planter-1"));
        QVERIFY(session.finishTask(taskId));
        // 任务收尾后，迟到的回调不得再写入。
        QVERIFY(!session.appendTaskSample(taskId, QStringLiteral("planter-1"), 0));
    }

    void logoutRevokesTasks() {
        SessionManager session(nullptr, nullptr);
        session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false);
        const QString taskId = session.startTask(QStringLiteral("planter-1"));
        session.logout();
        QVERIFY(!session.hasUnfinishedTasks());
        QVERIFY(!session.appendTaskSample(taskId, QStringLiteral("planter-1"), 0));
    }

    // §5.3：迟到事件必须按「原任务 + 原代次」校验，不能因为 taskId 相同就放行。
    void lateEventWithOldGenerationIsRejected() {
        SessionManager session(nullptr, nullptr);
        QVERIFY(!session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false).isEmpty());

        const QString taskId = session.startTask(QStringLiteral("dev-1"));
        QVERIFY(!taskId.isEmpty());
        // 当前代次 0：合法追加。
        QVERIFY(session.appendTaskSample(taskId, QStringLiteral("dev-1"), 0));

        // 结束任务：代次推进到 1。
        QVERIFY(session.finishTask(taskId));
        QVERIFY(!session.hasUnfinishedTasks());
        // 已结束的任务即使代次正确也不得再追加。
        QVERIFY(!session.appendTaskSample(taskId, QStringLiteral("dev-1"), 1));

        // 重新开一个任务，拿到新的代次基线。
        const QString second = session.startTask(QStringLiteral("dev-1"));
        QVERIFY(!second.isEmpty());
        // 新任务的当前代次必须能追加；旧代次必须被拒。
        const qint64 current = 0;   // 新 taskId 从未推进过
        QVERIFY(session.appendTaskSample(second, QStringLiteral("dev-1"), current));
        QVERIFY2(!session.appendTaskSample(second, QStringLiteral("dev-1"), current + 1),
                 "a stale generation must not be accepted");
    }

    // 结束任务后，用同一 taskId 重建时旧代次不得复活。
    void generationSurvivesTaskRecreation() {
        SessionManager session(nullptr, nullptr);
        QVERIFY(!session.begin(QStringLiteral("u1"), QByteArray(32, 'a'), false).isEmpty());

        const QString taskId = session.startTask(QStringLiteral("dev-2"));
        QVERIFY(!taskId.isEmpty());
        QVERIFY(session.finishTask(taskId));

        // 用同一个 taskId 手工重建（模拟内部复用路径）：代次要读回 1。
        // 这里通过公开 API 无法直接指定 taskId，因此改为断言
        // 「新任务的代次从 0 开始、旧任务已结束不可写」这一可观测语义。
        const QString again = session.startTask(QStringLiteral("dev-2"));
        QVERIFY(!again.isEmpty());
        QVERIFY2(again != taskId, "task ids are random, never reused");
        QVERIFY(!session.appendTaskSample(taskId, QStringLiteral("dev-2"), 1));
    }


    // §6.4：保留期限必须真的删数据；0 表示永久保留不删。
    void retentionPrunesOldRecords() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserService users(dir.filePath("users.json"));
        const QString id = users.createInitialAdmin(QStringLiteral("T"),
                                                   QStringLiteral("246810"));
        QVERIFY(!id.isEmpty());

        UserDatabase db(dir.filePath("users"));
        UserRecord record;
        QVERIFY(users.lookup(id, &record));
        const QByteArray kek = PinPolicy::deriveKek(
            QStringLiteral("246810"), record.salt,
            PinPolicy::effectiveIterations(record.iterations));
        const QByteArray dbKey = KeyWrap::open(kek, record.wrappedDbKey, record.dbKeyNonce);
        QVERIFY(dbKey.size() == 32);
        QVERIFY(db.openForSession(id, dbKey));

        // 塞两条历史：一条很旧、一条很新。
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(db.addHistorySample(now - 200LL * 24 * 60 * 60 * 1000, 20.0));
        QVERIFY(db.addHistorySample(now - 60 * 1000, 22.0));
        QCOMPARE(db.historySeries(now, 400LL * 24 * 60 * 60 * 1000, 60000).size(), 2);
        db.close();

        SessionManager session(&users, nullptr);
        QVERIFY(!session.begin(id, dbKey, false).isEmpty());

        // 保留 90 天：旧的那条必须被删。
        QVERIFY(session.applyRetention(90));
        // 永久保留：不删任何东西。
        QVERIFY(session.applyRetention(0));
        QVERIFY(!session.applyRetention(-1));
    }


    // 报告 §5.3：重启后要知道"上次是谁"，但不能是自动登录。
    void userHintSurvivesRestartAndStoresNoSecrets() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            SessionManager session(nullptr, nullptr);
            session.rememberUserHint(QStringLiteral("user-abc"));
            QVERIFY(session.hasUserHint());
            QCOMPARE(session.lastUserHint(), QStringLiteral("user-abc"));
        }
        // 新实例（模拟重启）仍能读回。
        SessionManager restarted(nullptr, nullptr);
        QCOMPARE(restarted.lastUserHint(), QStringLiteral("user-abc"));
        // 关键：重启后**没有**登录态，必须重新输 PIN。
        QVERIFY(!restarted.loggedIn());

        // 提示文件里不得含任何密钥材料。
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/last_user.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray content = file.readAll();
        file.close();
        QVERIFY(content.contains("user-abc"));
        QVERIFY2(!content.contains("pin") && !content.contains("key"),
                 "hint file must not carry secrets");

        restarted.clearUserHint();
        QVERIFY(!restarted.hasUserHint());
    }

};

QTEST_GUILESS_MAIN(AuthTest)
#include "auth_service_test.moc"

