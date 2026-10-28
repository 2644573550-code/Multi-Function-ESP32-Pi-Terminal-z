// 啸天围界模块的业务逻辑测试（报告 §11.3 / §11.5）。
//
// 直接编译 module.cpp 测试：去抖、告警保持、触发计数、未知状态不冒充。
#include "pi_module_api.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {
// 捕获模块 emit 的事件
struct Captured {
    QString type;
    QJsonObject data;
};
QVector<Captured> g_events;

extern "C" void captureEvent(void *, const char *type, const char *json) {
    Captured captured;
    captured.type = QString::fromUtf8(type);
    captured.data = QJsonDocument::fromJson(QByteArray(json)).object();
    g_events.append(captured);
}
extern "C" int noopSend(void *, uint8_t, const uint8_t *, uint32_t) { return 0; }
extern "C" void noopAction(const char *, const char *) {}
extern "C" void noopPublish(const char *, const char *) {}
extern "C" const char *noopDir() { return "/tmp"; }
extern "C" void noopLog(int, const char *) {}
}  // namespace

// module.cpp 的导出
extern "C" uint32_t pi_module_api_version();
extern "C" pi_module_handle_t *pi_module_create(const pi_host_services_t *);
extern "C" int pi_module_on_attach(pi_module_handle_t *, const char *);
extern "C" int pi_module_on_data(pi_module_handle_t *, uint8_t, const uint8_t *, uint32_t);
extern "C" int pi_module_action(pi_module_handle_t *, const char *);
extern "C" void pi_module_stop(pi_module_handle_t *);
extern "C" void pi_module_destroy(pi_module_handle_t *);

class FenceModuleTest : public QObject {
    Q_OBJECT

private:
    pi_host_services_t services_{};

    static QByteArray payload(const QJsonObject &obj) {
        return QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }
    static int send(pi_module_handle_t *m, const QJsonObject &obj) {
        const QByteArray p = payload(obj);
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
        services_.context = nullptr;
        services_.emit_event = &captureEvent;
        services_.send_data = &noopSend;
        services_.publish_event = &noopPublish;
        services_.publish_action = &noopAction;
        services_.data_directory = &noopDir;
        services_.log = &noopLog;
    }

    // 去抖：单次遮挡不算数，连续两次才认。
    void debounceRequiresTwoSamples()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(m);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"fence-1\"}") == 0);
        QVERIFY(pi_module_action(m, "arm") == 0);

        QJsonObject seg{{QStringLiteral("segment_id"), QStringLiteral("A")},
                        {QStringLiteral("blocked"), true}};
        // 第一次：未收敛，valid=false 且不计触发。
        QVERIFY(send(m, {{QStringLiteral("segments"), QJsonArray{seg}}}) == 0);
        QCOMPARE(last().value(QStringLiteral("trigger_count")).toInt(), 0);
        QCOMPARE(last().value(QStringLiteral("segments")).toArray().first().toObject()
                     .value(QStringLiteral("valid")).toBool(), false);

        // 第二次：收敛，计一次触发。
        QVERIFY(send(m, {{QStringLiteral("segments"), QJsonArray{seg}}}) == 0);
        QCOMPARE(last().value(QStringLiteral("trigger_count")).toInt(), 1);
        QCOMPARE(last().value(QStringLiteral("segments")).toArray().first().toObject()
                     .value(QStringLiteral("valid")).toBool(), true);

        pi_module_destroy(m);
    }

    // 未布防时遮挡不计触发（避免把路过当成告警）。
    void disarmedBlockageDoesNotTrigger()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"fence-2\"}") == 0);
        QJsonObject seg{{QStringLiteral("segment_id"), QStringLiteral("A")},
                        {QStringLiteral("blocked"), true}};
        for (int i = 0; i < 3; ++i) send(m, {{QStringLiteral("segments"), QJsonArray{seg}}});
        QCOMPARE(last().value(QStringLiteral("trigger_count")).toInt(), 0);
        pi_module_destroy(m);
    }

    // 告警保持：触发后一段时间内 alarm_active 为真。
    void alarmHoldsForAWindow()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"fence-3\"}") == 0);
        QVERIFY(pi_module_action(m, "arm") == 0);
        QJsonObject seg{{QStringLiteral("segment_id"), QStringLiteral("A")},
                        {QStringLiteral("blocked"), true}};
        send(m, {{QStringLiteral("segments"), QJsonArray{seg}}});
        send(m, {{QStringLiteral("segments"), QJsonArray{seg}}});
        QVERIFY2(last().value(QStringLiteral("alarm_active")).toBool(),
                 "alarm must hold right after the trigger");
        pi_module_destroy(m);
    }

    // 没有供电硬件时如实上报"未知"，不伪造。
    void powerIsUnknownWithoutHardware()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"fence-4\"}") == 0);
        send(m, {{QStringLiteral("segments"), QJsonArray{}}});
        QCOMPARE(last().value(QStringLiteral("power_known")).toBool(), false);
        pi_module_destroy(m);
    }

    // sensor_online 与 fault_code 是两种不同状态，不能互相冒充。
    void offlineAndFaultAreDistinct()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"fence-5\"}") == 0);

        send(m, {{QStringLiteral("segments"), QJsonArray{}},
                 {QStringLiteral("sensor_online"), false},
                 {QStringLiteral("fault_code"), QString()}});
        QCOMPARE(last().value(QStringLiteral("sensor_online")).toBool(), false);
        QCOMPARE(last().value(QStringLiteral("fault_code")).toString(), QString());

        send(m, {{QStringLiteral("segments"), QJsonArray{}},
                 {QStringLiteral("sensor_online"), true},
                 {QStringLiteral("fault_code"), QStringLiteral("E42")}});
        QCOMPARE(last().value(QStringLiteral("fault_code")).toString(), QStringLiteral("E42"));
        pi_module_destroy(m);
    }

    // 未知动作有界拒绝，不崩溃、不改状态。
    void unknownActionIsRejected()
    {
        pi_module_handle_t *m = pi_module_create(&services_);
        QVERIFY(pi_module_on_attach(m, "{\"module_instance\":\"fence-6\"}") == 0);
        QCOMPARE(pi_module_action(m, "self_destruct"), -1);
        QCOMPARE(pi_module_action(m, ""), -1);
        // 已知动作仍然可用。
        QCOMPARE(pi_module_action(m, "arm"), 0);
        pi_module_destroy(m);
    }
};

QTEST_GUILESS_MAIN(FenceModuleTest)
#include "fence_module_test.moc"
