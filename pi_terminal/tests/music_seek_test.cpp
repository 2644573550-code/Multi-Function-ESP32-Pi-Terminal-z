#include <QtTest>
#include <QAudioOutput>
#include <QAudioDevice>
#include <QMediaPlayer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <functional>
#include <memory>
#include "ui/SystemTools.h"
#include "ui/MusicService.h"
#include "ui/TerminalUi.h"

// Real compressed HTTP media and production QML/cache paths, not a CLI or a
// second seek implementation. Test gain never changes the system sink volume.
class MusicSeekTests : public QObject {
    Q_OBJECT
    QByteArray media_;
    QTcpServer server_;
    QThread serverThread_;
    quint16 port_ = 0;
    std::unique_ptr<SystemTools> tools_;
    std::unique_ptr<MusicService> service_;
    std::unique_ptr<QQmlEngine> engine_;
    std::unique_ptr<QObject> window_;
    QObject *page_ = nullptr, *slider_ = nullptr;
    QMediaPlayer *player_ = nullptr;
    bool mutedCachePrimeObserved_ = false;
    bool cachePrimeGainRestored_ = false;
    bool explicitTestGain_ = false;
    qreal testOutputGain_ = 0;
    QObject *lyricLine(int index) {
        // Popup delegates belong to the Overlay visual tree, not necessarily
        // the ApplicationWindow QObject ownership tree.
        std::function<QObject *(QQuickItem *)> find=[&](QQuickItem *item)->QObject * {
            if(item->objectName()=="musicLyricLine" && item->property("index").toInt()==index)return item;
            for(auto *child:item->childItems())if(auto *found=find(child))return found;
            return nullptr;
        };
        return find(qobject_cast<QQuickWindow *>(window_.get())->contentItem());
    }

