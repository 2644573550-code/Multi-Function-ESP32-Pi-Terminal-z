#include "ConnectionManager.h"

#include "LaneGate.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>
#include <QStandardPaths>
#include <QSysInfo>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSettings>
#include <QDir>

static qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

namespace {
// 位置事件的可路由形态（pbr-secure-v1 §4.3）。
// 四项缺一不可：旧版桥板不发这些字段，缺失时保持既有行为、不路由，
// 避免把兼容性做成回归。
struct SlotPosition
{
    QString bridgeId;
    QString deviceId;
    QString generation;
    int slotId = -1;
    bool valid() const
    {
        return slotId >= 0 && !bridgeId.isEmpty() && !generation.isEmpty() && !deviceId.isEmpty();
    }
};

SlotPosition slotPositionOf(const QJsonObject &event)
{
    SlotPosition position;
    const QJsonValue slot = event.value(QStringLiteral("slot_id"));
    if (slot.isDouble()) position.slotId = slot.toInt(-1);
    position.bridgeId = event.value(QStringLiteral("bridge_id")).toString();
    position.deviceId = event.value(QStringLiteral("device_id")).toString();
    position.generation = event.value(QStringLiteral("binding_generation")).toString();
    return position;
}
} // namespace

ConnectionManager::ConnectionManager(QObject *parent) : QObject(parent), modules_(this), pogo_(this) {
    connect(this, &ConnectionManager::cardsChanged, this, [this] { cards_.synchronize(cards()); });
    connect(&pogo_, &TransportBase::connected, &hostEvents_, &HostEventDetector::reset);
    connect(&pogo_, &TransportBase::connected, this, [this] { bridgeMonitor_.setConnected(true); });
    connect(&bridgeMonitor_, &BridgeConnectionMonitor::connectionChanged, this, [this](bool) { emit bridgeConnectionChanged(); });
    connect(&bridgeMonitor_, &BridgeConnectionMonitor::unavailable, this, [this] {
        handleBridgeUnavailable();
    });
    // 桥板重新连上：解除位置 unknown 并允许新的物理插入判定。
    connect(&bridgeMonitor_, &BridgeConnectionMonitor::connectionChanged, this,
            [this](bool connected) {
        if (connected) handleBridgeRestored();
    });
    connect(&pogo_, &TransportBase::controlEvent, &hostEvents_, &HostEventDetector::consume);
    connect(&hostEvents_, &HostEventDetector::detected, this, [this](const QJsonObject &event) {
        // T-08：RC522 是桥板级事件，不进入 attach 路径。
        if (event.value("event").toString() == QLatin1String("rfid_card")) {
            if (!positionOpsAllowed()) {
                // §10.3：桥板失联时位置未知，禁止新的刷卡。
                qWarning().noquote() << "rfid_card dropped: bridge unavailable";
                return;
            }
            // raw UID 只经信号传递一次，接收方必须立即派生并丢弃。
            emit cardPresented(event.value("uid").toString(),
                               event.value("uid_len").toInt(-1));
            return;
        }
        attach(event);
    });
    connect(&pogo_, &TransportBase::disconnected, this, [this](const auto &reason) {
        bridgeMonitor_.setConnected(false);
        status_ = "控制桥断开：" + reason;
        emit cardsChanged();
    });
    connect(&pogo_, &TransportBase::error, this, [this](const auto &s) { status_ = s; emit cardsChanged(); });
    connect(&modules_, &ModuleManager::error, this, [this](const auto &id, const auto &m) {
        auto s = sessions_.value(id); if (s) state(s, "error", m);
    });
    connect(&modules_, &ModuleManager::event, this, [this](const auto &id, const auto &type, const auto &json) {
        auto s = sessions_.value(id); if (s && !s->stopping) {
            const QJsonObject payload = QJsonDocument::fromJson(json.toUtf8()).object();

            // 报告 §6.1：模块只发原始观测，历史写进当前用户的加密库。
            // 模块自己不再写明文 JSON；这里拿不到 DB 就不写，不伪造归属。
            if (type == QLatin1String("sample")) {
                const QJsonValue temperature = payload.value(QStringLiteral("temperature_C"));
                if (temperature.isDouble() && observationSink)
                    observationSink(id, temperature.toDouble());
            }

            pages_.update(id, payload.toVariantMap());
            s->message = json.left(2048); emit cardsChanged();
        }
    });
    modules_.sender = [this](const QString &id, quint8 t, const QByteArray &data) {
        auto s = sessions_.value(id);
        return s && s->loaded && s->ready && !s->stopping && send(s, "data", {{"payload", QString::fromLatin1(data.toBase64())}}, t);
    };
    modules_.controlSender = [this](const QString &id, quint8 cmd, const QByteArray &payload, quint16 *seq) {
        auto s = sessions_.value(id); if (!s || s->stopping) return false;
        if (!payload.isEmpty()) return false;
        QString command;
        if (cmd == PI_BRIDGE_CONTROL_PING) command = "ping";
        else if (cmd == PI_BRIDGE_CONTROL_STATUS) command = "status";
        else return false;
        quint32 bridgeSequence = 0;
        const bool sent = pogo_.sendCommand({{"command", command}}, &bridgeSequence);
        if (sent && seq) *seq = quint16(bridgeSequence);
        return sent;
    };
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, [this] {
        for (auto s : sessions_) {
            if (s->stopping) continue;
            if (s->ready) {
                if (s->closePending && now() >= s->closeDeadline) {
                    if (s->awaitingPeerClose) { stop(s, "等待模块关闭确认超时，已安全关闭本地会话"); continue; }
                    if (s->closeRetries >= 3) { stop(s, "断开确认超时，已安全关闭本地会话"); continue; }
                    ++s->closeRetries;
                    sendCloseRequest(s);
                    continue;
                }
                if (now() - s->lastSeen > 6000) lost(s->instance, "数据超时，正在重连");
                else if (!s->closePending) send(s, "ping");
            } else if (s->lostAt && now() - s->lostAt > 30000) stop(s, "重连超时");
            else if (s->retryAt && now() >= s->retryAt) { s->retryAt = now() + 3000; connectData(s); }
        }
    });
    tick_.start();
}
ConnectionManager::~ConnectionManager() {
    tick_.stop();
    for (auto s : sessions_) {
        s->stopping = true;
        releaseHotspot(s);
        if (s->receiver) s->receiver->cancel();
        if (s->installer) { s->installer->disconnect(this); s->installer->kill(); s->installer->waitForFinished(2000); }
        if (s->lan) s->lan->close();
        if (s->ble) s->ble->close();
        modules_.unload(s->instance);
    }
}
bool ConnectionManager::configure(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) { status_ = f.errorString(); return false; }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) { status_ = "配置 JSON 无效: " + error.errorString(); return false; }
    config_ = doc.object(); profiles_ = config_.value("modules").toObject();
    if (profiles_.isEmpty()) { status_ = "配置缺少 modules"; return false; }
    const QDir base = QFileInfo(path).absoluteDir();
    for (const auto *key : {"public_key", "package_tool", "cache"}) {
        const auto value = config_.value(key).toString();
        if (!value.isEmpty() && QDir::isRelativePath(value)) config_[key] = base.absoluteFilePath(value);
    }
    return true;
}
bool ConnectionManager::start(const QString &device) {
    bridgeMonitor_.enable(); emit bridgeConnectionChanged();
    return pogo_.open(device);
}
QVariantList ConnectionManager::slotStatus() const {
    QVariantList list;
    for (int slot = 0; slot < 2; ++slot) {
        const SlotRouter::Binding binding = slotRouter_.binding(slot);
        QVariantMap entry{{"slot", slot}, {"bound", binding.valid()}};
        if (binding.valid()) {
            entry.insert("bridgeId", binding.bridgeId);
            entry.insert("deviceId", binding.deviceId);
            entry.insert("generation", binding.generation);
            entry.insert("instance", binding.instance);
            entry.insert("boundAt", binding.boundAt);
            entry.insert("bridgeOnline", slotRouter_.bridgeOnline(binding.bridgeId));
        }
        list.append(entry);
    }
    return list;
}

