#pragma once

#include <QString>
#include <QVector>

namespace security {

class SqlCipherStore;

// 审计记录（报告 §3.2「可审计但不泄露」）。
//
// 原则：
//   - 记**发生了什么**（谁、何时、对什么、结果），不记任何值
//   - 禁止写入 PIN、KEK、DB key、token、raw UID、健康数值、请求正文
//   - 审计落在每用户加密库里：管理员也读不到他人审计明细
//
// 与运行日志（qInfo）的区别：日志可被删改，审计是数据库里的一行，
// 随库一起加密与备份，构成证据。
namespace AuditAction {
// 授权类
constexpr auto GrantDevice = "device.grant";
constexpr auto RevokeDevice = "device.revoke";
constexpr auto PairBridge = "bridge.pair";
constexpr auto RevokeBridge = "bridge.revoke";
// 用户与凭据
constexpr auto CreateUser = "user.create";
constexpr auto DisableUser = "user.disable";
constexpr auto ChangePin = "pin.change";
// 数据生命周期（§6.4 / §7）
constexpr auto ExportData = "data.export";
constexpr auto BackupData = "data.backup";
constexpr auto RestoreData = "data.restore";
constexpr auto PurgeData = "data.purge";
constexpr auto PruneHistory = "data.prune";
// 隐私设置
constexpr auto SetRetention = "privacy.retention";
constexpr auto SetNetworkPolicy = "privacy.network";
}  // namespace AuditAction

namespace AuditResult {
constexpr auto Ok = "ok";
constexpr auto Denied = "denied";
constexpr auto Failed = "failed";
}  // namespace AuditResult

struct AuditEntry {
    qint64 atMs = 0;
    QString actor;    // 发起者的 userId；系统动作为 "system"
    QString action;   // AuditAction::*
    QString target;   // 目标用户/设备/桥板的不透明 ID，不含卡号与健康值
    QString result;   // AuditResult::*
};

// 审计存储：绑定到某个已经打开的加密库连接上。
class AuditLog
{
public:
    // store 必须已打开；不接管所有权。
    explicit AuditLog(SqlCipherStore *store) : store_(store) {}

    // 写一条审计。返回 false 表示写失败（调用方应把业务动作按失败处理，
    // 不能"做了但没记"）。
    bool append(const AuditEntry &entry);

    // 便捷入口。
    bool record(const QString &actor, const QString &action, const QString &target,
                const QString &result);

    // 读最近 N 条（界面展示用；不提供跨用户查询）。
    QVector<AuditEntry> recent(int limit = 100) const;

    // 保留策略：删除早于 beforeMs 的记录。返回删除条数。
    int prune(qint64 beforeMs);

    int count() const;

private:
    SqlCipherStore *store_ = nullptr;
};

// 审计值的白名单校验：任何不在允许集合里的字段一律拒绝写入，
// 防止有人把 PIN/健康值当 target 塞进来。
bool auditFieldIsSafe(const QString &value);

}  // namespace security
