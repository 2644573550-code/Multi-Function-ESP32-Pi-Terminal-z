// 报告 §9.3 第 5、6 条：有限超时、有上限重试、流量上限、恢复时不自动下载。
#include "core/FetchBudget.h"

#include <QSignalSpy>
#include <QtTest>

class FetchBudgetTest : public QObject {
    Q_OBJECT

private slots:
    // 自动重试次数有上限，用尽后必须停下来等用户。
    void autoRetryIsBounded()
    {
        FetchBudget budget;
        QCOMPARE(budget.remainingRetries(), budget.maxAutoRetries());
        QVERIFY(budget.consumeAutoRetry());
        QVERIFY(budget.consumeAutoRetry());
        QCOMPARE(budget.remainingRetries(), 0);
        // 第三次必须失败：不能自己继续循环。
        QVERIFY2(!budget.consumeAutoRetry(), "auto retry must stop at the limit");
    }

    // 只有用户主动重试才重置预算。
    void userRetryResetsBudget()
    {
        FetchBudget budget;
        while (budget.consumeAutoRetry()) { /* 用尽 */ }
        QCOMPARE(budget.remainingRetries(), 0);
        budget.userRetry();
        QCOMPARE(budget.remainingRetries(), budget.maxAutoRetries());
    }

    // 单次下载超限直接拒绝，不截断。
    void singleDownloadLimitIsEnforced()
    {
        FetchBudget budget;
        QVERIFY(!budget.recordBytes(budget.maxSingleDownloadBytes() + 1));
        QCOMPARE(budget.usedBytes(), 0);   // 拒绝时不记账
        QVERIFY(budget.recordBytes(1024));
        QCOMPARE(budget.usedBytes(), 1024);
    }

    // 累计预算：超出后拒绝新的下载。
    void cumulativeBudgetIsEnforced()
    {
        FetchBudget budget;
        budget.setByteBudget(2048);
        QVERIFY(budget.recordBytes(1024));
        QVERIFY(budget.recordBytes(1024));
        QCOMPARE(budget.usedBytes(), 2048);
        QVERIFY2(!budget.recordBytes(1), "must reject beyond the cumulative budget");
        QCOMPARE(budget.usedBytes(), 2048);
        // 用户清空后可以继续。
        budget.resetUsage();
        QVERIFY(budget.recordBytes(512));
    }

    // §9.3 第 6 条：网络刚恢复不得自动下载大包。
    void recoveryDoesNotAutoDownload()
    {
        FetchBudget budget;
        // 小包、预算充足、非恢复期：可以自动。
        QVERIFY(budget.autoDownloadAllowed(1024, false));
        // 恢复期：一律不自动，无论大小。
        QVERIFY2(!budget.autoDownloadAllowed(1024, true),
                 "must not auto-download right after network recovery");
        // 超过单次上限：一律不自动。
        QVERIFY(!budget.autoDownloadAllowed(budget.maxSingleDownloadBytes() + 1, false));
    }

    // 自动重试用尽后，自动下载也必须停。
    void exhaustedRetriesBlockAutoDownload()
    {
        FetchBudget budget;
        while (budget.consumeAutoRetry()) { /* 用尽 */ }
        QVERIFY(!budget.autoDownloadAllowed(1024, false));
    }

    // 超时必须是一个有限正值：不能是 0（无限等）或负（立即失败）。
    void timeoutIsFinite()
    {
        FetchBudget budget;
        QVERIFY(budget.timeoutMs() > 0);
        QVERIFY(budget.timeoutMs() <= 60000);
    }
};

QTEST_GUILESS_MAIN(FetchBudgetTest)
#include "fetch_budget_test.moc"
