#include "security/PermissionMatrix.h"

namespace security {

bool PermissionMatrix::isSupported(const QString &capability) {
    return capability == QLatin1String(Capability::ReadHealthHistory)
        || capability == QLatin1String(Capability::InternetAccess)
        || capability == QLatin1String(Capability::LocalTransport)
        || capability == QLatin1String(Capability::SubmitMeasurement);
}

bool PermissionMatrix::conflict(const QString &a, const QString &b) {
    // §8.2 的强制互斥：读健康历史 vs 任意互联网访问。
    const bool aHealth = a == QLatin1String(Capability::ReadHealthHistory);
    const bool aNet = a == QLatin1String(Capability::InternetAccess);
    const bool bHealth = b == QLatin1String(Capability::ReadHealthHistory);
    const bool bNet = b == QLatin1String(Capability::InternetAccess);
    return (aHealth && bNet) || (aNet && bHealth);
}

void PermissionMatrix::addUser(const QString &userId) {
    if (userId.isEmpty()) return;
    grants_.insert(userId, grants_.value(userId));
}

void PermissionMatrix::removeUser(const QString &userId) {
    grants_.remove(userId);
}

bool PermissionMatrix::hasUser(const QString &userId) const {
    return grants_.contains(userId);
}

GrantVerdict PermissionMatrix::grant(const QString &userId, int moduleId,
                                     const QString &capability) {
    if (!grants_.contains(userId)) return GrantVerdict::DeniedUnknownUser;
    if (!isSupported(capability)) return GrantVerdict::DeniedUnknownCapability;

    QStringList &list = grants_[userId][moduleId];
    if (list.contains(capability)) return GrantVerdict::Granted;   // 幂等

    // 互斥检查必须在写入之前：冲突时保持原状，不做部分授予。
    for (const QString &existing : list)
        if (conflict(existing, capability)) return GrantVerdict::DeniedMutuallyExclusive;

    list.append(capability);
    return GrantVerdict::Granted;
}

bool PermissionMatrix::revoke(const QString &userId, int moduleId, const QString &capability) {
    if (!grants_.contains(userId)) return false;
    auto moduleIt = grants_[userId].find(moduleId);
    if (moduleIt == grants_[userId].end()) return false;
    return moduleIt->removeAll(capability) > 0;
}

bool PermissionMatrix::has(const QString &userId, int moduleId, const QString &capability) const {
    const auto userIt = grants_.constFind(userId);
    if (userIt == grants_.constEnd()) return false;
    const auto moduleIt = userIt->constFind(moduleId);
    if (moduleIt == userIt->constEnd()) return false;
    return moduleIt->contains(capability);
}

int PermissionMatrix::revokeAll(const QString &userId) {
    const auto userIt = grants_.constFind(userId);
    if (userIt == grants_.constEnd()) return 0;
    int total = 0;
    for (auto it = userIt->cbegin(); it != userIt->cend(); ++it) total += it->size();
    grants_[userId].clear();
    return total;
}

QStringList PermissionMatrix::granted(const QString &userId, int moduleId) const {
    const auto userIt = grants_.constFind(userId);
    if (userIt == grants_.constEnd()) return {};
    return userIt->value(moduleId);
}

QString PermissionMatrix::describe(GrantVerdict verdict) {
    switch (verdict) {
    case GrantVerdict::Granted:
        return QStringLiteral("granted");
    case GrantVerdict::DeniedMutuallyExclusive:
        // 不暴露另一项能力的细节，只说明冲突。
        return QStringLiteral("denied: mutually exclusive with an already granted capability");
    case GrantVerdict::DeniedUnknownCapability:
        return QStringLiteral("denied: unsupported capability");
    case GrantVerdict::DeniedUnknownUser:
        return QStringLiteral("denied: unknown user");
    }
    return QStringLiteral("denied");
}

}  // namespace security
