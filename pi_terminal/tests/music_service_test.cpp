#include "ui/MusicService.h"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <memory>

class MusicServiceTest : public QObject {
    Q_OBJECT
private slots:
    void staleSourcesAreDiscarded() {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, socket, [socket, buffer] {
                    *buffer += socket->readAll();
                    const auto split = buffer->indexOf("\r\n\r\n");
                    if (split < 0 || socket->property("handled").toBool()) return;
                    int size = 0;
                    for (const auto &line : buffer->left(split).split('\n'))
                        if (line.toLower().startsWith("content-length:")) size = line.mid(15).trimmed().toInt();
                    if (buffer->size() < split + 4 + size) return;
                    socket->setProperty("handled", true);
                    const auto body = buffer->mid(split + 4);
                    const bool old = body.contains("id=old");
                    const auto json = QByteArray("{\"code\":200,\"data\":[{\"url\":\"https://audio.example/") + (old ? "old" : "new") + ".mp3\",\"freeTrialInfo\":null}]}";
                    QTimer::singleShot(old ? 150 : 10, socket, [socket, json] {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json);
                        socket->disconnectFromHost();
                    });
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        MusicService service;
        service.setEndpoint(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        QSignalSpy sources(&service, &MusicService::sourceReady);
        service.resolve("old");
        service.resolve("new");
        QTRY_COMPARE_WITH_TIMEOUT(sources.count(), 1, 2000);
        QTest::qWait(250);
        QCOMPARE(sources.count(), 1);
        QCOMPARE(sources.at(0).at(0).toString(), QString("new"));
        QCOMPARE(sources.at(0).at(1).toUrl(), QUrl("https://audio.example/new.mp3"));
        service.resolve("old");
        service.cancelPlaybackRequest();
        QTest::qWait(250);
        QCOMPARE(sources.count(), 1);
    }
};
QTEST_GUILESS_MAIN(MusicServiceTest)
#include "music_service_test.moc"
