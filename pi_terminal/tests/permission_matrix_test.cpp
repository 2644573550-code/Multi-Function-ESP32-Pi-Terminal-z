// 报告 §8.2：健康历史与任意互联网访问的强制互斥。
#include "security/PermissionMatrix.h"

#include <QtTest>

using namespace security;

class PermissionMatrixTest : public QObject {
    Q_OBJECT

private slots:
    // 核心条款：两项能力互斥，第二项必须被拒且不部分授予。
    void healthAndInternetAreMutuallyExclusive()
    {
        PermissionMatrix matrix;
        matrix.addUser(QStringLiteral("u1"));

        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::ReadHealthHistory),
                 GrantVerdict::Granted);
        // 用户再单独申请联网：主机必须拒绝第二项。
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::InternetAccess),
                 GrantVerdict::DeniedMutuallyExclusive);
        QVERIFY(matrix.has(QStringLiteral("u1"), 80, Capability::ReadHealthHistory));
        QVERIFY(!matrix.has(QStringLiteral("u1"), 80, Capability::InternetAccess));
    }

    // 反方向同样成立（先联网后读历史）。
    void internetThenHealthIsAlsoRejected()
    {
        PermissionMatrix matrix;
        matrix.addUser(QStringLiteral("u1"));
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::InternetAccess),
                 GrantVerdict::Granted);
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::ReadHealthHistory),
                 GrantVerdict::DeniedMutuallyExclusive);
        QVERIFY(!matrix.has(QStringLiteral("u1"), 80, Capability::ReadHealthHistory));
    }

    // 互斥只在**同一模块同一用户**范围内：不同模块互不影响。
    void exclusionIsPerModule()
    {
        PermissionMatrix matrix;
        matrix.addUser(QStringLiteral("u1"));
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::ReadHealthHistory),
                 GrantVerdict::Granted);
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 81, Capability::InternetAccess),
                 GrantVerdict::Granted);
    }

    // 不互斥的能力可以共存。
    void unrelatedCapabilitiesCoexist()
    {
        PermissionMatrix matrix;
        matrix.addUser(QStringLiteral("u1"));
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::ReadHealthHistory),
                 GrantVerdict::Granted);
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::LocalTransport),
                 GrantVerdict::Granted);
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::SubmitMeasurement),
                 GrantVerdict::Granted);
    }

    // 撤销后可重新授予另一项。
    void revokeUnlocksTheOtherCapability()
    {
        PermissionMatrix matrix;
        matrix.addUser(QStringLiteral("u1"));
        matrix.grant(QStringLiteral("u1"), 80, Capability::ReadHealthHistory);
        QVERIFY(matrix.revoke(QStringLiteral("u1"), 80, Capability::ReadHealthHistory));
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::InternetAccess),
                 GrantVerdict::Granted);
    }

    // 未知能力与未知用户一律拒绝，不静默写入。
    void unknownCapabilityOrUserIsDenied()
    {
        PermissionMatrix matrix;
        matrix.addUser(QStringLiteral("u1"));
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, QStringLiteral("root.everything")),
                 GrantVerdict::DeniedUnknownCapability);
        QCOMPARE(matrix.grant(QStringLiteral("nobody"), 80, Capability::ReadHealthHistory),
                 GrantVerdict::DeniedUnknownUser);
        QVERIFY(matrix.granted(QStringLiteral("u1"), 80).isEmpty());
    }

    // 幂等：重复授予同一能力不报错也不重复记录。
    void grantIsIdempotent()
    {
        PermissionMatrix matrix;
        matrix.addUser(QStringLiteral("u1"));
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::LocalTransport),
                 GrantVerdict::Granted);
        QCOMPARE(matrix.grant(QStringLiteral("u1"), 80, Capability::LocalTransport),
                 GrantVerdict::Granted);
        QCOMPARE(matrix.granted(QStringLiteral("u1"), 80).size(), 1);
    }

    // 切用户时撤销该用户全部能力（报告：切换到无授权用户立即关闭）。
    void revokeAllClearsEveryModule()
    {
        PermissionMatrix matrix;
        matrix.addUser(QStringLiteral("u1"));
        matrix.grant(QStringLiteral("u1"), 80, Capability::ReadHealthHistory);
        matrix.grant(QStringLiteral("u1"), 81, Capability::LocalTransport);
        QCOMPARE(matrix.revokeAll(QStringLiteral("u1")), 2);
        QVERIFY(matrix.granted(QStringLiteral("u1"), 80).isEmpty());
        QVERIFY(matrix.granted(QStringLiteral("u1"), 81).isEmpty());
    }

    // 拒绝原因不得泄露另一项能力的细节。
    void denialReasonDoesNotLeakTheOtherCapability()
    {
        const QString reason = PermissionMatrix::describe(GrantVerdict::DeniedMutuallyExclusive);
        QVERIFY(!reason.contains(QStringLiteral("history.read")));
        QVERIFY(!reason.contains(QStringLiteral("internet.any")));
    }
};

QTEST_GUILESS_MAIN(PermissionMatrixTest)
#include "permission_matrix_test.moc"
