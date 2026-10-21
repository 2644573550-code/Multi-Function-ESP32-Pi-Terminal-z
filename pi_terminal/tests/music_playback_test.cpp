#include "ui/MusicPlayback.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

class MusicPlaybackTest : public QObject {
    Q_OBJECT
private slots:
    void liveCliSwitching() {
        const auto path = qEnvironmentVariable("NCM_LIVE_TRACKS");
        if (path.isEmpty()) QSKIP("Opt-in real-account test: set NCM_LIVE_TRACKS and NCM_LIVE_PROGRAM");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto tracks = QJsonDocument::fromJson(file.readAll()).array();
        QVERIFY(tracks.size() >= 3);
        MusicPlayback playback;
        playback.configure(qEnvironmentVariable("NCM_LIVE_PROGRAM"), QProcessEnvironment::systemEnvironment());
        for (const auto &value : tracks) {
            const auto song = value.toObject();
            QVERIFY(playback.play({"play", "--song", "--encrypted-id", song.value("id").toString(),
                "--original-id", QString::number(static_cast<qint64>(song.value("originalId").toDouble()))}));
            QTRY_VERIFY_WITH_TIMEOUT(playback.progress() == 100 || !playback.error().isEmpty(), 25000);
            QVERIFY2(playback.error().isEmpty(), qPrintable(playback.error()));
            QVERIFY(playback.state().value("title").toString().contains(song.value("name").toString()));
            QTest::qWait(1500);
        }
        QVERIFY(playback.control("stop"));
        QTest::qWait(1000);
    }
    void latestSelectionWins() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile cli(directory.filePath("cli.py"));
        QVERIFY(cli.open(QIODevice::WriteOnly));
        cli.write(R"(#!/usr/bin/env python3
import sys, os, json, time
p = os.path.dirname(__file__)
action = sys.argv[1]
with open(p + '/commands', 'a') as f: f.write(' '.join(sys.argv[1:]) + '\n')
if action == 'stop':
    time.sleep(.15)
    with open(p + '/song', 'w') as f: f.write('')
    print('{"success":true}')
elif action == 'play':
    song = sys.argv[-1]
    if song == 'bad':
        print('{"success":false}')
    else:
        with open(p + '/song', 'w') as f: f.write(song)
        print('{"success":true}')
        if song.startswith('attached'): time.sleep(60)
elif action == 'state':
    with open(p + '/song') as f: song = f.read()
    print(json.dumps({'state': {'title': song, 'status':'playing' if song else 'stopped','position':1,'duration':200}}))
else:
    print('{"success":true}')
)");
        cli.close();
        QVERIFY(cli.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        MusicPlayback playback;
        playback.configure(cli.fileName(), QProcessEnvironment::systemEnvironment());
        QVERIFY(playback.play({"play", "--original-id", "A"}));
        QTest::qWait(30);
        QVERIFY(playback.play({"play", "--original-id", "B"}));
        QVERIFY(playback.play({"play", "--original-id", "C"}));
        QVERIFY(!playback.control("seek", "90"));
        QTRY_COMPARE_WITH_TIMEOUT(playback.progress(), 100, 5000);
        QCOMPARE(playback.state().value("title").toString(), QString("C"));
        QFile log(directory.filePath("commands"));
        QVERIFY(log.open(QIODevice::ReadOnly));
        const auto commands = log.readAll();
        QVERIFY(commands.contains("play --original-id C"));
        QVERIFY(!commands.contains("play --original-id A"));
        QVERIFY(!commands.contains("play --original-id B"));
        QVERIFY(playback.play({"play", "--original-id", "D"}));
        QVERIFY(playback.state().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(playback.progress(), 100, 5000);
        QCOMPARE(playback.state().value("title").toString(), QString("D"));
        QVERIFY(playback.play({"play", "--original-id", "attached1"}));
        QTRY_COMPARE_WITH_TIMEOUT(playback.progress(), 100, 5000);
        QCOMPARE(playback.state().value("title").toString(), QString("attached1"));
        QVERIFY(playback.play({"play", "--original-id", "attached2"}));
        QTRY_COMPARE_WITH_TIMEOUT(playback.progress(), 100, 5000);
        QCOMPARE(playback.state().value("title").toString(), QString("attached2"));
        QVERIFY(playback.play({"play", "--original-id", "bad"}));
        QTRY_VERIFY_WITH_TIMEOUT(!playback.error().isEmpty(), 5000);
        QCOMPARE(playback.progress(), 0);
    }
};
QTEST_GUILESS_MAIN(MusicPlaybackTest)
#include "music_playback_test.moc"