    void normalClock() {
        QTest::qWait(700); // Exclude the seek/preroll transition itself.
        QElapsedTimer clock; clock.start();
        const auto start = player_->position();
        QTest::qWait(2400);
        const double ratio = double(player_->position() - start) / clock.elapsed();
        qInfo() << "HTTP_MP3_CLOCK" << "ratio" << ratio << "rate" << player_->playbackRate();
        QVERIFY2(ratio > 0.75 && ratio < 1.25, qPrintable(QString::number(ratio)));
        QCOMPARE(player_->playbackRate(), 1.0);
    }
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Material");
        initializeTerminalUi();
        QTemporaryDir fixture;
        auto path=qEnvironmentVariable("TERMINAL_MUSIC_TEST_FIXTURE");
        if(path.isEmpty()) {
            path=fixture.filePath("seek.mp3");
            QProcess encoder;
            encoder.start("ffmpeg", {"-v","error","-f","lavfi","-i","sine=frequency=440:sample_rate=44100", "-t","90","-c:a","libmp3lame","-b:a","128k",path});
            QVERIFY(encoder.waitForFinished(60000));
            QVERIFY2(encoder.exitCode() == 0, encoder.readAllStandardError().constData());
        }
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); media_=file.readAll();
        connect(&server_, &QTcpServer::newConnection, &server_, [this] {
            while(server_.hasPendingConnections()) {
                auto *socket=server_.nextPendingConnection();
                auto request=std::make_shared<QByteArray>();
                connect(socket,&QTcpSocket::readyRead,socket,[this,socket,request] {
                    *request+=socket->readAll();
                    if(!request->contains("\r\n\r\n") || socket->property("handled").toBool())return;
                    socket->setProperty("handled",true);
                    qInfo() << "HTTP_REQUEST" << request->split('\n').first();
                    const auto range=QRegularExpression("Range: bytes=(\\d+)-(\\d*)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(*request));
                    const qint64 first=range.hasMatch()?range.captured(1).toLongLong():0;
                    const qint64 last=range.hasMatch()&&!range.captured(2).isEmpty()?qMin(qint64(media_.size()-1),range.captured(2).toLongLong()):media_.size()-1;
                    if(first>last) {socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();return;}
                    QByteArray response=range.hasMatch()?"HTTP/1.1 206 Partial Content\r\n":"HTTP/1.1 200 OK\r\n";
                    response+="Accept-Ranges: bytes\r\nContent-Type: audio/mpeg\r\nConnection: close\r\nContent-Length: "+QByteArray::number(last-first+1)+"\r\n";
                    if(range.hasMatch())response+="Content-Range: bytes "+QByteArray::number(first)+"-"+QByteArray::number(last)+"/"+QByteArray::number(media_.size())+"\r\n";
                    response+="\r\n";
                    if(!request->startsWith("HEAD "))response+=media_.mid(first,last-first+1);
                    socket->write(response);socket->disconnectFromHost();
                });
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
        // Qt 6.5 FFmpeg can synchronously open HTTP during setSource(). A
        // same-thread fixture server would deadlock instead of testing seeks.
        server_.moveToThread(&serverThread_); serverThread_.start();
        bool listening=false;
        QMetaObject::invokeMethod(&server_,[&]{listening=server_.listen(QHostAddress::LocalHost);port_=server_.serverPort();},Qt::BlockingQueuedConnection);
        QVERIFY(listening);
    }
    void cleanupTestCase() {
        if(!serverThread_.isRunning())return;
        QMetaObject::invokeMethod(&server_,[this]{
            server_.close();
            for(auto *socket:server_.findChildren<QTcpSocket *>())delete socket;
            server_.moveToThread(QCoreApplication::instance()->thread());
        },Qt::BlockingQueuedConnection);
        serverThread_.quit();serverThread_.wait();
    }
    void init() {
        tools_=std::make_unique<SystemTools>(); service_=std::make_unique<MusicService>();
        engine_=std::make_unique<QQmlEngine>(); engine_->addImportPath("qrc:/qt/qml");
        engine_->rootContext()->setContextProperty("systemTools",tools_.get());
        engine_->rootContext()->setContextProperty("musicService",service_.get());
        QQmlComponent component(engine_.get());
        component.setData(R"(
import QtQuick
import QtQuick.Controls
import OrangePiTerminal
ApplicationWindow {
    width: 1024; height: 600; visible: true
    MusicPlayer { anchors.fill: parent; autoNetworkMode: false; mode: "online"; onlinePlayback: true
        auditLyrics: [{time:6000,text:"Backward lyric",translation:""},{time:45000,text:"Forward lyric",translation:""}]
    }
})",QUrl());
        window_.reset(component.create());
        QVERIFY2(window_,qPrintable(component.errorString()));
        page_=window_->findChild<QObject *>("musicPlayerPage");
        player_=window_->findChild<QMediaPlayer *>("musicMediaPlayer");
        slider_=window_->findChild<QObject *>("musicSeekSlider");
        QVERIFY(page_ && player_ && slider_ && player_->audioOutput());
        QVERIFY(page_->setProperty("pendingOnlineId",QStringLiteral("cache-fixture-1")));
        QCOMPARE(page_->property("onlinePlayback").toBool(),true);
        connect(player_,&QMediaPlayer::errorOccurred,this,[](QMediaPlayer::Error, const QString &message){qWarning()<<"HTTP_MEDIA_ERROR"<<message;});
        connect(player_,&QMediaPlayer::mediaStatusChanged,this,[this](QMediaPlayer::MediaStatus status){qInfo()<<"HTTP_STATUS"<<status<<player_->duration()<<player_->position()<<player_->errorString();});
        // Default to silent host tests; Pi can opt into a quiet sine tone to
        // exercise the real audio sink without changing system volume.
        bool volumeOk=false;
        const qreal testVolume=qEnvironmentVariable("TERMINAL_MUSIC_TEST_OUTPUT_GAIN").toDouble(&volumeOk);
        const qreal requestedGain=volumeOk?qBound(0.0,testVolume,0.25):0.0;
        explicitTestGain_=volumeOk;testOutputGain_=requestedGain;
        QVERIFY(page_->setProperty("volume",requestedGain));
        mutedCachePrimeObserved_=false;cachePrimeGainRestored_=false;
        connect(player_->audioOutput(),&QAudioOutput::volumeChanged,this,[this](float value){
            if(page_->property("cachePrimeMuted").toBool() && value<=0.0001f)mutedCachePrimeObserved_=true;
            if(mutedCachePrimeObserved_ && !page_->property("cachePrimeMuted").toBool() && value>0.0001f)cachePrimeGainRestored_=true;
        });
        const auto outputDevice=player_->audioOutput()->device();
        qInfo() << "HTTP_AUDIO_OUTPUT" << outputDevice.description() << "null" << outputDevice.isNull()
                << "gain" << player_->audioOutput()->volume();
        if(volumeOk) QVERIFY2(!outputDevice.isNull(),"Audible Pi test requested but Qt reports no audio output device");
        const QUrl httpSource(QString("http://127.0.0.1:%1/seek.mp3").arg(port_));
        page_->setProperty("onlineSourceUrl",httpSource);
        player_->setSource(httpSource);
        player_->play();
        QTRY_VERIFY_WITH_TIMEOUT(player_->isSeekable() && player_->duration()>60000 && player_->position()>200,15000);
        QCOMPARE(player_->playbackState(),QMediaPlayer::PlayingState);
    }
    void cleanup() {
        if(player_)player_->stop();
        window_.reset();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        engine_.reset();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        service_.reset();tools_.reset();
        page_=slider_=nullptr;player_=nullptr;
    }
    void dragOnlyPreviewsUntilRelease() {
        QSignalSpy states(player_,&QMediaPlayer::playbackStateChanged);
        bool pausedWhileRemote=false;
        connect(player_,&QMediaPlayer::playbackStateChanged,this,[this,&pausedWhileRemote](QMediaPlayer::PlaybackState state){
            if(state==QMediaPlayer::PausedState && !player_->source().isLocalFile())pausedWhileRemote=true;
        });
        QSignalSpy cacheReady(service_.get(),&MusicService::seekAudioCacheReady);
        QSignalSpy cacheFailed(service_.get(),&MusicService::seekAudioCacheFailed);
        QVERIFY(slider_->setProperty("pressed",true));
        QVERIFY(slider_->setProperty("value",30000));
        QVERIFY(QMetaObject::invokeMethod(slider_,"moved"));
        QVERIFY(page_->property("seeking").toBool());
        QTest::qWait(350); // Original debounce incorrectly commits while held.
        QVERIFY2(player_->position()<5000,"Dragging committed a real HTTP seek before release");
        slider_->setProperty("value",45000);QMetaObject::invokeMethod(slider_,"moved");
        QTest::qWait(200);
        QVERIFY(player_->position()<5000);
        QVERIFY(slider_->setProperty("pressed",false));
        QVERIFY(!player_->source().isLocalFile()); // Keep old stream intact until full-cache commit.
        QTRY_VERIFY_WITH_TIMEOUT(cacheReady.count() + cacheFailed.count() > 0,15000);
        if(cacheFailed.count())qInfo()<<"CACHE_FAILED"<<cacheFailed.constFirst();
        QCOMPARE(cacheReady.count(),1);
        QTRY_VERIFY_WITH_TIMEOUT(player_->source().isLocalFile(),15000);
        QCOMPARE(cacheReady.count(),1);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player_->position()-45000)<1500,5000);
        QVERIFY(QFileInfo(player_->source().toLocalFile()).size() >= media_.size());
        normalClock();
        if(explicitTestGain_ && testOutputGain_>0) {
            QVERIFY(mutedCachePrimeObserved_);
            QVERIFY(cachePrimeGainRestored_);
        }
        int pauses=0;for(const auto &row:states)if(row[0].toInt()==QMediaPlayer::PausedState)++pauses;
        QVERIFY(pauses>=1); // Local cache priming pauses only the muted local pipeline.
        QVERIFY(!pausedWhileRemote);
        QCOMPARE(player_->playbackState(),QMediaPlayer::PlayingState);
    }
    void lyricStyleJumpsAndPausedSeek() {
        auto *dialog=window_->findChild<QObject *>("musicLyricsDialog");QVERIFY(dialog);
        page_->setProperty("currentTitle",QString(240,QChar('T')));
        page_->setProperty("currentArtist",QString(160,QChar('A')));
        page_->setProperty("currentAlbum",QString(1200,QChar('L')));
        QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
        QTRY_VERIFY(lyricLine(0) && lyricLine(1));
        for(const auto &name:{"lyricsTranslationButton","lyricsCloseButton"}) {
            auto *button=window_->findChild<QQuickItem *>(name);QVERIFY(button);QVERIFY(button->isVisible());
            auto *header=button->parentItem();QVERIFY(header);
            const QPointF pos=button->mapToItem(header,QPointF());
            QVERIFY(pos.x()>=0&&pos.x()+button->width()<=header->width()+1);
        }
        auto *metadata=window_->findChild<QQuickItem *>("lyricsTrackMetadata");QVERIFY(metadata);
        QVERIFY(metadata->height()<=qCeil(page_->property("secondaryTextSize").toDouble()*1.35)*3+1);
        auto *playback=window_->findChild<QQuickItem *>("lyricsPlaybackControls");QVERIFY(playback);
        QVERIFY(playback->height()>=48);
        QVERIFY(playback->y()+playback->height()<=playback->parentItem()->height()+1);
        QSignalSpy states(player_,&QMediaPlayer::playbackStateChanged);
        bool pausedWhileRemote=false;
        connect(player_,&QMediaPlayer::playbackStateChanged,this,[this,&pausedWhileRemote](QMediaPlayer::PlaybackState state){
            if(state==QMediaPlayer::PausedState && !player_->source().isLocalFile())pausedWhileRemote=true;
        });
        QSignalSpy cacheReady(service_.get(),&MusicService::seekAudioCacheReady);
        QSignalSpy cacheFailed(service_.get(),&MusicService::seekAudioCacheFailed);
        // Repeated lyric clicks coalesce to one full-track cache operation;
        // playback switches only after the local copy is ready.
        QVERIFY(QMetaObject::invokeMethod(lyricLine(0),"clicked"));
        QVERIFY(QMetaObject::invokeMethod(lyricLine(1),"clicked"));
        QVERIFY(!player_->source().isLocalFile());
        QTRY_VERIFY_WITH_TIMEOUT(cacheReady.count() + cacheFailed.count() > 0,15000);
        if(cacheFailed.count())qInfo()<<"CACHE_FAILED"<<cacheFailed.constFirst();
        QCOMPARE(cacheReady.count(),1);
        QTRY_VERIFY_WITH_TIMEOUT(player_->source().isLocalFile(),15000);
        QCOMPARE(cacheReady.count(),1);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player_->position()-45000)<1500,5000);
        QVERIFY(QFileInfo(player_->source().toLocalFile()).size() >= media_.size());
        normalClock();
        if(explicitTestGain_ && testOutputGain_>0) {
            QVERIFY(mutedCachePrimeObserved_);
            QVERIFY(cachePrimeGainRestored_);
        }
        int pauses=0;for(const auto &row:states)if(row[0].toInt()==QMediaPlayer::PausedState)++pauses;
        QVERIFY(pauses>=1);
        QVERIFY(!pausedWhileRemote);
        QCOMPARE(player_->playbackState(),QMediaPlayer::PlayingState);
        QVERIFY(QMetaObject::invokeMethod(lyricLine(0),"clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player_->position()-6000)<1500,5000);
        normalClock();
        QCOMPARE(player_->playbackState(),QMediaPlayer::PlayingState);
        QVERIFY(!pausedWhileRemote);
        player_->pause();
        slider_->setProperty("value",20000); // keyboard/a11y movement, no press
        QVERIFY(QMetaObject::invokeMethod(slider_,"moved"));
        QTest::qWait(1800);
        QCOMPARE(player_->playbackState(),QMediaPlayer::PausedState);
        QVERIFY(qAbs(player_->position()-20000)<1000);
    }
    void completedTrackCacheSurvivesServiceRecreation() {
        const QUrl source(QString("http://127.0.0.1:%1/seek.mp3").arg(port_));
        QSignalSpy completed(service_.get(),&MusicService::seekAudioCacheReady);
        QSignalSpy failed(service_.get(),&MusicService::seekAudioCacheFailed);
        service_->cacheAudioForSeek(QStringLiteral("persistent-cache-fixture"),source.toString());
        QTRY_VERIFY_WITH_TIMEOUT(completed.count()+failed.count()>0,15000);
        QCOMPARE(failed.count(),0);
        QCOMPARE(completed.count(),1);
        const QUrl cached=completed.constFirst().at(1).toUrl();
        QVERIFY(cached.isLocalFile());
        QCOMPARE(QFileInfo(cached.toLocalFile()).size(),media_.size());

        MusicService reopened;
        QCOMPARE(reopened.cachedAudioForTrack(QStringLiteral("persistent-cache-fixture")),cached);
        QSignalSpy reused(&reopened,&MusicService::seekAudioCacheReady);
        reopened.cacheAudioForSeek(QStringLiteral("persistent-cache-fixture"),source.toString());
        QCOMPARE(reused.count(),1); // Existing complete cache resolves without another wait.
        QCOMPARE(reused.constFirst().at(1).toUrl(),cached);
    }
};

int main(int argc,char **argv) {
    QTemporaryDir state;
    qputenv("HOME",state.path().toUtf8());
    for(const auto &kind:{"CONFIG","DATA","STATE","CACHE"})qputenv((QByteArray("XDG_")+kind+"_HOME").constData(),state.filePath(kind).toUtf8());
    QGuiApplication app(argc,argv);
    MusicSeekTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "music_seek_test.moc"
