#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

namespace security {


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

    // 离线恢复码（报告 §6.2 / §7.2）：与 PIN 完全独立的一条解封路径。
    // 用户丢 PIN 时用恢复码取回同一个 dbKey；两者都丢才需要销毁数据。
    // 只保存 REK 封装后的密文，不保存恢复码本身。
    QByteArray recoverySalt;        // 16B
    QByteArray wrappedDbKeyByCode;  // AES-256-GCM(REK, dbKey)
    QByteArray dbKeyByCodeNonce;    // 12B
    bool hasRecoveryCode = false;   // false 时该用户没有恢复路径
};

class UserService {
public:
    explicit UserService(QString registryPath = {});

    QString path() const;

    // 首次初始化：注册表里已有任何用户则拒绝，且不覆盖。
    // recoveryCode 由调用方生成并展示给用户一次；传入空则建不带恢复路径的账户。
    // 返回新建的 userId；失败返回空。
    QString createInitialAdmin(const QString &displayName, const QString &pin,
                               const QString &recoveryCode = QString());

    // 管理员建普通用户。非管理员调用返回空。
    QString createUser(const QString &actorUserId, const QString &displayName, const QString &pin,
                       bool admin = false, const QString &recoveryCode = QString());

    // 用恢复码解出 DB key（丢 PIN 时的唯一自救路径）。
    // 成功后不自动改 PIN；调用方应引导用户设置新 PIN。
    bool openWithRecoveryCode(const QString &userId, const QString &recoveryCode,
                              QByteArray *dbKeyOut) const;

    // 为已存在的用户补发恢复码（需要当前 PIN 验证后由调用方触发）。
    // 返回 false 表示用户不存在或参数不合法。
    bool setRecoveryCode(const QString &userId, const QByteArray &dbKey,
                         const QString &recoveryCode);

    // 用户是否有可用的恢复路径。
    bool hasRecoveryCode(const QString &userId) const;

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

}  // namespace security
