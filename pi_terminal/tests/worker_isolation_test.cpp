// IT-013 / T-11：worker IPC 边界与 unit 配置。
// 真 UID / 拒网 / systemd 生效必须在 Pi 上验（本机无 nmcli，也可能无 systemd session）。
#include "core/ModuleWorkerLauncher.h"
#include "core/WorkerIpc.h"

#include <QFile>
#include <QJsonDocument>
#include <QtTest>


class WorkerIsolationTest : public QObject {
    Q_OBJECT

private slots:
    void encodeDecodeRoundTrip() {
        const QJsonObject request = WorkerIpc::request(7, QStringLiteral("publishSample"),
                                                       {{"temperature_C", 21.5}});
        const QByteArray frame = WorkerIpc::encode(request);
        QVERIFY(frame.size() > 4);
        // 长度前缀是小端 u32。
        const quint32 declared = quint32(frame[0]) | (quint32(frame[1]) << 8)
            | (quint32(frame[2]) << 16) | (quint32(frame[3]) << 24);
        QCOMPARE(int(declared), frame.size() - 4);

        const QJsonObject decoded = WorkerIpc::decode(frame);
        QCOMPARE(decoded.value("schema").toString(), QString("p0-worker-v1"));
        QCOMPARE(decoded.value("op").toString(), QString("publishSample"));
        QCOMPARE(decoded.value("request_id").toInt(), 7);
    }

    void oversizedMessageIsRejected() {
        QByteArray payload(int(WorkerIpc::kMaxMessageBytes) + 100, 'x');
        QByteArray frame(4, '\0');
        const quint32 size = quint32(payload.size());
        frame[0] = char(size & 0xff); frame[1] = char((size >> 8) & 0xff);
        frame[2] = char((size >> 16) & 0xff); frame[3] = char((size >> 24) & 0xff);
        frame.append(payload);
        QVERIFY(WorkerIpc::decode(frame).isEmpty());
    }

    void lengthMismatchIsRejected() {
        const QByteArray frame = WorkerIpc::encode(WorkerIpc::request(1, "stop", {}));
        QByteArray truncated = frame;
        truncated.chop(3);
        // 声明长度与实际不符必须拒绝，不得按声明去截断解析。
        QVERIFY(WorkerIpc::decode(truncated).isEmpty());
    }

    void unknownSchemaIsRejected() {
        const QJsonObject bad{{"schema", "p0-worker-v2"}, {"request_id", 1}, {"op", "stop"}, {"body", {}}};
        QVERIFY(WorkerIpc::decode(WorkerIpc::encode(bad)).isEmpty());
    }

    void deepNestingIsRejected() {
        QJsonObject nested;
        for (int i = 0; i < 12; ++i) nested = QJsonObject{{"n", nested}};
        const QJsonObject bad{{"schema", "p0-worker-v1"}, {"request_id", 1},
                              {"op", "stop"}, {"body", nested}};
        QVERIFY(WorkerIpc::decode(WorkerIpc::encode(bad)).isEmpty());
    }

    void opWhitelistIsEnforced() {
        QVERIFY(WorkerIpc::opAllowed(QStringLiteral("publishSample")));
        QVERIFY(WorkerIpc::opAllowed(QStringLiteral("readOwnRecords")));
        QVERIFY(WorkerIpc::opAllowed(QStringLiteral("reportState")));
        QVERIFY(WorkerIpc::opAllowed(QStringLiteral("stop")));
        // 白名单之外一律拒绝，不允许通过请求体扩展能力。
        QVERIFY(!WorkerIpc::opAllowed(QStringLiteral("readAnyRecords")));
        QVERIFY(!WorkerIpc::opAllowed(QStringLiteral("execSql")));
        QVERIFY(!WorkerIpc::opAllowed(QString()));
    }

    void unitNameIsSanitized() {
        const QString unit = ModuleWorkerLauncher::unitName(QStringLiteral("sess/../evil"));
        QVERIFY(!unit.contains(QStringLiteral("..")));
        QVERIFY(!unit.contains(QLatin1Char('/')));
        QVERIFY(unit.startsWith(QStringLiteral("orange-pi-worker@")));
    }

    void launcherUsesSystemctlNotRawProcess() {
        const QStringList command = ModuleWorkerLauncher::startCommand(QStringLiteral("s1"));
        // 必须经 systemctl：直接起二进制会绕过 DynamicUser/PrivateNetwork。
        QCOMPARE(command.first(), QStringLiteral("systemctl"));
        QCOMPARE(command.at(1), QStringLiteral("start"));
        QVERIFY(command.at(2).contains(QStringLiteral("orange-pi-worker@")));
    }

    void unitFileDeclaresRequiredIsolation() {
        const QString path = QStringLiteral(WORKER_UNIT_PATH);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const QString text = QString::fromUtf8(file.readAll());
        // §0.4 的六条必须全部在 unit 里真正声明。
        for (const auto *directive : {"DynamicUser=yes", "PrivateNetwork=yes", "PrivateTmp=yes",
                                      "ProtectHome=yes", "ProtectSystem=strict", "NoNewPrivileges=yes",
                                      "RestrictAddressFamilies=AF_UNIX"}) {
            QVERIFY2(text.contains(QLatin1String(directive)), directive);
        }
        // DynamicUser 下不得手工改属主：只检查真正的指令行，注释里的说明不算。
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1Char('#'))) continue;
            QVERIFY2(!trimmed.contains(QStringLiteral("chown")), qPrintable(trimmed));
        }
    }
};

QTEST_GUILESS_MAIN(WorkerIsolationTest)
#include "worker_isolation_test.moc"
