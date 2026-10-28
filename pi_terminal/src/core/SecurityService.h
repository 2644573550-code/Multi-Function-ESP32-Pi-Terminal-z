#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class TrustStore;   // 注意：TrustStore 在全局命名空间（D 侧契约）

namespace security {
class PermissionMatrix;
class AuditLog;
class UserDatabase;
class CardIndex;
class SessionManager;
}  // namespace security

// 安全服务的统一门面（T-03 / T-08 / D9b / §8.2 / §3.2）。
//
// 为什么需要它：QML 不得直接持有 TrustStore / PermissionMatrix / AuditLog /
// UserDatabase 这些对象 —— 它们能读写密钥与健康数据。本类只暴露
// **经过服务端校验的操作**与**低敏只读视图**，并且每次都要求调用方
// 带上 currentUserId，由 C++ 复核管理员身份与用户归属。
//
// 所有写操作都落一条审计（AuditLog）：成功记 ok，被拒记 denied。
class SecurityService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY changed)

public:
    explicit SecurityService(QObject *parent = nullptr);
    ~SecurityService() override;

    // 由 TerminalApplication 注入。不接管所有权。
    void setSessionManager(security::SessionManager *session);
    // 每用户库：登录后由 SessionManager 打开的那个实例。
    // 未登录时为 nullptr，此时所有写操作一律拒绝。
    void setUserDatabase(security::UserDatabase *database);

    bool ready() const { return ready_; }
    QString lastMessage() const { return lastMessage_; }

    // ---- 桥板信任（T-03）----
    // 只读列表：不返回完整公钥，只给指纹与 key_id。
    Q_INVOKABLE QVariantList trustedBridges() const;
    Q_INVOKABLE bool healthy() const;
    // 配对：publicKeyHex 是 65 字节未压缩点。需要管理员。
    Q_INVOKABLE bool pairBridge(const QString &bridgeId, const QString &publicKeyHex);
    // 撤销：按指纹前缀（16 hex）定位。需要管理员。
    Q_INVOKABLE bool revokeBridge(const QString &fingerprintHex);

    // ---- 卡绑定（T-08 Z 侧）----
    Q_INVOKABLE QVariantList boundCards() const;
    // 绑卡：需要已登录 + 30 秒窗口由调用方持有时序；这里只做归属校验。
    Q_INVOKABLE bool bindCard(const QString &rawUidHex);
    Q_INVOKABLE bool revokeCard(const QString &rawUidHex);
    Q_INVOKABLE int revokeAllCards();

    // ---- 用户级能力授权（§8.2）----
    Q_INVOKABLE QVariantList capabilities(int moduleId) const;
    // 授予/撤销：互斥冲突时返回 false 并给出原因（不部分授予）。
    Q_INVOKABLE bool grantCapability(const QString &targetUserId, int moduleId,
                                     const QString &capability);
    Q_INVOKABLE bool revokeCapability(const QString &targetUserId, int moduleId,
                                      const QString &capability);

    // ---- 审计（§3.2）----
    // 只返回本用户可见的最近记录；不含任何值。
    Q_INVOKABLE QVariantList recentAudit(int limit = 50) const;

    // ---- 数据生命周期（§6.4 / §7）----
    // 导出：加密写出到 path，密码由用户提供。返回 false 表示失败。
    Q_INVOKABLE bool exportEncrypted(const QString &path, const QString &password);
    // 备份/恢复（§7.1/§7.2）：同密码学原语，但语义是"可恢复的完整副本"。
    Q_INVOKABLE bool createBackup(const QString &path, const QString &password);
    Q_INVOKABLE bool restoreBackup(const QString &path, const QString &password);
    // 删除本用户全部数据：需要二次确认由界面负责，这里只执行。
    Q_INVOKABLE bool purgeOwnData();

signals:
    void changed();

private:
    // 当前登录用户；未登录返回空。所有写操作先过这一步。
    QString activeUserId() const;
    bool requireAdmin(const QString &action);
    bool requireUser(const QString &action);
    void setMessage(const QString &message);
    void audit(const QString &action, const QString &target, const QString &result);
    // 懒加载：TrustStore 与 PermissionMatrix 是进程级单例语义。
    ::TrustStore *trustStore() const;
    security::PermissionMatrix *permissions() const;

    security::SessionManager *session_ = nullptr;
    security::UserDatabase *database_ = nullptr;
    mutable ::TrustStore *trustStore_ = nullptr;
    mutable security::PermissionMatrix *permissions_ = nullptr;
    mutable security::CardIndex *cards_ = nullptr;
    bool ready_ = false;
    QString lastMessage_;
};
