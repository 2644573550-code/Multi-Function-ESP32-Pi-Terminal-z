#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

// 用户注册表：AppConfigLocation/users.json，0600，明文（内容本身不含可逆秘密）。
//
// 为什么不放进每用户 SQLCipher 库：PIN 用来解封 DB key，但库要先有 key 才能打开。
// 验证器和封装的 DB key 必须先于库可读，所以落在库外。
//
// 不落在这个文件里的东西：PIN、KEK、解封后的 DB key、raw UID。
struct UserRecord {
    QString userId;          // 不透明 ID，不是用户名
    QString displayName;     // 低敏显示名
    bool admin = false;
    bool disabled = false;
    QByteArray salt;         // 16B
    QByteArray verifier;     // verifyValue(KEK)，不是 KEK
    QByteArray wrappedDbKey; // AES-256-GCM(KEK, dbKey)
    QByteArray dbKeyNonce;   // 12B
    int iterations = 0;      // 建档时的迭代数，用于将来升级参数
};

class UserService {
public:
    explicit UserService(QString registryPath = {});

    QString path() const;

    // 首次初始化：注册表里已有任何用户则拒绝，且不覆盖。
    // 返回新建的 userId；失败返回空。
    QString createInitialAdmin(const QString &displayName, const QString &pin);

    // 管理员建普通用户。非管理员调用返回空。
    QString createUser(const QString &actorUserId, const QString &displayName, const QString &pin,
                       bool admin = false);

    bool disableUser(const QString &actorUserId, const QString &targetUserId);

    // 低敏视图：不含 salt/verifier/wrapped key。
    struct PublicUser {
        QString userId;
        QString displayName;
        bool admin = false;
        bool disabled = false;
    };
    QList<PublicUser> listUsers() const;

    // 只在可信侧使用：认证成功后取回该用户的验证材料。
    bool lookup(const QString &userId, UserRecord *out) const;
    bool replaceRecord(const UserRecord &record);

    bool hasAnyUser() const;

private:
    QList<UserRecord> load() const;
    bool save(const QList<UserRecord> &records) const;
    QString registryPath_;
};
