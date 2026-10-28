#include "core/ModuleDataApi.h"

#include "security/UserDatabase.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

namespace {

// 模块 schema 的字段名 → SensorSample 字段。只认识这些，其余忽略，
// 避免模块把任意键塞进样本表。
enum class Field { Temperature, Humidity, Pressure, Light, BatteryV, BatteryPct };

Field fieldOf(const QString &name, bool *ok) {
    static const QHash<QString, Field> map{
        {QStringLiteral("temperature_c"), Field::Temperature},
        {QStringLiteral("temperature_C"), Field::Temperature},
        {QStringLiteral("humidity_rh"), Field::Humidity},
        {QStringLiteral("humidity_RH"), Field::Humidity},
        {QStringLiteral("pressure_hpa"), Field::Pressure},
        {QStringLiteral("pressure_hPa"), Field::Pressure},
        {QStringLiteral("light_lux"), Field::Light},
        {QStringLiteral("battery_v"), Field::BatteryV},
        {QStringLiteral("battery_pct"), Field::BatteryPct},
    };
    const auto it = map.constFind(name);
    const bool found = it != map.constEnd();
    if (ok) *ok = found;
    // 未命中时不能解引用 end()：返回一个占位值，由调用方看 *ok。
    return found ? it.value() : Field::Temperature;
}

security::SampleSource sourceOf(const QString &name) {
    if (name == QLatin1String("device.auto")) return security::SampleSource::DeviceAuto;
    if (name == QLatin1String("device.manual")) return security::SampleSource::DeviceManual;
    if (name == QLatin1String("user.entry")) return security::SampleSource::UserEntry;
    return security::SampleSource::Unknown;
}

QString sourceName(security::SampleSource source) {
    switch (source) {
    case security::SampleSource::DeviceAuto: return QStringLiteral("device.auto");
    case security::SampleSource::DeviceManual: return QStringLiteral("device.manual");
    case security::SampleSource::UserEntry: return QStringLiteral("user.entry");
    case security::SampleSource::Unknown: break;
    }
    return QStringLiteral("unknown");
}

}  // namespace

ModuleDataApi::ModuleDataApi(int moduleId, QString instance, QObject *parent)
    : QObject(parent), moduleId_(moduleId), instance_(std::move(instance)) {}

void ModuleDataApi::setDatabase(security::UserDatabase *database) {
    if (database_ == database) return;
    database_ = database;
    emit readyChanged();
}

void ModuleDataApi::setInstance(const QString &instance) {
    if (instance_ == instance) return;
    instance_ = instance;
    emit readyChanged();
}

QString ModuleDataApi::scope() const {
    return QStringLiteral("module-%1/%2").arg(moduleId_).arg(instance_);
}

bool ModuleDataApi::ready() const {
    return database_ && database_->isOpen();
}

bool ModuleDataApi::checkReady() const {
    // 未登录/库未打开：失败关闭，绝不落到临时文件或内存假存储。
    return ready();
}

bool ModuleDataApi::keyIsValid(const QString &key) {
    if (key.isEmpty() || key.size() > 64) return false;
    for (const QChar c : key) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-')
            && c != QLatin1Char('.'))
            return false;
    }
    return true;
}

bool ModuleDataApi::appendSample(const QVariantMap &sample) {
    if (!checkReady() || instance_.isEmpty()) return false;

    security::SensorSample record;
    // 接收时间由宿主填，不接受模块自报（报告 §11.4）。
    record.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
    record.sampleSeq = qint64(sample.value(QStringLiteral("sample_seq")).toDouble());
    record.deviceId = instance_;
    record.source = sourceOf(sample.value(QStringLiteral("source")).toString());
    record.valid = sample.value(QStringLiteral("valid"), true).toBool();
    record.invalidReason = sample.value(QStringLiteral("invalid_reason")).toString();

    // 逐字段填值；缺失必须保持 hasXxx=false（写 null），不能补 0。
    for (auto it = sample.constBegin(); it != sample.constEnd(); ++it) {
        bool known = false;
        const Field field = fieldOf(it.key(), &known);
        if (!known) continue;
        // null 与"字段缺失"等价：都按缺失处理。
        if (!it.value().isValid() || it.value().isNull()) continue;
        const double value = it.value().toDouble();
        if (!std::isfinite(value)) continue;
        switch (field) {
        case Field::Temperature: record.hasTemperature = true; record.temperatureC = value; break;
        case Field::Humidity: record.hasHumidity = true; record.humidityRh = value; break;
        case Field::Pressure: record.hasPressure = true; record.pressureHpa = value; break;
        case Field::Light: record.hasLight = true; record.lightLux = value; break;
        case Field::BatteryV: record.hasBatteryV = true; record.batteryV = value; break;
        case Field::BatteryPct: record.hasBatteryPct = true; record.batteryPct = int(value); break;
        }
    }

    if (!database_->appendSample(record)) return false;
    emit dataChanged();
    return true;
}

