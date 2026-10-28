#include "core/SlotRouter.h"

#include <cstdlib>
#include <iostream>

// T-05 验收：双槽交错事件、事件不串槽、槽位复用、迟到事件、冲突隔离。
// 与 protocol_test 同风格：纯 main + require。

static int g_checks = 0;

static void require(bool ok, const char *message)
{
    ++g_checks;
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static const QString kBridgeA = QStringLiteral("bridge-a");
static const QString kBridgeB = QStringLiteral("bridge-b");
static const QString kGen1 = QStringLiteral("11111111111111111111111111111111");
static const QString kGen2 = QStringLiteral("22222222222222222222222222222222");
static const QString kGen3 = QStringLiteral("33333333333333333333333333333333");
static const QString kDev1 = QStringLiteral("device-1");
static const QString kDev2 = QStringLiteral("device-2");

// ---------------------------------------------------------------------------
// 1. 基本绑定与路由
// ---------------------------------------------------------------------------
static void basicBindRoute()
{
    SlotRouter router;
    require(!router.isBound(0), "slot 0 unbound initially");
    require(router.route(kBridgeA, 0, kDev1, kGen1).verdict == SlotRouter::Verdict::Unbound,
            "route before bind is Unbound");

    const auto bind = router.bind(kBridgeA, 0, kDev1, kGen1, QStringLiteral("sess-1"), 1000);
    require(bind.verdict == SlotRouter::Verdict::Accepted, "bind accepted");
    require(bind.instance == QStringLiteral("sess-1"), "bind returns instance");
    require(router.isBound(0), "slot 0 bound");
    require(router.binding(0).deviceId == kDev1, "binding carries device");
    require(router.binding(0).boundAt == 1000, "binding carries timestamp");

    const auto route = router.route(kBridgeA, 0, kDev1, kGen1);
    require(route.verdict == SlotRouter::Verdict::Accepted, "route accepted");
    require(route.instance == QStringLiteral("sess-1"), "route returns bound instance");

    // 幂等：同一绑定重复上报不改变状态。
    const auto again = router.bind(kBridgeA, 0, kDev1, kGen1, QStringLiteral("sess-1"), 2000);
    require(again.verdict == SlotRouter::Verdict::Accepted, "duplicate bind is idempotent");
    require(router.binding(0).boundAt == 1000, "idempotent bind does not rewrite timestamp");

    // 释放后回到 Unbound。
    require(router.release(kBridgeA, 0, kGen1, 3000).verdict == SlotRouter::Verdict::Accepted,
            "release accepted");
    require(!router.isBound(0), "slot unbound after release");
    require(router.route(kBridgeA, 0, kDev1, kGen1).verdict == SlotRouter::Verdict::StaleGeneration,
            "route after release reports stale for the retired generation");
    require(router.release(kBridgeA, 0, kGen1, 4000).verdict == SlotRouter::Verdict::StaleGeneration,
            "second release reports stale");
}

// ---------------------------------------------------------------------------
// 2. 双槽交错（T-05 明确要求）
// ---------------------------------------------------------------------------
static void interleavedSlots()
{
    SlotRouter router;
    require(router.bind(kBridgeA, 0, kDev1, kGen1, QStringLiteral("s0"), 1).verdict
                == SlotRouter::Verdict::Accepted, "bind slot 0");
    require(router.bind(kBridgeA, 1, kDev2, kGen2, QStringLiteral("s1"), 2).verdict
                == SlotRouter::Verdict::Accepted, "bind slot 1");

    // 交错 20 轮，每次都必须路由到自己那一槽，不得串槽。
    for (int i = 0; i < 20; ++i) {
        const auto d0 = router.route(kBridgeA, 0, kDev1, kGen1);
        require(d0.verdict == SlotRouter::Verdict::Accepted, "interleaved slot 0 accepted");
        require(d0.instance == QStringLiteral("s0"), "interleaved slot 0 instance");

        const auto d1 = router.route(kBridgeA, 1, kDev2, kGen2);
        require(d1.verdict == SlotRouter::Verdict::Accepted, "interleaved slot 1 accepted");
        require(d1.instance == QStringLiteral("s1"), "interleaved slot 1 instance");
    }

    // 交叉投递：slot 0 的事件带 slot-1 的代次/设备，必须被拒。
    require(router.route(kBridgeA, 0, kDev2, kGen2).verdict
                == SlotRouter::Verdict::GenerationMismatch, "slot 0 rejects slot 1 generation");
    require(router.route(kBridgeA, 0, kDev2, kGen1).verdict
                == SlotRouter::Verdict::DeviceMismatch, "slot 0 rejects slot 1 device under its generation");
    require(router.route(kBridgeA, 1, kDev1, kGen2).verdict
                == SlotRouter::Verdict::DeviceMismatch, "slot 1 rejects slot 0 device");

    // 释放一槽不影响另一槽。
    require(router.release(kBridgeA, 0, kGen1, 10).verdict == SlotRouter::Verdict::Accepted,
            "release slot 0");
    require(!router.isBound(0), "slot 0 released");
    require(router.isBound(1), "slot 1 unaffected by slot 0 release");
    require(router.route(kBridgeA, 1, kDev2, kGen2).instance == QStringLiteral("s1"),
            "slot 1 keeps its instance");
}

// ---------------------------------------------------------------------------
// 3. 槽位复用：换设备换代次，旧代次迟到事件必须被拒
// ---------------------------------------------------------------------------
static void slotReuseAndLateEvents()
{
    SlotRouter router;
    require(router.bind(kBridgeA, 0, kDev1, kGen1, QStringLiteral("s-old"), 1).verdict
                == SlotRouter::Verdict::Accepted, "bind old device");

    // 新设备占用同一槽（桥板可能直接宣布新代次，中间没有 detach）。
    require(router.bind(kBridgeA, 0, kDev2, kGen2, QStringLiteral("s-new"), 2).verdict
                == SlotRouter::Verdict::Accepted, "rebind slot to new device");
    require(router.binding(0).deviceId == kDev2, "slot now owned by new device");
    require(router.binding(0).instance == QStringLiteral("s-new"), "slot now maps to new instance");

    // 旧代次的迟到事件：route / detach 都必须被拒。
    require(router.route(kBridgeA, 0, kDev1, kGen1).verdict == SlotRouter::Verdict::StaleGeneration,
            "late event from old generation is stale");
    require(router.release(kBridgeA, 0, kGen1, 3).verdict == SlotRouter::Verdict::StaleGeneration,
            "late detach from old generation is stale");
    require(router.isBound(0), "late detach did not remove the new binding");
    require(router.binding(0).deviceId == kDev2, "new binding intact after late detach");

    // 新代次仍然正常。
    require(router.route(kBridgeA, 0, kDev2, kGen2).verdict == SlotRouter::Verdict::Accepted,
            "new generation routes fine");

    // 已退役的代次不能被重新绑定（防重放旧 attach）。
    require(router.bind(kBridgeA, 0, kDev1, kGen1, QStringLiteral("s-old"), 4).verdict
                == SlotRouter::Verdict::StaleGeneration, "rebinding a retired generation is rejected");
    require(router.binding(0).deviceId == kDev2, "rejected rebind left state untouched");
}

// ---------------------------------------------------------------------------
// 4. 冲突隔离
// ---------------------------------------------------------------------------
static void conflicts()
{
    SlotRouter router;
    require(router.bind(kBridgeA, 0, kDev1, kGen1, QStringLiteral("s1"), 1).verdict
                == SlotRouter::Verdict::Accepted, "bind");

    // 同代次声称另一台设备 → 冲突，不能覆盖。
    require(router.bind(kBridgeA, 0, kDev2, kGen1, QStringLiteral("s2"), 2).verdict
                == SlotRouter::Verdict::Conflict, "same generation, different device is a conflict");
    require(router.binding(0).deviceId == kDev1, "conflict did not overwrite the binding");

    // 桥板不符 → 拒绝，不改变绑定。
    require(router.route(kBridgeB, 0, kDev1, kGen1).verdict == SlotRouter::Verdict::BridgeMismatch,
            "other bridge cannot route this slot");
    require(router.release(kBridgeB, 0, kGen1, 3).verdict == SlotRouter::Verdict::BridgeMismatch,
            "other bridge cannot release this slot");
    require(router.isBound(0), "binding intact after foreign bridge attempts");
}

// ---------------------------------------------------------------------------
// 5. 入参校验
// ---------------------------------------------------------------------------
static void argumentValidation()
{
    SlotRouter router;
    for (int slot : {-1, 2, 99}) {
        require(router.bind(kBridgeA, slot, kDev1, kGen1, QStringLiteral("x"), 1).verdict
                    == SlotRouter::Verdict::SlotMismatch, "bind rejects invalid slot");
        require(router.route(kBridgeA, slot, kDev1, kGen1).verdict
                    == SlotRouter::Verdict::SlotMismatch, "route rejects invalid slot");
        require(router.release(kBridgeA, slot, kGen1, 1).verdict
                    == SlotRouter::Verdict::SlotMismatch, "release rejects invalid slot");
    }

    // 代次格式：必须 32 位小写 hex。
    const QStringList badGenerations{
        QString(), QStringLiteral("1111"),
        QStringLiteral("1111111111111111111111111111111G"),
        QStringLiteral("111111111111111111111111111111111"),   // 33
        QStringLiteral("1111111111111111111111111111111"),    // 31
        QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"),    // 大写
    };
    for (const QString &generation : badGenerations) {
        require(router.bind(kBridgeA, 0, kDev1, generation, QStringLiteral("x"), 1).verdict
                    == SlotRouter::Verdict::GenerationMismatch, "bind rejects malformed generation");
        require(router.route(kBridgeA, 0, kDev1, generation).verdict
                    == SlotRouter::Verdict::GenerationMismatch, "route rejects malformed generation");
    }
    require(SlotRouter::isValidGeneration(kGen1), "well-formed generation accepted by helper");
    require(!SlotRouter::isValidGeneration(QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")),
            "uppercase generation rejected by helper");

    // 空设备名拒绝。
    require(router.bind(kBridgeA, 0, QString(), kGen1, QStringLiteral("x"), 1).verdict
                == SlotRouter::Verdict::DeviceMismatch, "empty device rejected");
}

// ---------------------------------------------------------------------------
// 6. 桥板失联：位置 unknown
// ---------------------------------------------------------------------------
static void bridgeOffline()
{
    SlotRouter router;
    // 未登记过的桥板默认在线，首次 bring-up 不应被自己挡住。
    require(router.bridgeOnline(kBridgeA), "unregistered bridge is treated as online");
    require(router.bind(kBridgeA, 0, kDev1, kGen1, QStringLiteral("s1"), 1).verdict
                == SlotRouter::Verdict::Accepted, "bind while bridge considered online");

    router.setBridgeOnline(kBridgeA, false);
    require(!router.bridgeOnline(kBridgeA), "bridge marked offline");

    // 离线时：拒绝新的物理绑定，拒绝位置事件。
    require(router.bind(kBridgeA, 1, kDev2, kGen2, QStringLiteral("s2"), 2).verdict
                == SlotRouter::Verdict::PositionUnknown, "offline bridge cannot create a binding");
    require(!router.isBound(1), "no binding created while offline");
    require(router.route(kBridgeA, 0, kDev1, kGen1).verdict == SlotRouter::Verdict::PositionUnknown,
            "position event rejected while bridge offline");

    // 已有绑定不被本类清除：业务继续，由上层判断。
    require(router.isBound(0), "existing binding retained while offline");

    // 恢复上线后位置重新可信。
    router.setBridgeOnline(kBridgeA, true);
    require(router.route(kBridgeA, 0, kDev1, kGen1).verdict == SlotRouter::Verdict::Accepted,
            "routing works again after bridge recovers");
    require(router.bind(kBridgeA, 1, kDev2, kGen2, QStringLiteral("s2"), 3).verdict
                == SlotRouter::Verdict::Accepted, "binding allowed again after recovery");
}

// ---------------------------------------------------------------------------
// 7. 退役历史上限：最旧的降级为 GenerationMismatch，但依然拒绝
// ---------------------------------------------------------------------------
static void retiredHistoryBound()
{
    SlotRouter router(2);   // 只保留最近 2 个退役代次
    const QStringList generations{
        QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        QStringLiteral("cccccccccccccccccccccccccccccccc"),
        QStringLiteral("dddddddddddddddddddddddddddddddd"),
    };
    for (int i = 0; i < generations.size(); ++i) {
        require(router.bind(kBridgeA, 0, kDev1, generations.at(i), QStringLiteral("s"), i).verdict
                    == SlotRouter::Verdict::Accepted, "sequential rebind accepted");
        if (i + 1 < generations.size()) {
            require(router.release(kBridgeA, 0, generations.at(i), i).verdict
                        == SlotRouter::Verdict::Accepted, "release previous generation");
        }
    }

    // 最近两个退役代次仍被识别为 Stale。
    require(router.route(kBridgeA, 0, kDev1, generations.at(2)).verdict
                == SlotRouter::Verdict::StaleGeneration, "recent retired generation is stale");
    // 最旧的已被历史淘汰：判成 GenerationMismatch，**仍然是拒绝**。
    const auto old = router.route(kBridgeA, 0, kDev1, generations.at(0));
    require(old.verdict != SlotRouter::Verdict::Accepted, "over-aged generation is still rejected");
    require(old.verdict == SlotRouter::Verdict::GenerationMismatch,
            "over-aged generation degrades to GenerationMismatch, not acceptance");
}

// ---------------------------------------------------------------------------
// 8. clear() 与判定名
// ---------------------------------------------------------------------------
static void clearAndNames()
{
    SlotRouter router;
    router.bind(kBridgeA, 0, kDev1, kGen1, QStringLiteral("s"), 1);
    router.setBridgeOnline(kBridgeB, false);
    router.clear();
    require(!router.isBound(0), "clear drops bindings");
    require(router.route(kBridgeA, 0, kDev1, kGen1).verdict == SlotRouter::Verdict::Unbound,
            "after clear, retired history is gone too, so route is Unbound");
    require(router.bridgeOnline(kBridgeB), "clear drops bridge state");

    require(SlotRouter::verdictName(SlotRouter::Verdict::Accepted) == QStringLiteral("ACCEPTED"),
            "verdict name accepted");
    require(SlotRouter::verdictName(SlotRouter::Verdict::StaleGeneration)
                == QStringLiteral("STALE_GENERATION"), "verdict name stale");
    require(SlotRouter::verdictName(SlotRouter::Verdict::PositionUnknown)
                == QStringLiteral("POSITION_UNKNOWN"), "verdict name unknown position");
}

int main()
{
    basicBindRoute();
    interleavedSlots();
    slotReuseAndLateEvents();
    conflicts();
    argumentValidation();
    bridgeOffline();
    retiredHistoryBound();
    clearAndNames();
    std::cout << "slot router tests passed (" << g_checks << " assertions)\n";
    return 0;
}
