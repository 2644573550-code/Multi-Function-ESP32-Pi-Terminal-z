#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

class AuthService;

// 单一 activeUserId；切用户/退出立即销毁旧会话能力（服务端执行，不只是隐藏 UI）。
//
// 本类不做加解密，只持有"当前谁能用哪个 token"的状态。
// token 是不透明字节，QML 看不到，也不进 QSettings。
class SessionManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString activeUserId READ activeUserId NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)

public:
    explicit SessionManager(QObject *parent = nullptr);

    // 认证成功后建立唯一会话。已有会话会先被撤销。
    QString begin(const QString &userId, const QByteArray &dbKey, bool admin);

    // 锁屏：撤销前台能力，但保留原任务追加能力所需的 session 标识。
    void lock();
    void unlock(const QString &wantUserId);
    void logout();

    QString activeUserId() const { return activeUserId_; }
    bool locked() const { return locked_; }

    // 只有当前 token 能通过；旧 token 一律拒绝。
    bool validate(const QString &token) const;
    bool isAdmin() const { return admin_; }

    // 可信侧短期持有；不暴露给 QML。
    QByteArray dbKey() const;

signals:
    void changed();
    // 切用户/退出/撤权：D 侧据此断开传输并销毁派生会话密钥。
    void sessionInvalidated();

private:
    void clear();

    QString activeUserId_;
    QString tokenHex_;   // 与 begin() 返回值同一形态，避免调用侧还去做编码转换
    QByteArray dbKey_;
    bool locked_ = false;
    bool admin_ = false;
};