QVariantList ModuleDataApi::recentSamples(int limit) const {
    QVariantList out;
    if (!checkReady()) return out;
    for (const auto &record : database_->queryRecent(limit)) {
        QVariantMap item;
        item.insert(QStringLiteral("received_at"), record.receivedAtMs);
        item.insert(QStringLiteral("sample_seq"), record.sampleSeq);
        item.insert(QStringLiteral("source"), sourceName(record.source));
        item.insert(QStringLiteral("valid"), record.valid);
        // 缺失值不放进 map：QML 侧用 undefined 判断"未检测"。
        if (record.hasTemperature) item.insert(QStringLiteral("temperature_c"), record.temperatureC);
        out.append(item);
    }
    return out;
}

QVariantList ModuleDataApi::historySeries(qint64 spanMs, qint64 stepMs) const {
    QVariantList out;
    if (!checkReady() || spanMs <= 0 || stepMs <= 0) return out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &bucket : database_->historySeries(now, spanMs, stepMs)) {
        QVariantMap item;
        item.insert(QStringLiteral("t"), bucket.bucketMs);
        item.insert(QStringLiteral("sum"), bucket.sum);
        item.insert(QStringLiteral("low"), bucket.low);
        item.insert(QStringLiteral("high"), bucket.high);
        item.insert(QStringLiteral("count"), bucket.count);
        out.append(item);
    }
    return out;
}

bool ModuleDataApi::appendMeasurement(const QVariantMap &measurement) {
    if (!checkReady()) return false;

    security::UserDatabase::Measurement record;
    record.receivedAtMs = QDateTime::currentMSecsSinceEpoch();
    record.source = measurement.value(QStringLiteral("source"),
                                      QStringLiteral("unknown")).toString();
    record.deviceId = instance_;
    record.deviceModel = measurement.value(QStringLiteral("device_model")).toString();
    record.unit = measurement.value(QStringLiteral("unit"), QStringLiteral("mmHg")).toString();
    record.valid = measurement.value(QStringLiteral("valid"), true).toBool();
    record.invalidReason = measurement.value(QStringLiteral("invalid_reason")).toString();

    // 逐指标判缺失：undefined / null / 非数字都视为未测，不补 0。
    const auto take = [&measurement](const char *key, double *out) {
        const QVariant v = measurement.value(QString::fromLatin1(key));
        if (!v.isValid() || v.isNull()) return false;
        bool ok = false;
        const double d = v.toDouble(&ok);
        if (!ok || !std::isfinite(d)) return false;
        *out = d;
        return true;
    };
    record.hasSystolic = take("systolic", &record.systolic);
    record.hasDiastolic = take("diastolic", &record.diastolic);
    record.hasPulse = take("pulse", &record.pulse);

    if (!record.hasSystolic && !record.hasDiastolic && !record.hasPulse) return false;
    if (!database_->appendMeasurement(record)) return false;
    emit dataChanged();
    return true;
}

