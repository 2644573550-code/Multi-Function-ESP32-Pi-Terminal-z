#pragma once
#include <QObject>
#include <QVariantList>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QSharedPointer>
#include <QProcess>
#include <QTimer>
#include "ModuleManager.h"
#include "ModuleCardModel.h"
#include "PageProvider.h"
#include "SessionStateMachine.h"
#include "SlotRouter.h"
#include "PackageReceiver.h"
#include "HostEventDetector.h"
#include "BridgeConnectionMonitor.h"
#include "TransportPolicy.h"
#include "LaneGate.h"
#include "transport/PogoControlTransport.h"
#include "transport/BleTransport.h"
#include "transport/HotspotLanTransport.h"

class ConnectionManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cards READ cards NOTIFY cardsChanged)
    Q_PROPERTY(QString status READ status NOTIFY cardsChanged)
    Q_PROPERTY(ModuleCardModel *cardModel READ cardModel CONSTANT)
    Q_PROPERTY(PageProvider *pageProvider READ pageProvider CONSTANT)
    Q_PROPERTY(bool bridgeConnected READ bridgeConnected NOTIFY bridgeConnectionChanged)
    Q_PROPERTY(bool bridgeMonitoring READ bridgeMonitoring NOTIFY bridgeConnectionChanged)
public:
    explicit ConnectionManager(QObject *parent = nullptr);
    ~ConnectionManager() override;
    bool configure(const QString &path);
    bool start(const QString &device);
    bool bridgeConnected() const { return bridgeMonitor_.connected(); }
    bool bridgeMonitoring() const { return bridgeMonitor_.enabled(); }
    QVariantList cards() const;
    // 物理槽位快照（T-05 界面接点）。只读：槽位是"可空的位置属性"，
    // 业务身份仍由 cards() 表达。未绑定的槽也返回一条，bound=false。
    Q_INVOKABLE QVariantList slotStatus() const;
    // 槽位判定计数：accepted / rejected / lastRejection。
    // 用于回答「刷卡没反应是不是被当成迟到事件丢了」。
    Q_INVOKABLE QVariantMap slotStats() const;
    QString status() const { return status_; }
    ModuleCardModel *cardModel() { return &cards_; }
    PageProvider *pageProvider() { return &pages_; }
    Q_INVOKABLE void attach(const QJsonObject &event);
    Q_INVOKABLE void disconnectModule(const QString &instance);
    Q_INVOKABLE void releasePage(const QString &instance);
    Q_INVOKABLE void action(const QString &instance, const QString &name);
    Q_INVOKABLE void retry(const QString &instance);
    Q_INVOKABLE void trust(const QString &instance, bool always);

    // §0.8.5：业务数据必须挂在当前登录用户上。由 TerminalApplication 在
    // 登录/切用户/退出时调用；空字符串表示无人登录（业务数据全部拒绝）。
    void setActiveUserId(const QString &userId);
    QString activeUserId() const { return activeUserId_; }
signals:
    void cardsChanged();
    void pageMustClose(QString instance);
    void announcement(QString text);
    void trustRequired(QString instance, QString title, QStringList permissions);
    void deviceAttached(QString instance, int moduleId);
    void deviceDetached(QString instance);
    void dataLost(QString instance);
    void dataRestored(QString instance);
    void sessionDisconnected(QString instance);
    void hotspotLeaseRequested(QString instance);
    void hotspotLeaseReleased(QString instance);
    void bridgeConnectionChanged();
    void bridgeUnavailable();
public slots:
    void hotspotLeaseReady(const QString &instance, bool ready);
private:
    struct Session {
        int id = 0;
        QString instance, state, message, packagePath;
        QJsonObject profile;
        QJsonObject verifiedManifest;
        QString trustKey;
        SessionStateMachine *machine = nullptr;
        PackageReceiver *receiver = nullptr;
        QString incoming;
        HotspotLanTransport *lan = nullptr;
        BleTransport *ble = nullptr;
        // §0.8.5：每个 attach 一个 lane 门控实例（切 lane 必须重新认证、最多 3 次）。
        LaneGate *laneGate = nullptr;
        QProcess *installer = nullptr;
        quint32 tx = 0, rx = 0;
        qint64 lastSeen = 0, lostAt = 0, retryAt = 0;
        int progress = 0;
        bool contact = true, ready = false, loaded = false, stopping = false, bleConnected = false, lanConnected = false;
        bool hotspotPending = false, hotspotLease = false, packageNeedsHotspot = false;
        bool firstDataLogged = false;
        quint8 activeTransport = 0;
        quint8 candidateTransport = 0;
        bool releasing = false;
        bool closePending = false;
        bool awaitingPeerClose = false;
        int closeRetries = 0;
        qint64 closeDeadline = 0;
    };
    ModuleManager modules_;
    ModuleCardModel cards_;
    PageProvider pages_;
    PogoControlTransport pogo_;
    // 物理槽位路由（T-05）。仅在事件带齐 bridge_id/slot_id/binding_generation/
    // device_id 时启用；旧格式事件完全走原路径，行为不变。
    SlotRouter slotRouter_;
    BridgeConnectionMonitor bridgeMonitor_;
    HostEventDetector hostEvents_;
    QTimer tick_;
    QHash<QString, QSharedPointer<Session>> sessions_;
    QSet<QString> suppressedInstances_;
    QJsonObject profiles_, config_;
    QString status_ = QStringLiteral("等待模块");
    QString activeUserId_;
    void state(const QSharedPointer<Session> &, const QString &, const QString &);
    void connectData(const QSharedPointer<Session> &);
    void requestHotspot(const QSharedPointer<Session> &, bool forPackage);
    void releaseHotspot(const QSharedPointer<Session> &);
    void receive(const QString &, quint8, const QByteArray &);
    bool send(const QSharedPointer<Session> &, const QString &, const QJsonObject & = {}, quint8 transport = 0);
    void lost(const QString &, const QString &);
    void cancelDataPackageTransfer(const QSharedPointer<Session> &);
    void install(const QSharedPointer<Session> &);
    void loadVerified(const QSharedPointer<Session> &);
    void stop(const QSharedPointer<Session> &, const QString &);
    void finishRelease(const QSharedPointer<Session> &);
    void sendCloseRequest(const QSharedPointer<Session> &);
    void transportLost(const QSharedPointer<Session> &, quint8 transport, const QString &reason);
};
