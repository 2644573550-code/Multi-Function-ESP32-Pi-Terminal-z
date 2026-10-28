#include "core/SecurityStatus.h"

#include "core/ConnectionManager.h"
#include "protocol/uid_index.h"
#include "security/TrustStore.h"
#include "security/uid_index_key.h"

#if defined(TERMINAL_HAVE_MBEDTLS)
#include "security/HostIdentity.h"
#endif

#include <QDir>
#include <QFileInfo>

SecurityStatus::SecurityStatus(ConnectionManager *connection,
                               const QString &trustPathOverride,
                               const QString &identityPathOverride, QObject *parent)
    : QObject(parent)
    , connection_(connection)
    , trust_(std::make_unique<TrustStore>(trustPathOverride))
{
#if defined(TERMINAL_HAVE_MBEDTLS)
    identity_ = std::make_unique<HostIdentity>(identityPathOverride);
#else
    Q_UNUSED(identityPathOverride);
#endif

    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // QSaveFile 是"写临时文件再 rename"，rename 会让原有 watch 失效，
        // 所以每次事件后都要重新登记一次。
        if (path == trustStorePath()) reloadTrust();
#if defined(TERMINAL_HAVE_MBEDTLS)
        if (identity_ && path == identityPath()) reloadIdentity();
#endif
        watchFile(path);
    });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        // 目录变了说明文件被创建/替换：重新登记并全部重读。
        watchFile(trustStorePath());
#if defined(TERMINAL_HAVE_MBEDTLS)
        if (identity_) watchFile(identityPath());
#endif
        refresh();
    });

    watchFile(trustStorePath());
#if defined(TERMINAL_HAVE_MBEDTLS)
    if (identity_) watchFile(identityPath());
#endif

    reloadTrust();
    reloadIdentity();
    reloadUidKey();
}

SecurityStatus::~SecurityStatus() = default;

void SecurityStatus::watchFile(const QString &path)
{
    // 文件不存在时盯它的目录，这样"第一次写入"也能被看到。
    // 只盯文件本身的话，新机器上永远收不到第一个事件。
    if (path.isEmpty()) return;
    const QFileInfo info(path);
    if (info.exists() && !watcher_.files().contains(path)) watcher_.addPath(path);
    const QString dir = info.absolutePath();
    if (!dir.isEmpty() && QDir(dir).exists() && !watcher_.directories().contains(dir)) {
        watcher_.addPath(dir);
    }
}

void SecurityStatus::reloadTrust()
{
    if (!trust_) return;
    trust_->reload(nullptr);
    emit trustChanged();
}

void SecurityStatus::reloadIdentity()
{
#if defined(TERMINAL_HAVE_MBEDTLS)
    if (!identity_) return;
    if (!identity_->exists()) {
        identity_->reset();
        identityState_ = QStringLiteral("missing");
        emit identityChanged();
        return;
    }
    QString error;
    if (identity_->load(&error)) {
        identityState_ = QStringLiteral("ready");
    } else {
        identity_->reset();
        identityState_ = QStringLiteral("corrupt");
    }
    emit identityChanged();
#else
    identityState_ = QStringLiteral("unavailable");
#endif
}

void SecurityStatus::reloadUidKey()
{
    QByteArray key;
    const bool ready = uidIndexKey(&key);
    key.fill('\0');   // 密钥不留在栈上
    if (ready != uidKeyReady_) {
        uidKeyReady_ = ready;
        emit uidKeyChanged();
    }
}

void SecurityStatus::refresh()
{
    reloadTrust();
    reloadIdentity();
    reloadUidKey();
}

QVariantList SecurityStatus::trustRecords() const
{
    QVariantList list;
    if (!trust_) return list;
    const QVector<TrustStore::Record> records = trust_->all();
    list.reserve(records.size());
    for (const TrustStore::Record &record : records) {
        list.append(QVariantMap{
            {"bridgeId", record.bridgeId},
            {"fingerprintHex", QString::fromLatin1(record.fingerprint.toHex())},
            {"keyId", int(record.keyId())},
            {"keyIdText", TrustStore::keyIdText(record.keyId())},
            {"enrolledAt", record.enrolledAt},
            {"revokedAt", record.revokedAt},
            {"active", record.active()},
        });
    }
    return list;
}

bool SecurityStatus::trustStoreHealthy() const
{
    return trust_ ? trust_->healthy() : false;
}

QString SecurityStatus::trustStorePath() const
{
    return trust_ ? trust_->path() : QString();
}

QString SecurityStatus::identityFingerprint() const
{
#if defined(TERMINAL_HAVE_MBEDTLS)
    if (identity_ && identity_->hasKey()) {
        return QString::fromLatin1(identity_->fingerprint().toHex());
    }
#endif
    return {};
}

int SecurityStatus::identityKeyId() const
{
#if defined(TERMINAL_HAVE_MBEDTLS)
    if (identity_ && identity_->hasKey()) return int(identity_->keyId());
#endif
    return 0;
}

QString SecurityStatus::identityState() const
{
    return identityState_;
}

QString SecurityStatus::identityPath() const
{
#if defined(TERMINAL_HAVE_MBEDTLS)
    if (identity_) return identity_->path();
#endif
    return {};
}

bool SecurityStatus::uidKeyReady() const
{
    return uidKeyReady_;
}

QVariantList SecurityStatus::slotSnapshot() const
{
    // 槽位状态在 ConnectionManager 里；本类不复制、不缓存，
    // 避免出现"接点显示已绑定、实际会话已释放"的不一致。
    return connection_ ? connection_->slotStatus() : QVariantList{};
}
