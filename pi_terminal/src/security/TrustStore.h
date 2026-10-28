#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

// 桥板身份信任存储（pbr-secure-v1 §2.1 / §6.7）。
//
// 私钥存储定级：**仅测试级，不构成生产保障。** P0 以 0600 明文文件保存
// 公钥与配对记录；NVS 加密 / eFuse 直存列 P1。该措辞与规范 §2 保持一致，
// 修改时两处同步。
//
// 与模块包的信任流程（ConnectionManager 的 QSettings("trusted/"+trustKey)）
// 是**两套独立机制**：那一套验的是签名插件包，本类记的是桥板身份公钥。
// 不得混用，也不得改动模块包那一套。
//
// 线程约定：只在 GUI 线程使用，内部不加锁。
class TrustStore
{
public:
    struct Record
    {
        QString bridgeId;
        QByteArray publicKey;    // 65 字节未压缩点，0x04 开头
        QByteArray fingerprint;  // SHA-256(publicKey) 前 8 字节
        qint64 enrolledAt = 0;   // 毫秒时间戳
        qint64 revokedAt = 0;    // 0 表示仍有效

        bool active() const { return revokedAt == 0; }
        // 线上封装里的 1 字节选钥值。**只用于选钥，不作身份判定**
        // （碰撞面 1/256，见规范 §2.1）。
        quint8 keyId() const { return quint8(fingerprint.at(0)); }
    };

    // path 为空时用 AppConfigLocation/pbr_trust.json（config，不是 data）：
    // 身份判定发生在登录之前，不能放在按用户的运行数据目录里。
    explicit TrustStore(QString path = QString());
    ~TrustStore();

    QString path() const { return path_; }

    // 失败返回空 QByteArray；publicKey 必须是 65 字节且 0x04 开头。
    static QByteArray fingerprintOf(const QByteArray &publicKey);
    static quint8 keyIdOf(const QByteArray &publicKey);
    // "%02x"，与事件字段的 2 位小写 hex 一致。
    static QString keyIdText(quint8 keyId);
    static bool isValidPublicKey(const QByteArray &publicKey);
    static bool isValidBridgeId(const QString &bridgeId);

    // 按线上 1 字节 key_id 取**全部**未撤销候选。
    // 可能 0 条、也可能多条（key_id 只有 1 字节，冲突是正常的）——
    // 调用方必须逐个用完整公钥验签，**不得取第一条**。
    QVector<Record> lookupIdentity(quint8 keyId) const;

    // 按完整公钥精确查（§6.7 已配对判定：逐字节比较 65 字节）。
    std::optional<Record> findActive(const QByteArray &publicKey) const;

    QVector<Record> all() const;
    bool hasActiveIdentity() const;

    // 登记一条桥板身份。已存在同一公钥的有效记录时**拒绝覆盖**
    // （换板须先 revokeIdentity，对应 §6.7「不覆盖」的裁决）。
    bool enrollIdentity(const QString &bridgeId, const QByteArray &publicKey, QString *error = nullptr);

    // 撤销：标记 revokedAt，不删除记录（保留审计线索）。
    // 调用方在成功后必须**立即断开现有连接并销毁派生会话密钥**（§6.7）。
    bool revokeIdentity(const QByteArray &publicKey, QString *error = nullptr);
    bool revokeByFingerprint(const QByteArray &fingerprint, QString *error = nullptr);

    // 从磁盘重新读取；文件不存在视为空库并返回 true。
    bool reload(QString *error = nullptr);

private:
    bool ensureLoaded(QString *error) const;
    bool save(QString *error);

    QString path_;
    mutable QVector<Record> records_;
    mutable bool loaded_ = false;
    mutable bool corrupted_ = false;
};
