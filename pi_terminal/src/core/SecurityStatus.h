#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

class ConnectionManager;
class HostIdentity;
class TrustStore;

// 界面接点（只读）。**不是服务门面，不接受写操作。**
//
// 背景：`04-dev-tasks-api-ui.md` 的 T-09（登录/绑卡/锁屏/挂失）主责是 Mr.Z，
// 文件范围含 `qml/Main.qml`、`PageProvider`、新增登录/绑卡 QML。D 不碰那些页面，
// 只把 D 侧四块数据做成可读接点，由 Z 在 T-09 里接。
//
// 四块数据：
//   1. 桥板信任记录（T-03a TrustStore）
//   2. 本机身份指纹与状态（T-03b HostIdentity）
//   3. 物理槽位绑定（T-05 SlotRouter，快照）
//   4. UID 索引密钥是否就绪（T-08 的 uidIndexKey）
//
// 刷新模型（与用户确认过的取舍）：
//   - 信任/身份是**响应式**：用 QFileSystemWatcher 盯 pbr_trust.json 与
//     host_identity.pem，任何人写完都会触发 reload 并发信号。这样 Z 的 T-09b
//     或未来 T-07 用别的方式写文件时，界面无需我方提供写 API 也能更新。
//   - 槽位是**快照**：`slots()` 每次调用回读 ConnectionManager，页面自己决定
//     何时拉取（QML 里用 `slots()` 即可）。
//
// 隐私边界：不暴露 raw UID、不暴露私钥、不暴露会话密钥；只暴露公钥派生的
// 指纹与 key_id。
class SecurityStatus : public QObject
{
    Q_OBJECT
    // 每条：bridgeId, fingerprintHex, keyId, enrolledAt, revokedAt, active
    Q_PROPERTY(QVariantList trustRecords READ trustRecords NOTIFY trustChanged)
    Q_PROPERTY(bool trustStoreHealthy READ trustStoreHealthy NOTIFY trustChanged)
    Q_PROPERTY(QString trustStorePath READ trustStorePath CONSTANT)
    Q_PROPERTY(QString identityFingerprint READ identityFingerprint NOTIFY identityChanged)
    Q_PROPERTY(int identityKeyId READ identityKeyId NOTIFY identityChanged)
    // ready / missing / corrupt / unavailable（unavailable = 本次构建没接 mbedtls）
    Q_PROPERTY(QString identityState READ identityState NOTIFY identityChanged)
    Q_PROPERTY(QString identityPath READ identityPath CONSTANT)
    Q_PROPERTY(bool uidKeyReady READ uidKeyReady NOTIFY uidKeyChanged)

public:
    // trustPath / identityPath 为空时用各自的默认位置（AppConfigLocation）。
    // 允许注入是为了让测试不污染真实配置目录——生产代码只用默认值。
    explicit SecurityStatus(ConnectionManager *connection,
                            const QString &trustPathOverride = QString(),
                            const QString &identityPathOverride = QString(),
                            QObject *parent = nullptr);
    ~SecurityStatus() override;

    QVariantList trustRecords() const;
    bool trustStoreHealthy() const;
    QString trustStorePath() const;
    QString identityFingerprint() const;
    int identityKeyId() const;
    QString identityState() const;
    QString identityPath() const;
    bool uidKeyReady() const;

    // 槽位快照（只读）。0/1 两个槽各一条，未绑定时 bound=false。
    Q_INVOKABLE QVariantList slotSnapshot() const;

    // 手动刷新（重新读盘 + 重算 UID 密钥就绪状态）。文件监听之外的兜底。
    Q_INVOKABLE void refresh();

signals:
    void trustChanged();
    void identityChanged();
    void uidKeyChanged();

private:
    void watchFile(const QString &path);
    void reloadTrust();
    void reloadIdentity();
    void reloadUidKey();

    ConnectionManager *connection_ = nullptr;
    std::unique_ptr<TrustStore> trust_;
    std::unique_ptr<HostIdentity> identity_;   // mbedtls 缺失时为 nullptr
    QFileSystemWatcher watcher_;
    QString identityState_ = QStringLiteral("unavailable");
    bool uidKeyReady_ = false;
};
