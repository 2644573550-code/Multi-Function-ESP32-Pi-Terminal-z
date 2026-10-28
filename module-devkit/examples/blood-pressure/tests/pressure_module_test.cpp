// 血压模块的业务逻辑测试（报告 §12.1 / §12.3）。
//
// 重点：边界值校验、来源区分、异常读数标无效但不修正数值、不补造数据。
#include "pi_module_api.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {
struct Captured {
    QString type;
    QJsonObject data;
};
QVector<Captured> g_events;

extern "C" void captureEvent(void *, const char *type, const char *json) {
    g_events.append({QString::fromUtf8(type),
                     QJsonDocument::fromJson(QByteArray(json)).object()});
}
extern "C" int noopSend(void *, uint8_t, const uint8_t *, uint32_t) { return 0; }
extern "C" void noopAction(const char *, const char *) {}
extern "C" void noopPublish(const char *, const char *) {}
extern "C" const char *noopDir() { return "/tmp"; }
extern "C" void noopLog(int, const char *) {}
}  // namespace

extern "C" uint32_t pi_module_api_version();
extern "C" pi_module_handle_t *pi_module_create(const pi_host_services_t *);
extern "C" int pi_module_on_attach(pi_module_handle_t *, const char *);
extern "C" int pi_module_on_data(pi_module_handle_t *, uint8_t, const uint8_t *, uint32_t);
extern "C" int pi_module_action(pi_module_handle_t *, const char *);
extern "C" void pi_module_destroy(pi_module_handle_t *);

class PressureModuleTest : public QObject {
    Q_OBJECT

private:
    pi_host_services_t services_{};

    static int send(pi_module_handle_t *m, const QJsonObject &obj) {
        const QByteArray p = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        return pi_module_on_data(m, 2, reinterpret_cast<const uint8_t *>(p.constData()),
                                 uint32_t(p.size()));
    }
    static QJsonObject last() { return g_events.isEmpty() ? QJsonObject{} : g_events.last().data; }

private slots:
    void init()
    {
        g_events.clear();
        services_ = {};
        services_.api_version = 1;
        services_.emit_event = &captureEvent;
        services_.send_data = &noopSend;
        services_.publish_event = &noopPublish;
        services_.publish_action = &noopAction;
        services_.data_directory = &noopDir;
        services_.log = &noopLog;
    }

    // 正常读数：有效，字段原样保留。
    void validReadingPassesThrough()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"bp-1\"}") == 0);
        QVERIFY(send(m, {{QStringLiteral("systolic"), 120},
                         {QStringLiteral("diastolic"), 80},
                         {QStringLiteral("pulse"), 72},
                         {QStringLiteral("source"), QStringLiteral("device.auto")}}) == 0);
        const QJsonObject out = last();
        QCOMPARE(out.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(out.value(QStringLiteral("systolic")).toInt(), 120);
        QCOMPARE(out.value(QStringLiteral("diastolic")).toInt(), 80);
        QCOMPARE(out.value(QStringLiteral("source")).toString(), QStringLiteral("device.auto"));
        QCOMPARE(out.value(QStringLiteral("invalid_reason")).toString(), QString());
        pi_module_destroy(m);
    }

    // 边界值：超范围标无效，但**不修正数值**。
    void outOfRangeIsInvalidButNotClamped()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"bp-2\"}") == 0);
        QVERIFY(send(m, {{QStringLiteral("systolic"), 400},
                         {QStringLiteral("diastolic"), 80}}) == 0);
        const QJsonObject out = last();
        QCOMPARE(out.value(QStringLiteral("valid")).toBool(), false);
        QVERIFY(out.value(QStringLiteral("invalid_reason")).toString()
                    .contains(QStringLiteral("systolic_out_of_range")));
        pi_module_destroy(m);
    }

    // 收缩压不高于舒张压：无效，且不交换数值。
    void invertedReadingIsInvalid()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"bp-3\"}") == 0);
        QVERIFY(send(m, {{QStringLiteral("systolic"), 70},
                         {QStringLiteral("diastolic"), 90}}) == 0);
        const QJsonObject out = last();
        QCOMPARE(out.value(QStringLiteral("valid")).toBool(), false);
        QVERIFY(out.value(QStringLiteral("invalid_reason")).toString()
                    .contains(QStringLiteral("systolic_not_above_diastolic")));
        pi_module_destroy(m);
    }

    // 什么都没测到：明确报缺字段，不凭空造记录。
    void emptyReadingReportsNoMeasurement()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"bp-4\"}") == 0);
        QVERIFY(send(m, {}) == 0);
        const QJsonObject out = last();
        QCOMPARE(out.value(QStringLiteral("valid")).toBool(), false);
        QVERIFY(out.value(QStringLiteral("invalid_reason")).toString()
                    .contains(QStringLiteral("no_measurement")));
        // 缺失字段不写进 JSON。
        QVERIFY(!out.contains(QStringLiteral("systolic")));
        pi_module_destroy(m);
    }

    // 来源区分：手工与设备不混。
    void sourceIsPreserved()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"bp-5\"}") == 0);
        send(m, {{QStringLiteral("systolic"), 118},
                 {QStringLiteral("source"), QStringLiteral("user.entry")}});
        QCOMPARE(last().value(QStringLiteral("source")).toString(), QStringLiteral("user.entry"));
        send(m, {{QStringLiteral("systolic"), 118},
                 {QStringLiteral("source"), QStringLiteral("device.manual")}});
        QCOMPARE(last().value(QStringLiteral("source")).toString(), QStringLiteral("device.manual"));
        pi_module_destroy(m);
    }

    // 单位缺失时用默认 mmHg，不猜测成 kPa。
    void defaultUnitIsMmHg()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"bp-6\"}") == 0);
        send(m, {{QStringLiteral("systolic"), 120}});
        QCOMPARE(last().value(QStringLiteral("unit")).toString(), QStringLiteral("mmHg"));
        pi_module_destroy(m);
    }

    // 型号来自预置 profile，不接受模块自报。
    void deviceModelComesFromProfile()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"bp-7\","
                                       "\"device_model\":\"OMRON-HEM-7121\"}") == 0);
        send(m, {{QStringLiteral("systolic"), 120}});
        QCOMPARE(last().value(QStringLiteral("device_model")).toString(),
                 QStringLiteral("OMRON-HEM-7121"));
        pi_module_destroy(m);
    }
};

QTEST_GUILESS_MAIN(PressureModuleTest)
#include "pressure_module_test.moc"