QVariantMap ConnectionManager::slotStats() const {
    return QVariantMap{
        {"accepted", QVariant::fromValue(slotRouter_.acceptedCount())},
        {"rejected", QVariant::fromValue(slotRouter_.rejectedCount())},
        {"lastRejection", slotRouter_.lastRejection()},
    };
}

QVariantList ConnectionManager::cards() const {
    QVariantList list;
    for (auto s : sessions_) {
        list.append(QVariantMap{{"instance", s->instance}, {"moduleId", s->id}, {"state", s->state},
            {"message", s->message}, {"progress", s->progress}, {"contact", s->contact},
            {"ready", s->ready && s->loaded && !s->stopping}, {"page", modules_.page(s->instance)},
            {"title", modules_.manifest(s->instance).value("name").toString(QString("模块 %1").arg(s->id))},
            {"icon", modules_.manifest(s->instance).value("icon").toString()},
            {"actions", modules_.manifest(s->instance).value("actions").toArray().toVariantList()}});
    }
    return list;
}
void ConnectionManager::state(const QSharedPointer<Session> &s, const QString &value, const QString &text) {
    qInfo().noquote() << "Module session state:" << s->id << s->instance << value << text;
    s->machine->request(value, text);
}
void ConnectionManager::attach(const QJsonObject &e) {
    if (!HostEventDetector::valid(e)) return;
    const auto instance = e.value("module_instance").toString();
    const auto type = e.value("event").toString();
    if (type == "attach") {
        const auto moduleConfig = e.value("module_config").toObject();
        qInfo().noquote() << "Module-provided config:" << instance
                          << "schema=" << moduleConfig.value("schema").toString()
                          << "mode=" << moduleConfig.value("transport_mode").toString()
                          << "package=" << moduleConfig.value("package_transport").toString();
    }
    if (type == "detach") {
        // 位置事件先过槽位路由：迟到的 detach 不得解掉新设备的绑定，
        // 也不得把新会话的 contact 清掉。旧格式事件不带这些字段，跳过。
        const SlotPosition position = slotPositionOf(e);
        if (position.valid()) {
            const auto decision = slotRouter_.release(position.bridgeId, position.slotId,
                                                      position.generation, now());
            if (decision.verdict != SlotRouter::Verdict::Accepted) {
                qWarning().noquote() << "Slot detach rejected:" << instance
                                     << SlotRouter::verdictName(decision.verdict) << decision.detail;
                return;
            }
        }
        suppressedInstances_.remove(instance);
        auto s = sessions_.value(instance);
        if (s && e.value("module_id").toInt(-1) == s->id) {
            if (s->contact) emit deviceDetached(instance);
            s->contact = false;
            if (s->loaded && s->ready) state(s, "contact_detached", "触点已拔出，数据会话继续");
            emit cardsChanged();
        }
        return;
    }
    if (suppressedInstances_.contains(instance)) return;
    const double numericId = e.value("module_id").toDouble(-1);
    if (type != "attach" || numericId < 1 || numericId > 255 || numericId != int(numericId) ||
        !QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(instance).hasMatch()) return;
    {
        // 双槽路由：同一槽换设备必换 binding_generation，旧代次的迟到 attach
        // 在这里被拒，不会建立/覆盖会话。重复上报同代次是幂等的。
        const SlotPosition position = slotPositionOf(e);
        if (position.valid()) {
            const auto decision = slotRouter_.bind(position.bridgeId, position.slotId,
                                                   position.deviceId, position.generation,
                                                   instance, now());
            if (decision.verdict != SlotRouter::Verdict::Accepted) {
                qWarning().noquote() << "Slot attach rejected:" << instance
                                     << SlotRouter::verdictName(decision.verdict) << decision.detail;
                return;
            }
        }
    }
    if (sessions_.contains(instance)) {
        auto s = sessions_[instance];
        if (s->id != int(numericId) || s->stopping) return;
        QJsonObject updatedProfile = s->profile;
        if (TransportPolicy::applyModuleConfig(&updatedProfile, e.value("module_config").toObject()) &&
            (updatedProfile.value("transport_mode") != s->profile.value("transport_mode") ||
             updatedProfile.value("package_transport") != s->profile.value("package_transport"))) {
            qInfo().noquote() << "Module transport config changed; replacing session:" << instance
                              << s->profile.value("transport_mode").toString() << "->"
                              << updatedProfile.value("transport_mode").toString();
            s->contact = true;
            stop(s, "模块传输配置已更新，正在按新配置重建会话");
            return;
        }
        if (s->contact) return; // Periodic live-state announcements are idempotent.
        s->contact = true;
        if (s->loaded && s->ready) state(s, "ready", "模块触点已恢复");
        emit cardsChanged(); return;
    }
    if (sessions_.size() >= 8) { status_ = "模块数量已达上限"; emit cardsChanged(); return; }
    auto s = QSharedPointer<Session>::create(); s->id = int(numericId); s->instance = instance;
    s->profile = profiles_.value(QString::number(s->id)).toObject();
    const auto moduleConfig = e.value("module_config").toObject();
    // Module-provided configuration is authoritative for data/package lanes.
    // The host profile supplies only local credentials, BLE discovery identity,
    // trust key, and network endpoint details; it cannot override the module.
    const bool moduleConfigValid = TransportPolicy::applyModuleConfig(&s->profile, moduleConfig);
    sessions_.insert(instance, s);
    QTimer::singleShot(0, this, [this, instance] {
        auto session = sessions_.value(instance);
        if (session && !session->stopping) emit deviceAttached(instance, session->id);
    });
    s->machine = new SessionStateMachine(this);
    connect(s->machine, &SessionStateMachine::changed, this, [this, instance](const QString &value, const QString &text) {
        auto session = sessions_.value(instance); if (!session) return;
        session->state = value; session->message = text; status_ = text;
        emit cardsChanged(); emit announcement(text);
        if (value == "unloading" && session->releasing) {
            modules_.unload(instance); pages_.remove(instance); session->loaded = false;
            state(session, "closed", "模块已断开");
        } else if (value == "closed" && session->releasing) finishRelease(session);
    });
    state(s, "attached", "检测到模块，Host 已完成唤醒确认");
    if (!moduleConfigValid) {
        state(s, "error", moduleConfig.isEmpty() ? "缺少模块提供的传输配置文件" : "模块传输配置文件无效或版本不支持");
        return;
    }
    if (s->profile.isEmpty() || s->profile.value("token").toString().isEmpty()) { state(s, "error", "缺少预置模块配置/认证信息"); return; }
    const auto policy = TransportPolicy::fromProfile(s->profile);
    if (!policy.valid()) { state(s, "error", "未知传输方式或包传输配置"); return; }
    if (!policy.packageUsesDataLane() && s->profile.value("package_url").toString().isEmpty()) {
        state(s, "error", "HTTP插件包模式缺少package_url"); return;
    }
    if (policy.allowsLan()) {
        s->lan = new HotspotLanTransport(this);
        connect(s->lan, &TransportBase::connected, this, [this, instance] {
            auto s = sessions_.value(instance); if (!s || s->stopping) return; s->lanConnected = true;
            // TLS 指纹已比对通过（HotspotLanTransport 只在 pinnedVerified 后 emit connected）。
            if (s->laneGate) s->laneGate->markAuthenticated(LaneGate::Lane::Lan);
            send(s, "session_open", {{"token", s->profile.value("token")}, {"transport_choice", "hotspot_lan"}}, 2);
        });
        connect(s->lan, &TransportBase::payloadReceived, this, [this, instance](quint8 t, const auto &p) { receive(instance, t, p); });
        connect(s->lan, &TransportBase::disconnected, this, [this, instance](const auto &m) {
            auto s = sessions_.value(instance); if (s) transportLost(s, 2, m);
        });
        connect(s->lan, &TransportBase::error, this, [this, instance](const auto &m) {
            auto s = sessions_.value(instance); if (s) transportLost(s, 2, m);
        });
    }
    if (policy.allowsBle()) {
        s->ble = new BleTransport(this);
        connect(s->ble, &TransportBase::connected, this, [this, instance] {
            auto s = sessions_.value(instance); if (!s || s->stopping) return; s->bleConnected = true;
            // 挑战-响应已通过（BleTransport 只在 authenticated 后 emit connected）。
            if (s->laneGate) s->laneGate->markAuthenticated(LaneGate::Lane::Ble);
            send(s, "session_open", {{"token", s->profile.value("token")}, {"transport_choice", "ble"}}, 1);
        });
        connect(s->ble, &TransportBase::payloadReceived, this, [this, instance](quint8 t, const auto &p) { receive(instance, t, p); });
        connect(s->ble, &TransportBase::error, this, [this, instance](const auto &m) { auto s = sessions_.value(instance); if (s) transportLost(s, 1, m); });
        connect(s->ble, &TransportBase::disconnected, this, [this, instance](const auto &m) { auto s = sessions_.value(instance); if (s) transportLost(s, 1, m); });
    }
    s->lostAt = 0; s->retryAt = 0;

    // §0.8.5：本次 attach 的 lane 门控。绑定当前用户；未登录时为匿名会话，
    // 业务数据在 requestSwitch 时会被 RejectedNoSession/RejectedUnbound 拦下。
    if (!s->laneGate) {
        s->laneGate = new LaneGate();
        const QString gateSession = s->instance;
        connect(s->laneGate, &LaneGate::invalidated, this, [this, gateSession](const QString &reason) {
            auto session = sessions_.value(gateSession);
            if (!session) return;
            qInfo().noquote() << "lane gate invalidated:" << gateSession << reason;
        });
        connect(s->laneGate, &LaneGate::switchRequired, this, [this, gateSession](int lane) {
            auto session = sessions_.value(gateSession);
            if (!session) return;
            // 切换时丢弃未完成分片，不重传；由连接重跑一次 connectData。
            qInfo().noquote() << "lane switch required:" << gateSession
                              << (lane == int(LaneGate::Lane::Lan) ? "hotspot_lan" : "ble");
        });
    }
    s->laneGate->beginAttach(activeUserId_, s->instance);

    state(s, "connecting", "正在连接模块"); connectData(s);
}
void ConnectionManager::connectData(const QSharedPointer<Session> &s) {
    // Product profiles are mutually exclusive: BLE-only or LAN-only. PBR1
    // control remains independent and never carries business/package data.
    const auto policy = TransportPolicy::fromProfile(s->profile);
    if (!policy.valid()) { state(s, "error", "未知传输方式或包传输配置"); return; }
    if (!s->candidateTransport) s->candidateTransport = policy.initialTransport();
    qInfo().noquote() << "Session handshake candidate:" << s->instance
                      << (s->candidateTransport == 2 ? "hotspot_lan" : "ble");
    if (s->candidateTransport == 2 && s->lan && policy.sessionNeedsHotspot() && !s->hotspotLease) {
        requestHotspot(s, false);
        return;
    }
    if (s->candidateTransport == 2 && s->lan) {
        // §0.8.1：LAN 必须指纹固定。profile 里没有 pin_sha256 就不连——
        // 空指纹等于不校验，宁可连不上也不降级明文。
        const QByteArray pinned =
            QByteArray::fromHex(s->profile.value("pin_sha256").toString().toLatin1());
        if (pinned.size() != 32) {
            state(s, "error", "LAN 缺少已登记的证书指纹（pin_sha256）");
            return;
        }
        const QString sessionName = s->instance;
        connect(s->lan, &HotspotLanTransport::identityRejected, this,
                [this, sessionName](const QString &reason) {
            auto session = sessions_.value(sessionName);
            if (!session || session->stopping) return;
            qWarning().noquote() << "LAN identity rejected:" << sessionName << reason;
            state(session, "error", "IDENTITY_REJECTED");
        }, Qt::UniqueConnection);
        s->lan->connectToModule(QHostAddress(s->profile.value("address").toString()),
                                quint16(s->profile.value("port").toInt()), pinned,
                                QString::number(s->id));
    } else if (s->candidateTransport == 1 && s->ble) {
        QJsonObject bleConfig = s->profile.value("ble").toObject();
        // §0.8.2 应用层认证参数：密钥来自配对下发（T-03），只在内存里传。
        bleConfig.insert("module_id", s->id);
        bleConfig.insert("boot_id", s->profile.value("boot_id").toString());
        bleConfig.insert("session_key", s->profile.value("session_key").toString());
        const QString sessionName = s->instance;
        connect(s->ble, &BleTransport::authenticationFailed, this,
                [this, sessionName](const QString &reason) {
            auto session = sessions_.value(sessionName);
            if (!session || session->stopping) return;
            qWarning().noquote() << "BLE authentication failed:" << sessionName << reason;
            state(session, "error", "TRANSPORT_SECURITY_REQUIRED");
        }, Qt::UniqueConnection);
        s->ble->start(bleConfig);
    }
}
void ConnectionManager::handleBridgeUnavailable() {
    // 报告 §10.3：桥板 USB 失联只使位置变为 unknown 并禁止**新的**
    // 刷卡/绑卡/物理接入确认；已登记用户仍可用本机 PIN 解锁查看、停止或撤权。
    // 已认证且仍有效的无线业务**不因此中断**。
    bridgeUnavailable_ = true;
    // 把当前所有已绑定桥板在 SlotRouter 里标成离线，位置判定转 unknown。
    for (const auto &binding : slotRouter_.bindings()) {
        if (binding.valid()) slotRouter_.setBridgeOnline(binding.bridgeId, false);
    }
    status_ = "控制桥失联：位置未知，禁止新的刷卡/绑卡";
    qWarning().noquote() << "bridge unavailable; position-based operations blocked";
    emit bridgeUnavailable();
    emit bridgeAvailabilityChanged(false);
    emit cardsChanged();
}

