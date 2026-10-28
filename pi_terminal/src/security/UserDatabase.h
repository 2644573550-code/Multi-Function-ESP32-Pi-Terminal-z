#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

namespace security {


class SqlCipherStore;

// 数据来源（报告 §12.1：UI 与存储必须区分来源，避免重复记录）。
enum class SampleSource {
    Unknown,
    DeviceAuto,   // 设备自动测量
    DeviceManual, // 设备上手动触发
    UserEntry,    // 用户在终端手工录入
};

// 一条传感器样本。字段与 f-day1 §6 一致：缺失值必须是 null，不能用 0 伪造。
struct SensorSample {
    qint64 receivedAtMs = 0;    // Pi 侧单调时间，由可信宿主填，不接受模块自报
    qint64 sampleSeq = 0;
    QString deviceId;
    bool hasTemperature = false;
    double temperatureC = 0;
    bool hasHumidity = false;
    double humidityRh = 0;
    bool hasPressure = false;
    double pressureHpa = 0;
    bool hasLight = false;
    double lightLux = 0;
    bool hasBatteryV = false;
    double batteryV = 0;
    bool hasBatteryPct = false;
    int batteryPct = 0;
    // 来源与有效性（§12.1 / §12.3）：异常读数标无效，不补造数据。
    SampleSource source = SampleSource::Unknown;
    bool valid = true;
    QString invalidReason;   // 仅 valid=false 时有意义
};

// 每用户独立 SQLCipher DB。userId 由可信会话决定，不接受 worker/调用方自报。
//
// 目录布局：<root>/<userId>/data.db
// 文件名不含用户名/UID 明文；userId 本身是随机不透明 ID（见 UserService）。
class UserDatabase {
public:
    // root 为空时用 AppConfigLocation/users（不是模块可读的 AppLocalDataLocation）。
    explicit UserDatabase(QString root = {});
    ~UserDatabase();

    // userId 必须来自 SessionManager；dbKey 必须由 PIN 经 KEK 解封得到。
    bool openForSession(const QString &userId, const QByteArray &dbKey);
    void close();

    bool isOpen() const;
    // 当前打开的用户（不透明 ID）。备份用它做归属校验。
    QString openUserId() const { return openUserId_; }
    QString errorCode() const;

    // 写入归属当前打开的用户；userId 参数不存在，防止越权写入他人库。
    bool appendSample(const SensorSample &sample);

    // ---- 模块页的键值存储（报告 §6.1：QML 不持有 DB/SQL）----
    // 作用域由宿主给定：moduleId + instance。QML 只能读写自己这一格。
    bool setModuleValue(int moduleId, const QString &instance, const QString &key,
                        const QByteArray &encodedValue);
    QByteArray moduleValue(int moduleId, const QString &instance, const QString &key) const;
    bool removeModuleValue(int moduleId, const QString &instance, const QString &key);
    QStringList moduleKeys(int moduleId, const QString &instance) const;
    bool clearModuleValues(int moduleId, const QString &instance);

    // 一次测量（报告 §12.1）：同一次测量可能包含收缩压/舒张压/脉搏多个指标，
    // 必须作为一条记录保存并共享来源、型号、时间与有效性，
    // 否则无法区分"一次测了三个值"和"测了三次"。
    struct Measurement {
        qint64 receivedAtMs = 0;
        qint64 measuredAtMs = 0;
        QString source;          // device.auto / device.manual / user.entry
        QString deviceId;
        QString deviceModel;
        QString unit;
        // 每项独立可缺：缺失用 has* = false 表示，不写 0。
        bool hasSystolic = false;   double systolic = 0;
        bool hasDiastolic = false;  double diastolic = 0;
        bool hasPulse = false;      double pulse = 0;
        bool valid = true;
        QString invalidReason;
    };
    bool appendMeasurement(const Measurement &measurement);
    QVector<Measurement> queryMeasurements(int limit = 50) const;

    // 一条读数记录（报告 §12.1：保留型号、来源、单位、时间、有效性）。
    struct Reading {
        qint64 receivedAtMs = 0;
        qint64 measuredAtMs = 0;
        QString source;        // device.auto / device.manual / user.entry
        QString deviceId;
        QString deviceModel;
        QString metric;        // pressure / ...
        bool hasValue = false;
        double value = 0;
        QString unit;
        bool valid = true;
        QString invalidReason;
    };
    QVector<Reading> queryReadings(int limit = 50) const;

    // 手工录入（报告 §12.1）：与设备来源分开记录，UI 据此避免重复录入。
    // 只写当前打开的用户库；未登录/库未打开时返回 false。
    bool appendManualReading(double value, const QString &unit, const QString &deviceModel);

    // 设备上报的读数（报告 §12.1）：来源由调用方给定，与手工录入分开。
    // source 只接受 device.auto / device.manual，其它值按 device.auto 处理。
    bool appendDeviceReading(double value, const QString &unit, const QString &deviceModel,
                             const QString &source, const QString &deviceId);

    struct Record {
        qint64 receivedAtMs = 0;
        SampleSource source = SampleSource::Unknown;
        bool valid = true;
        qint64 sampleSeq = 0;
        QString deviceId;
        double temperatureC = 0;
        bool hasTemperature = false;
    };
    QVector<Record> queryRecent(int limit = 50) const;

    // ---- 模块历史的可信存储（替代模块自己写明文 JSON，报告 §6.1）----
    // 模块只发原始观测；聚合与持久化都在宿主侧完成，模块拿不到 DB。
    struct HistoryBucket {
        qint64 bucketMs = 0;      // 按 minute 对齐的时间戳
        double sum = 0;
        double low = 0;
        double high = 0;
        int count = 0;
    };
    // 把一条观测并入当前分钟桶。返回 false 表示样本无效或库未打开。
    bool addHistorySample(qint64 observedAtMs, double value);

    // 直接写入一个已聚合的桶（恢复备份用）。
    // 与 addHistorySample 的区别：不重新聚合，保留原 sum/low/high/count。
    bool restoreHistoryBucket(const HistoryBucket &bucket);
    // 按 [now - span, now] 取聚合序列，stepMs 为分组宽度。
    QVector<HistoryBucket> historySeries(qint64 nowMs, qint64 spanMs, qint64 stepMs) const;
    // 清理早于 nowMs - retentionMs 的桶。
    int pruneHistory(qint64 nowMs, qint64 retentionMs);
    // 审计入口：只在本用户库内可写，管理员也读不到他人审计。
    // 返回 nullptr 表示库未打开。
    class AuditLog *audit();

    // 旧明文 JSON 是否已迁移（供界面区分"还没导入"与"没有历史"）。
    bool legacyHistoryMigrated() const;
    bool markLegacyHistoryMigrated();

    // 跨用户拒绝：用 A 的会话打开 B 的库必须失败。
    // 返回 false 表示被拒绝（这正是期望行为）。
    bool openForeign(const QString &userId, const QByteArray &dbKey);

    QString databasePathFor(const QString &userId) const;
    QString cipherVersion() const;

private:
    bool checkCipherMajor(const QString &currentMajor);

    QString root_;
    class AuditLog *audit_ = nullptr;
    QString openUserId_;
    SqlCipherStore *store_ = nullptr;
};

}  // namespace security
