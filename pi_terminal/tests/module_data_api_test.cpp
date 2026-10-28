// 报告 §6.1 / §12.2：模块 QML 能访问**当前用户**的数据，但拿不到 DB/SQL。
#include "core/ModuleDataApi.h"
#include "security/UserDatabase.h"

#include <QDateTime>
#include <QTemporaryDir>
#include <QtTest>

using namespace security;

class ModuleDataApiTest : public QObject {
    Q_OBJECT

private:
    static QByteArray keyOf(char c) { return QByteArray(32, c); }

private slots:
    // 未登录（无库）时全部失败关闭，不落到任何临时存储。
    void unauthenticatedFailsClosed()
    {
        ModuleDataApi api(80, QStringLiteral("inst-1"));
        QVERIFY(!api.ready());
        QVERIFY(!api.appendSample(QVariantMap{{QStringLiteral("temperature_c"), 22.0}}));
        QVERIFY(!api.setValue(QStringLiteral("k"), 1));
        QVERIFY(api.recentSamples(10).isEmpty());
        QVERIFY(api.value(QStringLiteral("k")).isNull());
    }

    // 写入的样本能被读回，缺失字段不补 0。
    void samplesRoundTripWithoutFillingZeros()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));

        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&db);
        QVERIFY(api.ready());

        // 只给温度：湿度/气压等必须缺失，不能变 0。
        QVERIFY(api.appendSample(QVariantMap{
            {QStringLiteral("temperature_c"), 22.5},
            {QStringLiteral("source"), QStringLiteral("device.auto")}}));

        const QVariantList recent = api.recentSamples(10);
        QCOMPARE(recent.size(), 1);
        const QVariantMap first = recent.first().toMap();
        QCOMPARE(first.value(QStringLiteral("temperature_c")).toDouble(), 22.5);
        QVERIFY2(!first.contains(QStringLiteral("humidity_rh")),
                 "missing fields must stay absent, not zero");
        QCOMPARE(first.value(QStringLiteral("source")).toString(), QStringLiteral("device.auto"));
        QVERIFY(first.value(QStringLiteral("valid")).toBool());
    }

    // 作用域隔离：不同模块、不同实例、不同用户互不可见。
    void scopeIsIsolated()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));

        ModuleDataApi a(80, QStringLiteral("inst-1"));
        ModuleDataApi b(80, QStringLiteral("inst-2"));
        ModuleDataApi other(81, QStringLiteral("inst-1"));
        for (auto *api : {&a, &b, &other}) api->setDatabase(&db);

        QVERIFY(a.setValue(QStringLiteral("theme"), QStringLiteral("dark")));
        QCOMPARE(a.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
        // 同模块不同实例：看不到。
        QVERIFY(b.value(QStringLiteral("theme")).isNull());
        // 同实例不同模块：看不到。
        QVERIFY(other.value(QStringLiteral("theme")).isNull());
    }

    // 切用户后旧作用域的数据不可见（报告 §12.2「切换用户不会串档」）。
    void switchingUserDoesNotLeakData()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        UserDatabase userA(dir.path());
        QVERIFY(userA.openForSession(QStringLiteral("usera"), keyOf('a')));
        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&userA);
        QVERIFY(api.setValue(QStringLiteral("secret"), QStringLiteral("a-only")));
        userA.close();

        // 换到用户 B 的库。
        UserDatabase userB(dir.path());
        QVERIFY(userB.openForSession(QStringLiteral("userb"), keyOf('b')));
        api.setDatabase(&userB);
        QVERIFY2(api.value(QStringLiteral("secret")).isNull(),
                 "data must not leak across users");
    }

    // 键名必须是安全短标识，防止写爆存储。
    void invalidKeysAreRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));
        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&db);

        QVERIFY(!api.setValue(QString(), 1));
        QVERIFY(!api.setValue(QString(65, QLatin1Char('k')), 1));
        QVERIFY(!api.setValue(QStringLiteral("bad key"), 1));
        QVERIFY(!api.setValue(QStringLiteral("bad/key"), 1));
        QVERIFY(api.setValue(QStringLiteral("good.key_1-2"), 1));
    }

    // 历史曲线走聚合，返回的桶带 low/high/count，不能被压成单个平均值。
    void historySeriesKeepsAggregates()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(db.addHistorySample(now - 60000, 20.0));
        QVERIFY(db.addHistorySample(now - 59000, 24.0));

        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&db);
        const QVariantList series = api.historySeries(7LL * 24 * 3600 * 1000, 60000);
        QCOMPARE(series.size(), 1);
        const QVariantMap bucket = series.first().toMap();
        QCOMPARE(bucket.value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(bucket.value(QStringLiteral("low")).toDouble(), 20.0);
        QCOMPARE(bucket.value(QStringLiteral("high")).toDouble(), 24.0);
        QCOMPARE(bucket.value(QStringLiteral("sum")).toDouble(), 44.0);
    }

    // 读数：来源可区分，缺失值不补 0。
    void readingsKeepSourceAndNulls()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));
        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&db);

        QVERIFY(api.appendReading(QVariantMap{
            {QStringLiteral("value"), 120.0},
            {QStringLiteral("unit"), QStringLiteral("mmHg")},
            {QStringLiteral("source"), QStringLiteral("user.entry")}}));

        const QVariantList list = api.recentReadings(10);
        QCOMPARE(list.size(), 1);
        const QVariantMap first = list.first().toMap();
        QCOMPARE(first.value(QStringLiteral("source")).toString(), QStringLiteral("user.entry"));
        QCOMPARE(first.value(QStringLiteral("value")).toDouble(), 120.0);
        QCOMPARE(first.value(QStringLiteral("unit")).toString(), QStringLiteral("mmHg"));
        QVERIFY(first.value(QStringLiteral("valid")).toBool());

        // 缺 value 的读数必须拒绝。
        QVERIFY(!api.appendReading(QVariantMap{{QStringLiteral("unit"), QStringLiteral("mmHg")}}));
    }

    // 写入成功要发 dataChanged，便于模块页刷新。
    void writesEmitDataChanged()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));
        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&db);

        QSignalSpy spy(&api, &ModuleDataApi::dataChanged);
        QVERIFY(api.setValue(QStringLiteral("k"), 1));
        QCOMPARE(spy.count(), 1);
        QVERIFY(api.appendSample(QVariantMap{{QStringLiteral("temperature_c"), 20.0}}));
        QCOMPARE(spy.count(), 2);
    }

    // 设备来源与手工来源必须真的区分开（报告 §12.1）。
    void deviceAndManualSourcesAreDistinct()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));
        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&db);

        QVERIFY(api.appendReading(QVariantMap{
            {QStringLiteral("value"), 118.0},
            {QStringLiteral("unit"), QStringLiteral("mmHg")},
            {QStringLiteral("source"), QStringLiteral("user.entry")}}));
        QVERIFY(api.appendReading(QVariantMap{
            {QStringLiteral("value"), 121.0},
            {QStringLiteral("unit"), QStringLiteral("mmHg")},
            {QStringLiteral("source"), QStringLiteral("device.auto")}}));

        const QVariantList list = api.recentReadings(10);
        QCOMPARE(list.size(), 2);
        QStringList sources;
        for (const QVariant &item : list)
            sources.append(item.toMap().value(QStringLiteral("source")).toString());
        QVERIFY2(sources.contains(QStringLiteral("user.entry")), "manual source must be preserved");
        QVERIFY2(sources.contains(QStringLiteral("device.auto")), "device source must be preserved");
    }


    // 一次测量包含三个指标：必须存成一条记录，缺失项不补 0。
    void measurementKeepsMultipleMetrics()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));
        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&db);

        // 三个指标齐全。
        QVERIFY(api.appendMeasurement(QVariantMap{
            {QStringLiteral("systolic"), 122},
            {QStringLiteral("diastolic"), 78},
            {QStringLiteral("pulse"), 66},
            {QStringLiteral("unit"), QStringLiteral("mmHg")},
            {QStringLiteral("source"), QStringLiteral("user.entry")}}));

        // 只填收缩压：另两项必须缺失，不能变 0。
        QVERIFY(api.appendMeasurement(QVariantMap{
            {QStringLiteral("systolic"), 118},
            {QStringLiteral("source"), QStringLiteral("device.auto")}}));

        const QVariantList list = api.recentMeasurements(10);
        QCOMPARE(list.size(), 2);
        // 最新一条在前：只有收缩压。
        const QVariantMap newest = list.first().toMap();
        QCOMPARE(newest.value(QStringLiteral("systolic")).toInt(), 118);
        QVERIFY2(!newest.contains(QStringLiteral("diastolic")), "missing metric must stay absent");
        QVERIFY2(!newest.contains(QStringLiteral("pulse")), "missing metric must stay absent");
        // 上一条：三个指标齐全。
        const QVariantMap older = list.at(1).toMap();
        QCOMPARE(older.value(QStringLiteral("systolic")).toInt(), 122);
        QCOMPARE(older.value(QStringLiteral("diastolic")).toInt(), 78);
        QCOMPARE(older.value(QStringLiteral("pulse")).toInt(), 66);
    }

    // 一项都没填必须拒绝，不造空记录。
    void emptyMeasurementIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        UserDatabase db(dir.path());
        QVERIFY(db.openForSession(QStringLiteral("usera"), keyOf('a')));
        ModuleDataApi api(80, QStringLiteral("inst-1"));
        api.setDatabase(&db);

        QVERIFY(!api.appendMeasurement(QVariantMap{{QStringLiteral("source"),
                                                    QStringLiteral("user.entry")}}));
        QVERIFY(api.recentMeasurements(10).isEmpty());
    }

};

QTEST_GUILESS_MAIN(ModuleDataApiTest)
#include "module_data_api_test.moc"
