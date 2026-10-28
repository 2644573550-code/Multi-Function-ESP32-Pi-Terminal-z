#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// 物理槽位路由（`pbr-secure-v1` §4.3；`04-dev-tasks-f-day1.md` §4.2）。
//
// 只回答一个问题：**这条位置事件属于当前这次物理绑定吗？**
// 它不持有会话、不启停业务、不碰网络 —— 槽位只是"可空的位置属性"，
// 业务身份是 device_id + device_boot_id + business_session_id + task_id
// + origin_user_id（那部分在 ConnectionManager 里）。
//
// 关键语义：
//   - 同一槽换设备必然换 `binding_generation`；旧代次的**迟到事件必须被拒**，
//     既不能更新新设备的界面，也不能把新设备的绑定解掉。
//   - 拔出（detach）只释放槽位，**不停止**认证/授权仍有效的无线业务。
//   - 桥板失联时位置为 unknown：拒绝新的绑定与物理接入确认，但已有绑定不在
//     本类里被清除（业务继续由上层判断）。
//
// 纯 Qt Core，无网络依赖，便于单测。
class SlotRouter
{
public:
    enum class Verdict {
        Accepted,           // 属于当前绑定
        Unbound,            // 该槽从未绑定
        StaleGeneration,    // 已被替代的旧代次（迟到事件）
        GenerationMismatch, // 代次既不是当前、也不是已知旧代次
        DeviceMismatch,     // 代次对但设备不符
        BridgeMismatch,     // 代次对但桥板不符
        SlotMismatch,       // slot 越界（只允许 0/1）
        PositionUnknown,    // 桥板失联，位置不可信
        Conflict,           // 槽上已有别的设备绑定，需先 release
    };

    struct Binding
    {
        QString bridgeId;
        int slotId = -1;
        QString deviceId;
        QString generation;  // 32 位小写 hex
        QString instance;    // Pi 侧会话实例标签
        qint64 boundAt = 0;
        bool valid() const { return slotId >= 0 && !generation.isEmpty(); }
    };

    struct Decision
    {
        Verdict verdict = Verdict::Unbound;
        QString instance;   // 接受时为当前绑定实例
        QString detail;     // 供日志，不含敏感数据
    };

    static bool isValidSlot(int slotId) { return slotId == 0 || slotId == 1; }
    static bool isValidGeneration(const QString &generation);
    static QString verdictName(Verdict verdict);

    // 保留多少个已退役代次用于识别迟到事件；超出后最旧的被丢弃
    // （丢弃只会把迟到事件判成 GenerationMismatch，仍然拒绝）。
    explicit SlotRouter(int retiredHistory = 8);

    void setBridgeOnline(const QString &bridgeId, bool online);
    bool bridgeOnline(const QString &bridgeId) const;

    // 登记一次新的物理绑定。同一代次重复登记是幂等的。
    Decision bind(const QString &bridgeId, int slotId, const QString &deviceId,
                  const QString &generation, const QString &instance, qint64 now);

    // 释放槽位（触点拔出）。代次必须匹配当前绑定，否则拒绝——
    // 迟到的 detach 不得解掉新设备的绑定。
    Decision release(const QString &bridgeId, int slotId, const QString &generation, qint64 now);

    // 判定一条位置事件是否属于当前绑定。
    Decision route(const QString &bridgeId, int slotId, const QString &deviceId,
                   const QString &generation) const;

    Binding binding(int slotId) const;
    // 所有当前有效绑定（供桥板失联时批量置离线、界面展示）。
    QVector<Binding> bindings() const;
    bool isBound(int slotId) const;
    void clear();

    // 判定计数（排查用）。运维遇到「刷卡没反应」时，需要能看出事件是不是
    // 被当成迟到/串槽丢掉了——只写日志在界面上看不见。
    quint64 acceptedCount() const { return accepted_; }
    quint64 rejectedCount() const { return rejected_; }
    // 最近一次拒绝的原因（verdictName）；从未拒绝时为空。
    QString lastRejection() const { return lastRejection_; }

private:
    bool isRetired(int slotId, const QString &generation) const;
    void retire(int slotId, const QString &generation);

    Decision note(Decision decision) const;

    QHash<int, Binding> current_;
    QHash<int, QStringList> retired_;   // slot -> 旧代次（新→旧）
    QHash<QString, bool> bridgeOnline_;
    int retiredHistory_;
    // 计数与最近拒绝原因在 const 判定里更新，故为 mutable。
    mutable quint64 accepted_ = 0;
    mutable quint64 rejected_ = 0;
    mutable QString lastRejection_;
};
