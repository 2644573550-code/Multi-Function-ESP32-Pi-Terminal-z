#include "core/SlotRouter.h"

#include <QRegularExpression>

namespace {
const QString kUnknownDetail = QStringLiteral("position unknown: bridge offline");
}

SlotRouter::SlotRouter(int retiredHistory)
    : retiredHistory_(retiredHistory < 1 ? 1 : retiredHistory)
{
}

bool SlotRouter::isValidGeneration(const QString &generation)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{32}$"));
    return pattern.match(generation).hasMatch();
}

QString SlotRouter::verdictName(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Accepted: return QStringLiteral("ACCEPTED");
    case Verdict::Unbound: return QStringLiteral("UNBOUND");
    case Verdict::StaleGeneration: return QStringLiteral("STALE_GENERATION");
    case Verdict::GenerationMismatch: return QStringLiteral("GENERATION_MISMATCH");
    case Verdict::DeviceMismatch: return QStringLiteral("DEVICE_MISMATCH");
    case Verdict::BridgeMismatch: return QStringLiteral("BRIDGE_MISMATCH");
    case Verdict::SlotMismatch: return QStringLiteral("SLOT_MISMATCH");
    case Verdict::PositionUnknown: return QStringLiteral("POSITION_UNKNOWN");
    case Verdict::Conflict: return QStringLiteral("CONFLICT");
    }
    return QStringLiteral("UNKNOWN");
}

void SlotRouter::setBridgeOnline(const QString &bridgeId, bool online)
{
    bridgeOnline_[bridgeId] = online;
}

bool SlotRouter::bridgeOnline(const QString &bridgeId) const
{
    // 未登记过的桥板默认视为在线：首次 bring-up 时调用方可能还没上报状态，
    // 按"离线"处理会让第一次插入永远失败。
    auto it = bridgeOnline_.constFind(bridgeId);
    return it == bridgeOnline_.constEnd() ? true : it.value();
}

bool SlotRouter::isRetired(int slotId, const QString &generation) const
{
    return retired_.value(slotId).contains(generation);
}

void SlotRouter::retire(int slotId, const QString &generation)
{
    if (generation.isEmpty()) return;
    QStringList &list = retired_[slotId];
    list.removeAll(generation);
    list.prepend(generation);
    while (list.size() > retiredHistory_) list.removeLast();
}

SlotRouter::Decision SlotRouter::note(Decision decision) const
{
    if (decision.verdict == Verdict::Accepted) {
        ++accepted_;
    } else {
        ++rejected_;
        lastRejection_ = verdictName(decision.verdict);
    }
    return decision;
}

SlotRouter::Decision SlotRouter::bind(const QString &bridgeId, int slotId, const QString &deviceId,
                                      const QString &generation, const QString &instance, qint64 now)
{
    Decision decision;
    if (!isValidSlot(slotId)) {
        decision.verdict = Verdict::SlotMismatch;
        decision.detail = QStringLiteral("slot must be 0 or 1");
        return note(decision);
    }
    if (!isValidGeneration(generation)) {
        decision.verdict = Verdict::GenerationMismatch;
        decision.detail = QStringLiteral("generation must be 32 lowercase hex");
        return note(decision);
    }
    if (deviceId.isEmpty()) {
        decision.verdict = Verdict::DeviceMismatch;
        decision.detail = QStringLiteral("device_id must not be empty");
        return note(decision);
    }
    if (!bridgeOnline(bridgeId)) {
        // 桥板失联时不允许建立新的物理绑定。
        decision.verdict = Verdict::PositionUnknown;
        decision.detail = kUnknownDetail;
        return note(decision);
    }

    const Binding existing = current_.value(slotId);
    if (existing.valid()) {
        if (existing.generation == generation && existing.deviceId == deviceId
            && existing.bridgeId == bridgeId) {
            // 同一绑定重复上报：幂等接受，不动状态。
            decision.verdict = Verdict::Accepted;
            decision.instance = existing.instance;
            return note(decision);
        }
        if (existing.generation == generation && existing.deviceId != deviceId) {
            // 同一代次声称两台设备：状态被破坏，拒绝而不是覆盖。
            decision.verdict = Verdict::Conflict;
            decision.detail = QStringLiteral("same generation already bound to another device");
            return note(decision);
        }
        // 换设备/换代次必须显式 release 或带来新代次。这里允许"刷新"：
        // 桥板在上电/重新插拔时会直接宣布新代次，中间可能没有 detach。
        retire(slotId, existing.generation);
    }

    // 已经被替代过的旧代次不能重新绑定（防止重放旧的 attach）。
    if (isRetired(slotId, generation)) {
        decision.verdict = Verdict::StaleGeneration;
        decision.detail = QStringLiteral("generation was already superseded");
        return note(decision);
    }

    Binding binding;
    binding.bridgeId = bridgeId;
    binding.slotId = slotId;
    binding.deviceId = deviceId;
    binding.generation = generation;
    binding.instance = instance;
    binding.boundAt = now;
    current_.insert(slotId, binding);

    decision.verdict = Verdict::Accepted;
    decision.instance = instance;
    return note(decision);
}

