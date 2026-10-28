#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// 用户级能力授权（报告 §8.2）。
//
// 核心约束：**同一模块对当前用户不得同时拥有「读取健康历史」与「任意互联网
// 访问」**。即使用户分别发出两项授权，主机也必须拒绝第二项并说明原因。
//
// 注意这不是 UI 层的隐藏：判定在 C++ 服务里，QML 只能查询结果。
namespace security {

// 能力名与 manifest 的 capabilities 对应。
namespace Capability {
constexpr auto ReadHealthHistory = "history.read";
constexpr auto InternetAccess = "internet.any";
constexpr auto LocalTransport = "transport.local";
constexpr auto SubmitMeasurement = "measurement.submit";
}  // namespace Capability

enum class GrantVerdict {
    Granted,
    DeniedMutuallyExclusive,   // 与已授予的互斥能力冲突
    DeniedUnknownCapability,   // 不在支持集合内
    DeniedUnknownUser,
};

// 权限矩阵：userId → moduleId → 已授予能力集合。
// P0 只在内存里保存（每次登录由用户确认）；持久化属 P1。
class PermissionMatrix
{
public:
    // 注册用户。未注册用户的一切授权都会被拒。
    void addUser(const QString &userId);
    void removeUser(const QString &userId);
    bool hasUser(const QString &userId) const;

    // 授予一项能力。互斥冲突时拒绝并保持原状（不部分授予）。
    GrantVerdict grant(const QString &userId, int moduleId, const QString &capability);

    // 撤销。
    bool revoke(const QString &userId, int moduleId, const QString &capability);

    bool has(const QString &userId, int moduleId, const QString &capability) const;

    // 切换用户时撤销该用户全部能力（报告：切换到无授权用户时立即关闭）。
    int revokeAll(const QString &userId);

    QStringList granted(const QString &userId, int moduleId) const;

    // 供界面展示的拒绝原因。
    static QString describe(GrantVerdict verdict);

    // 能力是否被本机支持（未知能力不得写入）。
    static bool isSupported(const QString &capability);

private:
    // 同一模块同一用户下，这两项互斥。
    static bool conflict(const QString &a, const QString &b);

    QHash<QString, QHash<int, QStringList>> grants_;
};

}  // namespace security
