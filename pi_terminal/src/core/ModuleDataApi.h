#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace security {
class UserDatabase;
}

// 模块 QML 的用户数据接口（报告 §6.1 / §12.2）。
//
// 这是模块页面访问**当前登录用户数据**的唯一入口。设计约束来自报告：
//   - "worker/QML 不持有 DB 文件、key 或任意 SQL"
//   - "数据只写当前用户本机库"
//   - "当前用户在任务开始时绑定；切换用户不会串档"
//
// 因此本类给的是一组**语义化接口**，不是 SQL 也不是表句柄：
//   sensor.*     传感器样本的写入与查询
//   history.*    按时间窗聚合的曲线数据
//   readings.*   读数（血压等）的写入与查询
//   kv.*         模块自己的小状态（界面偏好、上次选择等）
//
// 作用域（moduleId + instance + 当前用户）由宿主编死，QML 无法越权。
// 未登录或库未打开时所有写操作失败关闭，读操作返回空。
class ModuleDataApi : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString scope READ scope CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
    ModuleDataApi(int moduleId, QString instance, QObject *parent = nullptr);

    // 由宿主在登录/切用户/会话重建时更新。nullptr = 未登录。
    void setDatabase(security::UserDatabase *database);
    void setInstance(const QString &instance);

    QString scope() const;
    bool ready() const;

    // ---- 传感器样本 ----
    // sample 是模块 schema 的字段字典（temperature_c / humidity_rh / ...）。
    // 缺失字段按 null 写入，不用 0 伪造。
    Q_INVOKABLE bool appendSample(const QVariantMap &sample);
    // 最近 N 条；每条含 received_at / source / valid 与各字段。
    Q_INVOKABLE QVariantList recentSamples(int limit = 50) const;

    // ---- 时间窗聚合 ----
    // 返回 [{t, sum, low, high, count}]，用于画曲线。
    Q_INVOKABLE QVariantList historySeries(qint64 spanMs, qint64 stepMs) const;

    // ---- 读数（血压等）----
    Q_INVOKABLE bool appendReading(const QVariantMap &reading);
    Q_INVOKABLE QVariantList recentReadings(int limit = 50) const;

    // ---- 模块私有键值 ----
    Q_INVOKABLE bool setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE bool removeValue(const QString &key);
    Q_INVOKABLE QStringList keys() const;

signals:
    void readyChanged();
    // 任何写入成功后发出，模块页据此刷新自己的视图。
    void dataChanged();

private:
    static bool keyIsValid(const QString &key);
    bool checkReady() const;

    security::UserDatabase *database_ = nullptr;
    int moduleId_ = 0;
    QString instance_;
};