void ConnectionManager::handleBridgeRestored() {
    if (!bridgeUnavailable_) return;
    bridgeUnavailable_ = false;
    for (const auto &binding : slotRouter_.bindings()) {
        if (binding.valid()) slotRouter_.setBridgeOnline(binding.bridgeId, true);
    }
    // 恢复后仍须重新认证与核对位置；这里不重建任何旧业务会话。
    status_ = "控制桥已恢复：需重新认证与核对位置";
    qInfo().noquote() << "bridge restored; re-authentication required";
    emit bridgeAvailabilityChanged(true);
    emit cardsChanged();
}

bool ConnectionManager::positionOpsAllowed() const {
    // 桥板在线才允许基于物理位置的新操作。
    return !bridgeUnavailable_;
}

void ConnectionManager::setActiveUserId(const QString &userId) {
    if (activeUserId_ == userId) return;
    activeUserId_ = userId;
    // 切用户/退出：所有已建立的 lane 认证立即作废，业务数据不得继续流入。
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it.value() && it.value()->laneGate)
            it.value()->laneGate->invalidateAll(
                userId.isEmpty() ? QStringLiteral("logged out")
                                 : QStringLiteral("active user changed"));
    }
}

void ConnectionManager::requestHotspot(const QSharedPointer<Session> &s, bool forPackage) {
    if (!s || s->stopping || s->hotspotLease || s->hotspotPending) return;
    s->hotspotPending = true;
    s->packageNeedsHotspot = forPackage;
    state(s, forPackage ? "downloading_package" : "connecting",
          forPackage ? "正在按需启用热点以下载签名插件" : "正在按需启用模块热点");
    emit hotspotLeaseRequested(s->instance);
}
void ConnectionManager::hotspotLeaseReady(const QString &instance, bool ready) {
    auto s = sessions_.value(instance);
    if (!s || !s->hotspotPending || s->stopping) {
        if (ready) emit hotspotLeaseReleased(instance);
        return;
    }
    const bool forPackage = s->packageNeedsHotspot;
    s->hotspotPending = false;
    s->packageNeedsHotspot = false;
    if (!ready) {
        if (forPackage) {
            state(s, "error", "无法启用Pi模块热点，未下载插件包；请检查Wi-Fi热点/profile");
            return;
        }
        state(s, "error", s->candidateTransport == 2
              ? "LAN only 模式需要Pi热点；终端不会在热点失败时改走BLE"
              : "热点HTTP包下载失败；BLE传感器数据会话保持蓝牙，不切换数据链路");
        return;
    }
    s->hotspotLease = true;
    if (forPackage) install(s);
    else connectData(s);
}
void ConnectionManager::releaseHotspot(const QSharedPointer<Session> &s) {
    if (!s || !s->hotspotLease) return;
    s->hotspotLease = false;
    emit hotspotLeaseReleased(s->instance);
}
bool ConnectionManager::send(const QSharedPointer<Session> &s, const QString &type, const QJsonObject &body, quint8 t) {
    QJsonObject e = body; e["event"] = type; e["module_id"] = s->id; e["module_instance"] = s->instance;
    e["seq"] = double(++s->tx); e["version"] = 1;
    const auto p = QJsonDocument(e).toJson(QJsonDocument::Compact);
    if (!t) t = s->activeTransport ? s->activeTransport : (s->lanConnected ? 2 : s->bleConnected ? 1 : s->lan ? 2 : 1);
    if (t == 2) return s->lan && s->lan->sendData(p);
    return t == 1 && s->ble && s->ble->sendData(p);
}
void ConnectionManager::receive(const QString &instance, quint8 t, const QByteArray &p) {
    auto s = sessions_.value(instance); if (!s || s->stopping) return;
    const auto e = QJsonDocument::fromJson(p).object();
    if (e.value("module_id").toDouble(-1) != s->id || e.value("module_instance").toString() != instance || e.value("version").toInt() != 1) return;
    const auto seq = e.value("seq").toDouble(-1);
    if (seq < 1 || seq > 4294967295.0 || seq != quint32(seq)) return;
    const auto type = e.value("event").toString();
    if (type == "session_ready") {
        if (s->stopping || (t == 1 && !s->bleConnected) || (t == 2 && !s->lanConnected)) return;
        if (e.value("token").toString() != s->profile.value("token").toString()) { lost(instance, "模块身份验证失败"); return; }
        const auto selected = e.value("selected_transport").toString();
        if (!selected.isEmpty() && selected != (t == 1 ? "ble" : "hotspot_lan")) {
            lost(instance, "模块连接方式确认不匹配"); return;
        }
        if (s->candidateTransport && s->candidateTransport != t) return;
        if (s->ready && s->activeTransport != t) return;
        if (s->ready) return; // A replay must not reset the receive sequence or reload a plugin.
        s->rx = quint32(seq); s->ready = true; s->lastSeen = now(); s->lostAt = s->retryAt = 0;
        s->activeTransport = t;
        qInfo().noquote() << "Session handshake selected:" << instance << (t == 1 ? "ble" : "hotspot_lan");
        if (!s->loaded && s->packagePath.isEmpty()) install(s);
        else if (s->loaded) { modules_.connected(instance, t); state(s, s->contact ? "ready" : "contact_detached", "模块连接已恢复"); emit dataRestored(instance); }
        else state(s, "awaiting_trust", "等待确认模块权限");
        return;
    }
    if (!s->ready || (s->activeTransport && t != s->activeTransport)) return;
    const quint32 incoming = quint32(seq);
    const quint32 distance = incoming - s->rx;
    if (distance == 0 || distance >= 0x80000000u) return;
    QByteArray decodedPayload;
    if (type == "package_chunk") {
        if (s->receiver && s->receiver->receive(e)) { s->rx = incoming; s->lastSeen = now(); }
        return;
    } else if (type == "data") {
        if (!e.value("payload").isString()) return;
        const auto decoded = QByteArray::fromBase64Encoding(e.value("payload").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.size() > 65536) return;
        decodedPayload = decoded.decoded;
    } else if (type == "module_disconnect_request") {
        if (!e.value("stop_required").isBool() || !e.value("auto_reconnect").isBool()) return;
    } else if (type == "disconnect_ack") {
        if (!e.value("accepted").isBool()) return;
    } else if (type != "pong" && type != "session_close") return;
    s->rx = incoming; s->lastSeen = now();
    if (type == "disconnect_ack") {
        if (!s->closePending || s->awaitingPeerClose) return;
        if (e.value("accepted").toBool()) stop(s, "模块已确认断开");
        else { s->closePending = false; s->closeRetries = 0; state(s, s->contact ? "ready" : "contact_detached", "模块拒绝断开"); }
    } else if (type == "session_close") {
        if (s->closePending && s->awaitingPeerClose) stop(s, "模块已完成断开");
    } else if (type == "module_disconnect_request") {
        const QStringList reasons{"user_button", "low_battery", "fault", "enter_sleep", "reboot", "maintenance"};
        if (!reasons.contains(e.value("reason").toString())) {
            send(s, "disconnect_ack", {{"accepted",false},{"reason","invalid_reason"}}); return;
        }
        const bool policyAllows = s->profile.value("allow_disconnect").toBool(true);
        // A module on a live physical pogo contact is still hard-connected to
        // the host; a radio-side close must not silently discard that wired
        // attachment. Wireless-only sessions (contact=false) may request close.
        const bool accepted = policyAllows && !s->contact;
        if (s->loaded) modules_.disconnectRequest(instance, e);
        // Periodic attach announcements must not undo an explicit disconnect
        // while contact is still present. If DET was already removed, however,
        // the next attach is a new insertion: there will be no further detach
        // to clear a newly inserted suppression entry.
        if (accepted && !e.value("auto_reconnect").toBool() && s->contact)
            suppressedInstances_.insert(instance);
        const QString rejectReason = !policyAllows ? "host_policy" : s->contact ? "physical_contact_active" : "accepted";
        send(s, "disconnect_ack", {{"accepted", accepted}, {"reason", rejectReason}});
        if (accepted) { s->closePending = true; s->awaitingPeerClose = true; s->closeDeadline = now() + 3000; }
    } else if (type == "data" && s->loaded) {
        // §0.8.4：单条业务 payload ≤512 字节，超出直接拒绝（INVALID_MESSAGE）。
        // 不截断、不重试、不降级 —— 交给上层按 INVALID_MESSAGE 处理。
        if (!LaneGate::payloadAcceptable(decodedPayload.size())) {
            state(s, "error", "INVALID_MESSAGE");
            return;
        }
        // §0.8.5：业务数据必须挂在已认证的 lane 与已绑定用户的会话上。
        // 未通过指纹固定 / 挑战-响应的连接一条数据都不放行。
        if (s->laneGate) {
            const auto lane = (t == 2) ? LaneGate::Lane::Lan : LaneGate::Lane::Ble;
            const auto verdict = s->laneGate->requestSwitch(lane);
            if (verdict != LaneGate::Verdict::Accepted) {
                qWarning().noquote() << "business data rejected by lane gate:" << instance
                                     << s->laneGate->lastReason();
                state(s, "error",
                      verdict == LaneGate::Verdict::RejectedNoSession ||
                              verdict == LaneGate::Verdict::RejectedUnbound
                          ? "STORAGE_UNAVAILABLE"
                          : "TRANSPORT_SECURITY_REQUIRED");
                return;
            }
        }
        if (!s->firstDataLogged) {
            qInfo().noquote() << "Module data received: module_id=" << s->id
                              << "instance=" << instance
                              << "transport=" << (t == 1 ? "BLE" : "LAN")
                              << "payload_bytes=" << decodedPayload.size();
            s->firstDataLogged = true;
        }
        modules_.data(instance, t, decodedPayload);
    }
}
void ConnectionManager::install(const QSharedPointer<Session> &s) {
    if (s->installer) return;
    const auto policy = TransportPolicy::fromProfile(s->profile);
    if (!policy.valid()) { state(s, "error", "未知传输方式或包传输配置"); return; }
    if (!policy.packageUsesDataLane() && s->profile.value("package_url").toString().isEmpty()) {
        state(s, "error", "HTTP插件包模式缺少package_url"); return;
    }
    if (policy.packageNeedsHotspot() && !s->hotspotLease) {
        requestHotspot(s, true);
        return;
    }
    const bool fetchPackageOverData = policy.packageUsesDataLane();
    if (fetchPackageOverData && s->incoming.isEmpty()) {
        if (s->receiver) return;
        s->progress = 5; state(s, "downloading_package", "正在通过数据链路接收插件包");
        auto *receiver = s->receiver = new PackageReceiver(this);
        const auto id = s->instance;
        connect(receiver, &PackageReceiver::request, this, [this, id](const QJsonObject &body) { auto s = sessions_.value(id); if (s && !s->stopping) send(s, "package_get", body); });
        connect(receiver, &PackageReceiver::progress, this, [this, id](int percent) { auto s = sessions_.value(id); if (s) { s->progress = percent; emit cardsChanged(); } });
        connect(receiver, &PackageReceiver::verifying, this, [this, id] { auto s = sessions_.value(id); if (s) state(s, "verifying_package", "正在验证发布者签名"); });
        connect(receiver, &PackageReceiver::failed, this, [this, id, receiver](const QString &message) {
            auto s = sessions_.value(id); if (s) { s->receiver = nullptr; state(s, "error", message); } receiver->deleteLater();
        });
        connect(receiver, &PackageReceiver::finished, this, [this, id](const QString &folder) { auto s = sessions_.value(id); if (s && !s->stopping) { s->incoming = folder; install(s); } });
        receiver->start(config_.value("python").toString("python3"), config_.value("package_tool").toString(PI_PACKAGE_TOOL), config_.value("public_key").toString(), s->id);
        return;
    }
    s->progress = 10; state(s, "downloading_package", "正在下载并验证模块功能");
    s->installer = new QProcess(this);
    auto *process = s->installer; const auto id = s->instance;
    connect(process, &QProcess::errorOccurred, this, [this, id, process](auto error) {
        auto s = sessions_.value(id);
        if (error == QProcess::FailedToStart) {
            if (s && s->installer == process) s->installer = nullptr;
            process->deleteLater();
        }
        if (s && !s->stopping) {
            state(s, "error", "插件包验证进程失败: " + process->errorString());
            if (TransportPolicy::fromProfile(s->profile).packageNeedsHotspot()) releaseHotspot(s);
        }
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, id, process](int code, auto exit) {
        process->deleteLater();
        auto s = sessions_.value(id); if (!s) return;
        s->installer = nullptr;
        if (s->receiver) { s->receiver->cancel(); s->receiver->deleteLater(); s->receiver = nullptr; }
        if (s->stopping) return;
        if (code || exit != QProcess::NormalExit) {
            state(s, "error", "插件包验证失败: " + QString::fromUtf8(process->readAllStandardError()).right(1024));
            const auto policy = TransportPolicy::fromProfile(s->profile);
            if (policy.mode == TransportPolicy::Mode::BleOnly && policy.packageNeedsHotspot()) releaseHotspot(s);
            return;
        }
        s->packagePath = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        QFile manifest(s->packagePath + "/manifest.json");
        if (!manifest.open(QIODevice::ReadOnly)) {
            state(s, "error", "无法读取已验证清单");
            if (TransportPolicy::fromProfile(s->profile).packageNeedsHotspot()) releaseHotspot(s);
            return;
        }
        s->verifiedManifest = QJsonDocument::fromJson(manifest.readAll()).object();
        QFile key(config_.value("public_key").toString()); key.open(QIODevice::ReadOnly);
        s->trustKey = QString::fromLatin1(QCryptographicHash::hash(key.readAll() + QJsonDocument(s->verifiedManifest).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        const auto policy = TransportPolicy::fromProfile(s->profile);
        if (policy.mode == TransportPolicy::Mode::BleOnly && policy.packageNeedsHotspot()) releaseHotspot(s);
        QSettings settings;
        if (settings.value("trusted/" + s->trustKey, false).toBool()) loadVerified(s);
        else {
            state(s, "awaiting_trust", "签名验证通过，请确认模块权限");
            QStringList permissions;
            for (const auto &v : s->verifiedManifest.value("permissions").toArray()) permissions.append(v.toString());
            // manifest 的 permissions 只是请求；把请求记录给安全服务，
            // 由授权页展示"请求了什么 / 实际授予了什么"，不自动授予。
            if (requestedCapabilitiesSink) requestedCapabilitiesSink(id.toInt(), permissions);
            emit trustRequired(id, s->verifiedManifest.value("name").toString(), permissions);
        }
    });
    process->start(config_.value("python").toString("python3"), {config_.value("package_tool").toString(PI_PACKAGE_TOOL),
        s->incoming.isEmpty() ? "install" : "install-local", s->incoming.isEmpty() ? s->profile.value("package_url").toString() : s->incoming, config_.value("public_key").toString(),
        config_.value("cache").toString(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/modules"),
        QString::number(s->id), QSysInfo::currentCpuArchitecture()});
    // A just-enabled hotspot can take several seconds to associate the module
    // STA. package_tool performs bounded transient HTTP retries; leave time for
    // that recovery without allowing an unbounded child process.
    QTimer::singleShot(90000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
}
void ConnectionManager::trust(const QString &id, bool always) {
    auto s = sessions_.value(id);
    if (!s || s->stopping || s->loaded || s->packagePath.isEmpty() || s->trustKey.isEmpty()) return;
    if (always) { QSettings settings; settings.setValue("trusted/" + s->trustKey, true); settings.sync(); }
    loadVerified(s);
}
void ConnectionManager::loadVerified(const QSharedPointer<Session> &s) {
    s->progress = 90; state(s, "loading_plugin", "正在准备功能界面");
    pages_.registerPage(s->instance, {});
    auto transport = s->profile; transport["module_instance"] = s->instance;
    if (!modules_.load(s->instance, s->id, s->packagePath, transport)) { pages_.remove(s->instance); return; }
    pages_.registerPage(s->instance, modules_.page(s->instance));
    s->loaded = true; s->progress = 100;
    if (s->ready) { modules_.connected(s->instance, s->activeTransport ? s->activeTransport : (s->lanConnected ? 2 : 1)); state(s, "ready", "模块已连接"); }
    else state(s, "data_lost", "数据连接中断，功能已缓存");
}
bool ConnectionManager::refreshModule(const QString &instance) {
    auto s = sessions_.value(instance);
    if (!s || s->stopping || !s->loaded || s->packagePath.isEmpty()) return false;

    const QString previousVersion = modules_.packageVersion(instance);
    // 先卸页面与插件：顺序不能反，否则新页面会指向已释放的 model。
    pages_.registerPage(instance, {});
    modules_.unload(instance);
    pages_.remove(instance);

    const QJsonObject transport = s->profile;
    if (!modules_.load(instance, s->id, s->packagePath, transport)) {
        state(s, "error", "热更新失败：模块无法重新加载");
        return false;
    }
    pages_.registerPage(instance, modules_.page(instance));
    const QString newVersion = modules_.packageVersion(instance);
    if (s->ready)
        modules_.connected(instance, s->activeTransport ? s->activeTransport
                                                        : (s->lanConnected ? 2 : 1));
    qInfo().noquote() << "module refreshed:" << instance
                      << previousVersion << "->" << newVersion
                      << "page schema" << modules_.pageSchemaVersion(instance);
    emit moduleRefreshed(instance, newVersion);
    return true;
}

void ConnectionManager::retry(const QString &id) {
    auto s = sessions_.value(id); if (!s || s->stopping || s->installer || s->state != "error") return;
    s->packagePath.clear(); s->trustKey.clear(); s->incoming.clear();
    if (s->receiver) { s->receiver->cancel(); s->receiver->deleteLater(); s->receiver = nullptr; }
    if (s->ready) install(s);
    else {
        const auto policy = TransportPolicy::fromProfile(s->profile);
        s->candidateTransport = policy.valid() ? policy.initialTransport() : 0;
        s->lostAt = 0; s->retryAt = 0;
        state(s, "connecting", "正在重试连接"); connectData(s);
    }
}
void ConnectionManager::lost(const QString &id, const QString &reason) {
    auto s = sessions_.value(id); if (!s || s->stopping) return;
    cancelDataPackageTransfer(s);
    if (s->ready) emit dataLost(id);
    s->ready = false; s->activeTransport = 0; s->lanConnected = false;
    if (!s->lostAt) s->lostAt = now();
    s->retryAt = now() + 3000;
    state(s, "data_lost", reason);
}
void ConnectionManager::cancelDataPackageTransfer(const QSharedPointer<Session> &s) {
    if (!s || !s->receiver) return;
    auto *receiver = s->receiver;
    s->receiver = nullptr;
    receiver->cancel();
    receiver->deleteLater();
    s->incoming.clear();
    s->progress = 0;
}
void ConnectionManager::transportLost(const QSharedPointer<Session> &s, quint8 transport, const QString &reason) {
    if (s->stopping) return;
    if (transport == 1) s->bleConnected = false; else s->lanConnected = false;
    if (!s->ready && s->candidateTransport && s->candidateTransport != transport) return;
    if (s->activeTransport != transport && s->ready) return;
    if (s->activeTransport == transport) {
        const bool wasReady = s->ready;
        s->ready = false; s->activeTransport = 0;
        if (wasReady) emit dataLost(s->instance);
        cancelDataPackageTransfer(s);
    }
    lost(s->instance, reason);
}
void ConnectionManager::disconnectModule(const QString &id) {
    auto s = sessions_.value(id); if (!s || s->stopping || s->closePending) return;
    if (!s->ready) { stop(s, "数据链路不可用，关闭本地会话"); return; }
    s->closePending = true; s->closeRetries = 0; sendCloseRequest(s);
}
void ConnectionManager::sendCloseRequest(const QSharedPointer<Session> &s) {
    s->closeDeadline = now() + 1000;
    if (!send(s, "stop") || !send(s, "session_close")) s->closeDeadline = now();
}
void ConnectionManager::stop(const QSharedPointer<Session> &s, const QString &reason) {
    if (s->stopping) return;
    s->stopping = true; s->ready = false;
    // 会话关闭时释放它占用的物理槽，避免槽位绑定泄漏到下一次插入。
    for (int slot = 0; slot < 2; ++slot) {
        const SlotRouter::Binding binding = slotRouter_.binding(slot);
        if (binding.valid() && binding.instance == s->instance)
            slotRouter_.release(binding.bridgeId, slot, binding.generation, now());
    }
    s->closePending = false;
    s->awaitingPeerClose = false;
    if (s->receiver) s->receiver->cancel();
    if (s->installer) s->installer->kill();
    if (s->lan) s->lan->close();
    if (s->ble) s->ble->close();
    state(s, "disconnecting", reason); emit pageMustClose(s->instance);
}
void ConnectionManager::releasePage(const QString &id) {
    auto s = sessions_.value(id); if (!s || !s->stopping || s->releasing) return;
    s->releasing = true; state(s, "unloading", "正在卸载模块");
}
void ConnectionManager::finishRelease(const QSharedPointer<Session> &s) {
    s->machine->deleteLater();
    if (s->receiver) s->receiver->deleteLater();
    if (s->lan) s->lan->deleteLater();
    if (s->ble) s->ble->deleteLater();
    releaseHotspot(s);
    sessions_.remove(s->instance); status_ = "模块已断开"; emit cardsChanged();
    emit sessionDisconnected(s->instance);
}
void ConnectionManager::action(const QString &id, const QString &name) {
    auto s = sessions_.value(id); if (s && s->ready && !s->stopping) modules_.action(id, name);
}