SlotRouter::Decision SlotRouter::release(const QString &bridgeId, int slotId,
                                         const QString &generation, qint64 now)
{
    Q_UNUSED(now);
    Decision decision;
    if (!isValidSlot(slotId)) {
        decision.verdict = Verdict::SlotMismatch;
        decision.detail = QStringLiteral("slot must be 0 or 1");
        return note(decision);
    }
    if (!isValidGeneration(generation)) {
        decision.verdict = Verdict::GenerationMismatch;
        decision.detail = QStringLiteral("generation must be 32 lowercase hex");
        return note(decision);
    }

    const Binding existing = current_.value(slotId);
    if (!existing.valid()) {
        // 从未绑定：迟到的 detach。若代次是退役过的，明确判迟到。
        decision.verdict = isRetired(slotId, generation) ? Verdict::StaleGeneration : Verdict::Unbound;
        decision.detail = QStringLiteral("no binding on slot");
        return note(decision);
    }
    if (existing.bridgeId != bridgeId) {
        decision.verdict = Verdict::BridgeMismatch;
        decision.detail = QStringLiteral("bridge does not own this slot");
        return note(decision);
    }
    if (existing.generation != generation) {
        // **关键**：迟到的 detach 不得解掉新设备的绑定。
        decision.verdict = isRetired(slotId, generation) ? Verdict::StaleGeneration
                                                         : Verdict::GenerationMismatch;
        decision.detail = QStringLiteral("generation does not match current binding");
        return note(decision);
    }

    retire(slotId, existing.generation);
    current_.remove(slotId);
    decision.verdict = Verdict::Accepted;
    decision.instance = existing.instance;
    return note(decision);
}

SlotRouter::Decision SlotRouter::route(const QString &bridgeId, int slotId,
                                       const QString &deviceId, const QString &generation) const
{
    Decision decision;
    if (!isValidSlot(slotId)) {
        decision.verdict = Verdict::SlotMismatch;
        decision.detail = QStringLiteral("slot must be 0 or 1");
        return note(decision);
    }
    if (!isValidGeneration(generation)) {
        decision.verdict = Verdict::GenerationMismatch;
        decision.detail = QStringLiteral("generation must be 32 lowercase hex");
        return note(decision);
    }

    const Binding existing = current_.value(slotId);
    if (!existing.valid()) {
        decision.verdict = isRetired(slotId, generation) ? Verdict::StaleGeneration : Verdict::Unbound;
        decision.detail = QStringLiteral("no binding on slot");
        return note(decision);
    }
    if (existing.generation != generation) {
        decision.verdict = isRetired(slotId, generation) ? Verdict::StaleGeneration
                                                         : Verdict::GenerationMismatch;
        decision.detail = QStringLiteral("generation does not match current binding");
        return note(decision);
    }
    if (existing.bridgeId != bridgeId) {
        decision.verdict = Verdict::BridgeMismatch;
        decision.detail = QStringLiteral("bridge does not own this slot");
        return note(decision);
    }
    if (existing.deviceId != deviceId) {
        decision.verdict = Verdict::DeviceMismatch;
        decision.detail = QStringLiteral("device does not match binding");
        return note(decision);
    }
    if (!bridgeOnline(bridgeId)) {
        // 位置事件本身来自桥板，桥板失联后不可能有新事件；
        // 保留此判断以防调用方在离线期间回放缓冲事件。
        decision.verdict = Verdict::PositionUnknown;
        decision.detail = kUnknownDetail;
        return note(decision);
    }

    decision.verdict = Verdict::Accepted;
    decision.instance = existing.instance;
    return note(decision);
}

SlotRouter::Binding SlotRouter::binding(int slotId) const
{
    return current_.value(slotId);
}

bool SlotRouter::isBound(int slotId) const
{
    return current_.value(slotId).valid();
}

void SlotRouter::clear()
{
    current_.clear();
    retired_.clear();
    bridgeOnline_.clear();
    accepted_ = 0;
    rejected_ = 0;
    lastRejection_.clear();
}
