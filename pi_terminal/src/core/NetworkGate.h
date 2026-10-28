#pragma once

#include <QHash>
#include <QObject>
#include <QString>

// 联网总开关与有限网络（报告 §9.3）。
//
// 报告原文拆成几件事，这里逐条对应：
//   1. 总开关只控**外网**，不关闭本地 BLE / Pogo / 受限局域连接
//   2. 浏览器、音乐、更新各有**分功能开关**；关闭时取消请求、阻止新建外网连接
//   3. 状态拆三态：**接口就绪 / 局域模块链路 / 互联网可达**，
//      不能因为"接口有 IP"就判定互联网可用
//   4. 离线健康功能照常工作；**无待上传队列**
//   5. 有限超时、有上限重试、用户主动重试、流量/缓存上限
//   6. **不因网络恢复自动下载大包**
//
// 本类只做策略判定与状态，不直接发请求；调用方在发起前问 allow()。
class NetworkGate : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)

    // §9.3 第 3 条：三态互不替代。
    Q_PROPERTY(bool interfaceReady READ interfaceReady NOTIFY stateChanged)
    Q_PROPERTY(bool moduleLinkReady READ moduleLinkReady NOTIFY stateChanged)
    Q_PROPERTY(bool internetReachable READ internetReachable NOTIFY stateChanged)

    // 分功能开关（§9.3 第 2 条）。
    Q_PROPERTY(bool browserAllowed READ browserAllowed NOTIFY changed)
    Q_PROPERTY(bool musicAllowed READ musicAllowed NOTIFY changed)
    Q_PROPERTY(bool updatesAllowed READ updatesAllowed NOTIFY changed)

    // 当前进行中的外网请求数（关闭时据此判断是否已静默）。
    Q_PROPERTY(int inFlight READ inFlight NOTIFY changed)

public:
    // 外网用途。分功能开关按用途判定。
    enum class Purpose {
        Browser,
        Music,
        Updates,
        TimeSync,      // 校时：属于外网，但离线功能照常
        Other,
    };
    Q_ENUM(Purpose)

    explicit NetworkGate(QObject *parent = nullptr) : QObject(parent) {}

    bool enabled() const { return enabled_; }
    QString reason() const { return reason_; }

    bool interfaceReady() const { return interfaceReady_; }
    bool moduleLinkReady() const { return moduleLinkReady_; }
    bool internetReachable() const { return internetReachable_; }

    bool browserAllowed() const { return browserAllowed_; }
    bool musicAllowed() const { return musicAllowed_; }
    bool updatesAllowed() const { return updatesAllowed_; }

    int inFlight() const { return inFlight_; }

    // 总开关。关闭时立刻丢弃进行中的请求（§9.3 第 2 条）。
    void setEnabled(bool enabled);
    Q_INVOKABLE void setEnabledFromUi(bool enabled) { setEnabled(enabled); }

    // 分功能开关：只有在总开关打开时才有效。
    void setBrowserAllowed(bool allowed);
    void setMusicAllowed(bool allowed);
    void setUpdatesAllowed(bool allowed);

    // 三态更新：由网络监视/模块链路分别驱动，不互相推导。
    void setInterfaceReady(bool ready);
    void setModuleLinkReady(bool ready);
    void setInternetReachable(bool reachable);

    // 请求放行。deny 时 reason() 给出可读原因。
    bool allow(Purpose purpose);
    // 便捷重载：用字符串标识（供 QML / 旧调用方）。
    bool allow(const QString &purpose);

    // 请求生命周期：放行成功后调用 begin()，结束调用 end()。
    // 关闭总开关时 end() 全部进行中的请求，inFlight 归零。
    void beginRequest();
    void endRequest();

signals:
    void changed();
    void stateChanged();

private:
    void clearReason();

    bool enabled_ = false;
    QString reason_;

    bool interfaceReady_ = false;
    bool moduleLinkReady_ = false;
    bool internetReachable_ = false;

    bool browserAllowed_ = true;
    bool musicAllowed_ = true;
    bool updatesAllowed_ = false;   // 更新下载默认关闭（§9.3 第 6 条）

    int inFlight_ = 0;
};