QVariantList ModuleDataApi::recentMeasurements(int limit) const {
    QVariantList out;
    if (!checkReady()) return out;
    for (const auto &m : database_->queryMeasurements(limit)) {
        QVariantMap item;
        item.insert(QStringLiteral("received_at"), m.receivedAtMs);
        item.insert(QStringLiteral("measured_at"), m.measuredAtMs);
        item.insert(QStringLiteral("source"), m.source);
        item.insert(QStringLiteral("device_model"), m.deviceModel);
        item.insert(QStringLiteral("unit"), m.unit);
        // 缺失指标不放进 map：QML 用 undefined 判断"未检测"。
        if (m.hasSystolic) item.insert(QStringLiteral("systolic"), m.systolic);
        if (m.hasDiastolic) item.insert(QStringLiteral("diastolic"), m.diastolic);
        if (m.hasPulse) item.insert(QStringLiteral("pulse"), m.pulse);
        item.insert(QStringLiteral("valid"), m.valid);
        if (!m.invalidReason.isEmpty())
            item.insert(QStringLiteral("invalid_reason"), m.invalidReason);
        out.append(item);
    }
    return out;
}

bool ModuleDataApi::appendReading(const QVariantMap &reading) {
    if (!checkReady()) return false;
    // 读数必须带数值与单位；缺失即拒绝，不补造。
    if (!reading.value(QStringLiteral("value")).isValid()) return false;
    const double value = reading.value(QStringLiteral("value")).toDouble();
    if (!std::isfinite(value)) return false;
    const QString unit = reading.value(QStringLiteral("unit")).toString();

    // 手工录入与设备来源分开记录（报告 §12.1）。
    // 这两条路径写同一张表但 source 不同，界面据此区分并避免重复记录。
    const QString source = reading.value(QStringLiteral("source")).toString();
    const QString deviceModel = reading.value(QStringLiteral("device_model")).toString();
    const bool ok = (source == QLatin1String("user.entry"))
        ? database_->appendManualReading(value, unit, deviceModel)
        : database_->appendDeviceReading(value, unit, deviceModel, source,
                                         reading.value(QStringLiteral("device_id")).toString());
    if (ok) emit dataChanged();
    return ok;
}

QVariantList ModuleDataApi::recentReadings(int limit) const {
    QVariantList out;
    if (!checkReady()) return out;
    for (const auto &reading : database_->queryReadings(limit)) {
        QVariantMap item;
        item.insert(QStringLiteral("received_at"), reading.receivedAtMs);
        item.insert(QStringLiteral("measured_at"), reading.measuredAtMs);
        item.insert(QStringLiteral("source"), reading.source);
        item.insert(QStringLiteral("device_model"), reading.deviceModel);
        item.insert(QStringLiteral("metric"), reading.metric);
        // 缺失值不写 0：用 has_value=false 表达。
        item.insert(QStringLiteral("has_value"), reading.hasValue);
        if (reading.hasValue) item.insert(QStringLiteral("value"), reading.value);
        item.insert(QStringLiteral("unit"), reading.unit);
        item.insert(QStringLiteral("valid"), reading.valid);
        if (!reading.invalidReason.isEmpty())
            item.insert(QStringLiteral("invalid_reason"), reading.invalidReason);
        out.append(item);
    }
    return out;
}

bool ModuleDataApi::setValue(const QString &key, const QVariant &value) {
    if (!checkReady() || !keyIsValid(key) || instance_.isEmpty()) return false;
    const QJsonValue json = QJsonValue::fromVariant(value);
    const QByteArray encoded = QJsonDocument(QJsonArray{json}).toJson(QJsonDocument::Compact);
    if (!database_->setModuleValue(moduleId_, instance_, key, encoded)) return false;
    emit dataChanged();
    return true;
}

QVariant ModuleDataApi::value(const QString &key, const QVariant &fallback) const {
    if (!checkReady() || !keyIsValid(key)) return fallback;
    const QByteArray encoded = database_->moduleValue(moduleId_, instance_, key);
    if (encoded.isEmpty()) return fallback;
    const QJsonDocument document = QJsonDocument::fromJson(encoded);
    if (!document.isArray() || document.array().isEmpty()) return fallback;
    return document.array().first().toVariant();
}

bool ModuleDataApi::removeValue(const QString &key) {
    if (!checkReady() || !keyIsValid(key)) return false;
    const bool ok = database_->removeModuleValue(moduleId_, instance_, key);
    if (ok) emit dataChanged();
    return ok;
}

QStringList ModuleDataApi::keys() const {
    if (!checkReady()) return {};
    return database_->moduleKeys(moduleId_, instance_);
}
