#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantMap>

class QQmlEngine;
class QQmlComponent;
class QQuickItem;

// 模块页沙箱（§0.7 / Z-7）。
//
// 现状问题：Main.qml:675 用 moduleLoader.setSource() 把模块页加载进**宿主同一个
// QQmlEngine**，根 context 里的 connectionManager / systemTools / musicService /
// desktopShell 全部可见，T-13 的「QML 只见用户选择结果」无法验收。
//
// 做法：每个模块页用独立 QQmlEngine + 独立 QQmlContext，只注入
//   sessionModel（该模块自己的数据）
//   userContext（只读 activeUserId / locked / isAdmin）
//   主题与 i18n 的必要值
// 禁止注入任何数据库/密钥/宿主服务对象。
class ModulePageHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *page READ page NOTIFY pageChanged)
    Q_PROPERTY(bool hasPage READ hasPage NOTIFY pageChanged)
    // 页面加载失败的原因（已本地化前的原始错误串）；成功时为空。
    Q_PROPERTY(QString lastError READ lastError NOTIFY pageChanged)

public:
    explicit ModulePageHost(QObject *parent = nullptr);
    ~ModulePageHost() override;

    // 只读用户上下文，不含任何密钥或 DB 句柄。
    struct UserContext {
        QString activeUserId;
        bool locked = false;
        bool isAdmin = false;
    };

    // 加载模块页。source 为空则卸载。
    // sessionModel 必须是宿主拥有的对象，生命周期与本次加载一致。
    Q_INVOKABLE bool load(const QUrl &source, QObject *sessionModel, const QVariantMap &theme);
    // 注入模块的数据接口（§6.1：QML 不持有 DB/SQL，只能用语义化接口）。
    // api 的生命周期由宿主管理；为 nullptr 时模块页拿不到数据接口。
    void setModuleDataApi(QObject *api);
    Q_INVOKABLE void unload();
    Q_INVOKABLE void updateUserContext(const UserContext &context);

    // 页面根对象由本类的独立引擎创建，不能放进宿主的 Loader。
    // 宿主提供一个 QQuickItem 容器，页面以子项形式渲染进去。
    Q_INVOKABLE void setContainer(QObject *container);

    // §8.4：模块页只能 import 白名单里的模块。
    // 报告要求"禁止模块任意加载 QML/JavaScript"；P0 的过渡形态是允许
    // 模块自带页面（这是产品特色），但**不允许**它 import 能访问
    // 文件系统/网络/进程的模块。拦截在 QQmlEngine 的 import 层做。
    static QStringList allowedImports();

    QObject *page() const { return page_; }
    bool hasPage() const { return page_ != nullptr; }
    QString lastError() const { return lastError_; }

    // 测试用：确认独立引擎里看不到宿主对象。
    Q_INVOKABLE bool hasHostObject(const QString &name) const;

signals:
    void pageChanged();

private:
    QQmlEngine *engine_ = nullptr;
    QQmlComponent *component_ = nullptr;
    QObject *page_ = nullptr;
    QQuickItem *container_ = nullptr;
    QObject *moduleDataApi_ = nullptr;
    QString lastError_;
    UserContext userContext_;
};
