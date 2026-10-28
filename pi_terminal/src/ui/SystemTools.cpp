#include "SystemTools.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QTimer>
#include <QProcess>
#include <QDateTime>
#include <QSaveFile>
#include <QStorageInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QImageReader>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QNetworkReply>
#include <QHash>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTextStream>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <cmath>
QString SystemTools::saveCameraFrame(const QString &frame, const QUrl &destination) {
    const auto path=destination.isLocalFile()?destination.toLocalFile():destination.scheme().isEmpty()?destination.toString():QString{};
    if (!QDir::isAbsolutePath(path)) return "请输入本机文件的完整路径";
    if (!frame.startsWith("data:image/png;base64,") || frame.size()>8*1024*1024) return "照片数据无效";
    const auto decoded=QByteArray::fromBase64Encoding(frame.mid(22).toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || QImage::fromData(decoded.decoded,"PNG").isNull()) return "PNG 照片无法解码";
    if (QFileInfo::exists(path)) return "同名文件已存在，请更换文件名";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(decoded.decoded)!=decoded.decoded.size() || !file.commit())
        return "保存失败："+file.errorString();
    return {};
}
QString SystemTools::cameraPhotoPath() const {
    auto directory=QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if(!QFileInfo(directory).isDir())directory=QDir::homePath();
    return QDir(directory).filePath("camera-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".png");
}

bool SystemTools::tvAutoplay() const {
    return qEnvironmentVariableIsSet("TERMINAL_TV_AUTOPLAY");
}

QString SystemTools::tvChannelData() const {
    QFile file(QStringLiteral(":/tv/china.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("内置电视频道表缺失: %s", qPrintable(file.fileName()));
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QVariantMap SystemTools::localMusicMetadata(const QString &relative) const {
    if (localMusicMetadataCache_.contains(relative)) return localMusicMetadataCache_.value(relative);
    const auto path = safePath(relative);
    if (path.isEmpty() || !QFileInfo(path).isFile()) return {};
    const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty()) return QVariantMap{{QStringLiteral("title"), QFileInfo(path).completeBaseName()}};
    QProcess probe;
    probe.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_entries"), QStringLiteral("format_tags:format=duration:stream=codec_type,disposition:stream_tags"), path});
    if (!probe.waitForFinished(2500)) { probe.kill(); return {}; }
    const auto object = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    const auto format = object.value(QStringLiteral("format")).toObject();
    const auto tags = format.value(QStringLiteral("tags")).toObject();
    auto tag = [&tags](const QStringList &keys) {
        for (const auto &key : keys) if (!tags.value(key).toString().isEmpty()) return tags.value(key).toString();
        return QString{};
    };
    QVariantMap result{
        {QStringLiteral("title"), tag({QStringLiteral("title"), QStringLiteral("TITLE")})},
        {QStringLiteral("artist"), tag({QStringLiteral("artist"), QStringLiteral("ARTIST"), QStringLiteral("album_artist"), QStringLiteral("ALBUM_ARTIST")})},
        {QStringLiteral("album"), tag({QStringLiteral("album"), QStringLiteral("ALBUM")})},
        {QStringLiteral("genre"), tag({QStringLiteral("genre"), QStringLiteral("GENRE")})},
        {QStringLiteral("year"), tag({QStringLiteral("date"), QStringLiteral("DATE"), QStringLiteral("year"), QStringLiteral("YEAR")})},
        {QStringLiteral("duration"), format.value(QStringLiteral("duration")).toString().toDouble() * 1000.0}
    };
    if (result.value(QStringLiteral("title")).toString().isEmpty()) result.insert(QStringLiteral("title"), QFileInfo(path).completeBaseName());
    const auto coverDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/music-covers");
    const auto key = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex();
    const auto cover = QDir(coverDir).filePath(QString::fromLatin1(key) + QStringLiteral(".jpg"));
    if (!QFileInfo::exists(cover)) {
        const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (!ffmpeg.isEmpty()) {
            QDir().mkpath(coverDir);
            QProcess extract;
            extract.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-i"), path,
                QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-frames:v"), QStringLiteral("1"), cover});
            extract.waitForFinished(4000);
            if (extract.exitStatus() != QProcess::NormalExit || extract.exitCode() != 0) QFile::remove(cover);
        }
    }
    if (QFileInfo(cover).isFile()) result.insert(QStringLiteral("coverUrl"), QUrl::fromLocalFile(cover).toString());
    localMusicMetadataCache_.insert(relative, result);
    return result;
}
QString musicCliExecutable() {
    const auto fromPath=QStandardPaths::findExecutable(QStringLiteral("ncm-cli"));
    if(!fromPath.isEmpty())return fromPath;
    const QStringList candidates={
        QDir::home().filePath(QStringLiteral(".local/opt/ncm-cli/bin/ncm-cli")),
        QDir::home().filePath(QStringLiteral(".npm-global/bin/ncm-cli")),
        QDir::home().filePath(QStringLiteral(".local/bin/ncm-cli"))
    };
    for(const auto &candidate:candidates)if(QFileInfo(candidate).isExecutable())return candidate;
    return {};
}
QString musicCliPathEnvironment() {
    const auto nodeBin=QDir::home().filePath(QStringLiteral(".local/opt/node-v24.22.0/bin"));
    const auto cliBin=QDir::home().filePath(QStringLiteral(".local/opt/ncm-cli/bin"));
    const auto mpvBin=QDir::home().filePath(QStringLiteral(".local/opt/mpv/usr/bin"));
    return nodeBin+QLatin1Char(':')+cliBin+QLatin1Char(':')+mpvBin+QLatin1Char(':')+qEnvironmentVariable("PATH");
}
QString localMpvLibraryDirectory() {
    auto arch=QSysInfo::currentCpuArchitecture();
    if(arch==QStringLiteral("arm64") || arch.startsWith(QStringLiteral("armv8")))arch=QStringLiteral("aarch64");
    return QDir::home().filePath(QStringLiteral(".local/opt/mpv/usr/lib/%1-linux-gnu").arg(arch));
}
bool SystemTools::musicCliAvailable() const {
    return !musicCliExecutable().isEmpty();
}
void SystemTools::probeMusicNetwork() {
    QNetworkRequest request(QUrl(QStringLiteral("https://www.baidu.com/")));
    request.setTransferTimeout(2500);
    auto *reply=musicNetworkManager_.head(request);
    connect(reply,&QNetworkReply::finished,this,[this,reply] {
        const bool online=reply->error()==QNetworkReply::NoError;
        reply->deleteLater();
        if(musicNetworkOnline_==online)return;
        musicNetworkOnline_=online; emit musicNetworkChanged();
    });
}
bool SystemTools::prepareMusicCliProcess() {
    if(musicCliProcess_.state()==QProcess::NotRunning)return true;
    // Library requests may not interrupt another request or playback transport.
    return false;
}
bool SystemTools::musicCliHasNextPlaylistPage() const {
    if(musicCliPlaylistId_.isEmpty() || musicCliTracks_.size()<50 || musicCliPlaylistOffset_+musicCliTracks_.size()>=500)return false;
    return musicCliPlaylistTotal_<=0 || musicCliPlaylistOffset_+musicCliTracks_.size()<musicCliPlaylistTotal_;
}
bool SystemTools::launchMusicCliSetup(const QString &action) {
    if(action!=QStringLiteral("configure") && action!=QStringLiteral("login"))return false;
    if(!musicCliAvailable()) {
        musicCliError_=QStringLiteral("ncm-cli is not installed. Install Node.js 18+ and @music163/ncm-cli first.");
        emit musicCliChanged();
        return false;
    }
    const auto terminal=QStandardPaths::findExecutable(QStringLiteral("x-terminal-emulator"));
    if(terminal.isEmpty()) {
        musicCliError_=QStringLiteral("No terminal emulator was found to run the official interactive CLI.");
        emit musicCliChanged();
        return false;
    }
    const auto title=action==QStringLiteral("configure")?QStringLiteral("NetEase CLI setup"):QStringLiteral("NetEase QR login");
    const auto mpvLibArch=QFileInfo(localMpvLibraryDirectory()).fileName();
    const auto script=QStringLiteral("export PATH=\"$HOME/.local/opt/node-v24.22.0/bin:$HOME/.local/opt/ncm-cli/bin:$HOME/.local/opt/mpv/usr/bin:$PATH\"; export LD_LIBRARY_PATH=\"$HOME/.local/opt/mpv/usr/lib/%3:${LD_LIBRARY_PATH:-}\"; ncm-cli %1; rc=$?; echo; echo '%2'; read -r -p 'Press Enter to close...' _; exit $rc")
        .arg(action,title,mpvLibArch);
    qint64 pid=0;
    const bool started=QProcess::startDetached(terminal,{QStringLiteral("-T"),title,QStringLiteral("-e"),QStringLiteral("/bin/bash"),QStringLiteral("-lc"),script},QDir::homePath(),&pid);
    if(!started)musicCliError_=QStringLiteral("Could not open a terminal for the official CLI.");
    else musicCliError_.clear();
    emit musicCliChanged();
    return started;
}
bool SystemTools::musicCliSearch(const QString &keyword,const QString &type) {
    const auto executable=musicCliExecutable();
    if(executable.isEmpty()) {
        musicCliError_=QStringLiteral("ncm-cli is not installed."); emit musicCliChanged(); return false;
    }
    if(keyword.trimmed().isEmpty())return false;
    if(type!=QStringLiteral("song") && type!=QStringLiteral("playlist"))return false;
    if(!prepareMusicCliProcess())return false;
    musicCliAction_=QStringLiteral("search"); musicCliSearchType_=type; musicCliOutput_.clear(); musicCliError_.clear(); musicCliTracks_.clear();
    auto env=QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"),musicCliPathEnvironment());
    const auto mpvLib=localMpvLibraryDirectory();
    env.insert(QStringLiteral("LD_LIBRARY_PATH"),mpvLib+QLatin1Char(':')+env.value(QStringLiteral("LD_LIBRARY_PATH")));
    musicCliProcess_.setProcessEnvironment(env);
    musicCliProcess_.setProgram(executable);
    musicCliProcess_.setArguments({QStringLiteral("search"),type,QStringLiteral("--keyword"),keyword.trimmed(),QStringLiteral("--limit"),QStringLiteral("20"),QStringLiteral("--output"),QStringLiteral("json")});
    musicCliProcess_.start();
    emit musicCliChanged();
    return true;
}
bool SystemTools::musicCliControl(const QString &action) {
    if(action==QStringLiteral("state") || action==QStringLiteral("pause") || action==QStringLiteral("resume") || action==QStringLiteral("stop"))
        return musicPlayback_.control(action);
    const auto executable=musicCliExecutable();
    if(executable.isEmpty()) {
        musicCliError_=QStringLiteral("ncm-cli is not installed."); emit musicCliChanged(); return false;
    }
    QStringList args;
    if(action==QStringLiteral("pause") || action==QStringLiteral("resume") || action==QStringLiteral("stop") ||
       action==QStringLiteral("next") || action==QStringLiteral("prev") || action==QStringLiteral("state") ||
       action==QStringLiteral("queue"))args.append(action);
    else if(action==QStringLiteral("logout"))args={QStringLiteral("logout"),QStringLiteral("--output"),QStringLiteral("json")};
    else if(action==QStringLiteral("recommend"))args={QStringLiteral("recommend"),QStringLiteral("daily"),QStringLiteral("--limit"),QStringLiteral("20")};
    else return false;
    if(!prepareMusicCliProcess())return false;
    musicCliAction_=action; musicCliOutput_.clear(); musicCliError_.clear();
    auto env=QProcessEnvironment::systemEnvironment(); env.insert(QStringLiteral("PATH"),musicCliPathEnvironment());
    const auto mpvLib=localMpvLibraryDirectory();
    env.insert(QStringLiteral("LD_LIBRARY_PATH"),mpvLib+QLatin1Char(':')+env.value(QStringLiteral("LD_LIBRARY_PATH")));
    musicCliProcess_.setProcessEnvironment(env); musicCliProcess_.setProgram(executable); musicCliProcess_.setArguments(args); musicCliProcess_.start();
    emit musicCliChanged(); return true;
}
bool SystemTools::musicCliPlaylists(const QString &scope) {
    if(scope!=QStringLiteral("created") && scope!=QStringLiteral("collected"))return false;
    const auto executable=musicCliExecutable();
    if(executable.isEmpty()) { musicCliError_=QStringLiteral("ncm-cli is not installed."); emit musicCliChanged(); return false; }
    if(!prepareMusicCliProcess())return false;
    musicCliAction_=QStringLiteral("playlistList"); musicCliSearchType_=QStringLiteral("playlist"); musicCliPlaylistId_.clear(); musicCliPlaylistOffset_=0; musicCliPlaylistTotal_=0; musicCliOutput_.clear(); musicCliError_.clear(); musicCliTracks_.clear();
    auto env=QProcessEnvironment::systemEnvironment(); env.insert(QStringLiteral("PATH"),musicCliPathEnvironment());
    musicCliProcess_.setProcessEnvironment(env); musicCliProcess_.setProgram(executable);
    musicCliProcess_.setArguments({QStringLiteral("playlist"),scope,QStringLiteral("--limit"),QStringLiteral("100"),QStringLiteral("--output"),QStringLiteral("json")});
    musicCliProcess_.start(); emit musicCliChanged(); return true;
}
bool SystemTools::musicCliPlaylistTracks(const QString &playlistId,int offset,int total) {
    static const QRegularExpression playlistIdPattern(QStringLiteral("^[A-Za-z0-9_-]{8,128}$"));
    if(!playlistIdPattern.match(playlistId).hasMatch() || offset<0 || offset>=500)return false;
    const auto executable=musicCliExecutable();
    if(executable.isEmpty()) { musicCliError_=QStringLiteral("ncm-cli is not installed."); emit musicCliChanged(); return false; }
    if(!prepareMusicCliProcess())return false;
    musicCliAction_=QStringLiteral("playlistTracks"); musicCliSearchType_=QStringLiteral("song"); musicCliPlaylistId_=playlistId;
    musicCliPlaylistOffset_=offset; musicCliPlaylistTotal_=qMax(0,total); musicCliOutput_.clear(); musicCliError_.clear(); musicCliTracks_.clear();
    auto env=QProcessEnvironment::systemEnvironment(); env.insert(QStringLiteral("PATH"),musicCliPathEnvironment());
    musicCliProcess_.setProcessEnvironment(env); musicCliProcess_.setProgram(executable);
    musicCliProcess_.setArguments({QStringLiteral("playlist"),QStringLiteral("tracks"),QStringLiteral("--playlistId"),playlistId,QStringLiteral("--limit"),QStringLiteral("50"),QStringLiteral("--offset"),QString::number(offset),QStringLiteral("--output"),QStringLiteral("json")});
    musicCliProcess_.start(); emit musicCliChanged(); return true;
}
bool SystemTools::musicCliPlay(const QString &encryptedId,const QString &originalId,const QString &type) {
    static const QRegularExpression encryptedPattern(QStringLiteral("^[A-Za-z0-9_-]{16,128}$"));
    static const QRegularExpression originalPattern(QStringLiteral("^[0-9]{1,20}$"));
    if(!encryptedPattern.match(encryptedId).hasMatch() || !originalPattern.match(originalId).hasMatch() ||
       (type!=QStringLiteral("song") && type!=QStringLiteral("playlist")))return false;
    const auto executable=musicCliExecutable();
    if(executable.isEmpty()) { musicCliError_=QStringLiteral("ncm-cli is not installed."); emit musicCliChanged(); return false; }
    auto env=QProcessEnvironment::systemEnvironment(); env.insert(QStringLiteral("PATH"),musicCliPathEnvironment());
    const auto mpvLib=localMpvLibraryDirectory();
    env.insert(QStringLiteral("LD_LIBRARY_PATH"),mpvLib+QLatin1Char(':')+env.value(QStringLiteral("LD_LIBRARY_PATH")));
    musicPlayback_.configure(executable,env);
    return musicPlayback_.play({QStringLiteral("play"),type==QStringLiteral("playlist")?QStringLiteral("--playlist"):QStringLiteral("--song"),QStringLiteral("--encrypted-id"),encryptedId,QStringLiteral("--original-id"),originalId});
}
bool SystemTools::musicCliVolume(int volume) {
    return musicPlayback_.control(QStringLiteral("volume"),QString::number(qBound(0,volume,100)));
}
bool SystemTools::musicCliCheckLogin() {
    const auto executable=musicCliExecutable();
    if(executable.isEmpty()) { musicCliAuthenticated_=false; musicCliError_=QStringLiteral("ncm-cli is not installed."); emit musicCliChanged(); return false; }
    if(musicCliProcess_.state()!=QProcess::NotRunning)return false;
    musicCliAction_=QStringLiteral("loginCheck"); musicCliOutput_.clear(); musicCliError_.clear();
    auto env=QProcessEnvironment::systemEnvironment(); env.insert(QStringLiteral("PATH"),musicCliPathEnvironment());
    musicCliProcess_.setProcessEnvironment(env); musicCliProcess_.setProgram(executable);
    musicCliProcess_.setArguments({QStringLiteral("login"),QStringLiteral("--check")});
    musicCliProcess_.start(); emit musicCliChanged(); return true;
}
bool SystemTools::musicCliSeek(int positionSeconds) {
    if(positionSeconds<0)return false;
    return musicPlayback_.control(QStringLiteral("seek"),QString::number(positionSeconds));
}
void SystemTools::setLauncherOpacity(double value) {
    if(!std::isfinite(value))return;
    settings_.setValue("launcherOpacity",qBound(0.0,value,1.0));emit changed();
}
void SystemTools::setModuleOpacity(double value) {
    if(!std::isfinite(value))return;
    settings_.setValue("moduleOpacity",qBound(0.0,value,1.0));emit changed();
}
void SystemTools::setLargeText(bool value) {
    if (largeText() == value) return;
    settings_.setValue("accessibility/largeText", value);
    settings_.sync();
    emit changed();
}

bool SystemTools::setRetentionDays(int days) {
    // 只允许四个明确档位；0 表示永久保留。
    if (days != 0 && days != 30 && days != 90 && days != 180) return false;
    if (retentionDays() == days) return true;
    settings_.setValue("privacy/retentionDays", days);
    settings_.sync();
    emit changed();
    return true;
}

void SystemTools::setEnergySaving(bool value) {
    if (energySaving() == value) return;
    settings_.setValue("energySaving", value); settings_.sync(); emit changed();
}
namespace {
// Pages may only persist their own state. "trusted/" and the host preferences
// share the same QSettings file, so an unscoped write from QML would be a way
// around the plugin trust prompt.
const QStringList &pageSettingPrefixes() {
    static const QStringList prefixes{QStringLiteral("time/")};
    return prefixes;
}
constexpr int kPageSettingMaxKey = 96;
constexpr int kPageSettingMaxBytes = 16384;
bool pageSettingKeyAllowed(const QString &key) {
    if (key.isEmpty() || key.size() > kPageSettingMaxKey) return false;
    for (const auto &prefix : pageSettingPrefixes())
        if (key.startsWith(prefix) && key.size() > prefix.size()) return true;
    return false;
}
}
QString SystemTools::settingsRead(const QString &key, const QString &fallback) const {
    if (!pageSettingKeyAllowed(key)) return fallback;
    return settings_.value(key, fallback).toString();
}
bool SystemTools::settingsWrite(const QString &key, const QString &value) {
    if (!pageSettingKeyAllowed(key)) { qWarning("page settings: rejected key %s", qPrintable(key.left(96))); return false; }
    if (value.toUtf8().size() > kPageSettingMaxBytes) { qWarning("page settings: value too large for %s", qPrintable(key)); return false; }
    settings_.setValue(key, value);
    settings_.sync();
    return true;
}
void SystemTools::setUiLanguage(const QString &language) {
    const auto normalized = language == QStringLiteral("en") ? QStringLiteral("en") : QStringLiteral("zh");
    if (uiLanguage() == normalized) return;
    settings_.setValue("uiLanguage", normalized); settings_.sync(); emit changed();
}
SystemTools::SystemTools(QObject *p) : QObject(p), speech_(this) {
    qApp->installEventFilter(this);
    loadPinyinDictionaries();
    musicNetworkProbeTimer_.setInterval(10000);
    connect(&musicNetworkProbeTimer_,&QTimer::timeout,this,&SystemTools::probeMusicNetwork);
    musicNetworkProbeTimer_.start();
    QTimer::singleShot(0,this,&SystemTools::probeMusicNetwork);
    // The terminal UI text is Chinese, but the host OS may use an English
    // locale. Select Mandarin explicitly instead of letting the speech
    // backend choose its default voice.
    speech_.setLocale(QLocale(QStringLiteral("zh_CN")));
    for (const auto &locale : speech_.availableLocales())
        if (locale.language() == QLocale::Chinese) { speech_.setLocale(locale); qtChineseVoice_ = true; break; }
    speechProgram_ = QStandardPaths::findExecutable("espeak-ng");
    connect(&speech_, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State state) {
        if (qtChineseVoice_) setSpeechActive(state == QTextToSpeech::Speaking);
    });
    speechTimeout_.setSingleShot(true); speechTimeout_.setInterval(15000);
    connect(&speechTimeout_, &QTimer::timeout, this, [this] { speechProcess_.kill(); });
    connect(&speechProcess_, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        speechTimeout_.stop();
        if (code != 0) log("语音播报失败，请检查音频输出：" + QString::fromUtf8(speechProcess_.readAllStandardError()).left(160));
        startPendingSpeech();
        if (speechProcess_.state() == QProcess::NotRunning) setSpeechActive(false);
    });
    connect(&speechProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { speechTimeout_.stop(); pendingSpeech_.clear(); setSpeechActive(false); log("无法启动中文语音引擎"); }
    });
    connect(&musicCliProcess_, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        if(musicCliAction_==QStringLiteral("loginCheck")) {
            musicCliProcess_.readAllStandardOutput(); musicCliProcess_.readAllStandardError();
            musicCliOutput_.clear(); musicCliError_.clear();
            musicCliAuthenticated_=status==QProcess::NormalExit && code==0;
            emit musicCliChanged();
            return;
        }
        musicCliOutput_=QString::fromUtf8(musicCliProcess_.readAllStandardOutput()).trimmed();
        musicCliError_=QString::fromUtf8(musicCliProcess_.readAllStandardError()).trimmed();
        static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-?]*[ -/]*[@-~]"));
        musicCliOutput_.remove(ansi); musicCliError_.remove(ansi);
        if(musicCliAction_==QStringLiteral("play") && !musicCliOutput_.isEmpty()) {
            QJsonParseError playError;
            const auto playResponse=QJsonDocument::fromJson(musicCliOutput_.toUtf8(),&playError);
            if(playError.error==QJsonParseError::NoError && playResponse.isObject()) {
                const auto object=playResponse.object();
                const auto code=object.value(QStringLiteral("code"));
                if(code.isDouble() && code.toInt()!=200) {
                    musicCliError_=object.value(QStringLiteral("message")).toString();
                    if(musicCliError_.isEmpty())musicCliError_=QStringLiteral("该歌曲当前无法播放");
                }
            }
        }
        if(musicCliAction_==QStringLiteral("logout")) {
            if(status==QProcess::NormalExit && code==0) {
                musicCliAuthenticated_=false;
                musicCliTracks_.clear(); musicCliPlaybackState_.clear(); musicCliPlaylistId_.clear();
                musicCliPlaylistOffset_=0; musicCliPlaylistTotal_=0; musicCliError_.clear();
            }
        } else if(musicCliAction_==QStringLiteral("state")) {
            QJsonParseError parseError;
            const auto response=QJsonDocument::fromJson(musicCliOutput_.toUtf8(),&parseError);
            if(parseError.error==QJsonParseError::NoError && response.isObject()) {
                const auto state=response.object().value(QStringLiteral("state")).toObject();
                musicCliPlaybackState_={
                    {QStringLiteral("status"),state.value(QStringLiteral("status")).toString()},
                    {QStringLiteral("title"),state.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("position"),state.value(QStringLiteral("position")).toDouble()},
                    {QStringLiteral("duration"),state.value(QStringLiteral("duration")).toDouble()},
                    {QStringLiteral("volume"),state.value(QStringLiteral("volume")).isNull()?-1:state.value(QStringLiteral("volume")).toDouble()},
                    {QStringLiteral("currentIndex"),state.value(QStringLiteral("currentIndex")).toInt()},
                    {QStringLiteral("queueLength"),state.value(QStringLiteral("queueLength")).toInt()}
                };
            }
        } else if(musicCliAction_==QStringLiteral("search") || musicCliAction_==QStringLiteral("recommend") ||
                  musicCliAction_==QStringLiteral("playlistList") || musicCliAction_==QStringLiteral("playlistTracks")) {
            musicCliTracks_.clear();
            QJsonParseError parseError;
            const auto response=QJsonDocument::fromJson(musicCliOutput_.toUtf8(),&parseError);
            if(parseError.error==QJsonParseError::NoError && response.isObject()) {
                    const auto data=response.object().value(QStringLiteral("data"));
                QJsonArray records;
                if(data.isObject()) {
                    const auto object=data.toObject();
                    records=object.value(QStringLiteral("records")).toArray();
                    if(records.isEmpty())records=object.value(QStringLiteral("playlists")).toArray();
                    if(records.isEmpty())records=object.value(QStringLiteral("list")).toArray();
                }
                else if(data.isArray())records=data.toArray();
                for(const auto &value:records) {
                    if(!value.isObject())continue;
                    const auto song=value.toObject();
                    QStringList artistNames;
                    for(const auto &artist:song.value(QStringLiteral("artists")).toArray())
                        if(artist.isObject())artistNames.append(artist.toObject().value(QStringLiteral("name")).toString());
                    auto idString=[](const QJsonValue &id) {
                        if(id.isString())return id.toString();
                        if(id.isDouble())return QString::number(static_cast<qint64>(id.toDouble()));
                        return QString{};
                    };
                    const auto album=song.value(QStringLiteral("album")).toObject();
                    const auto creator=song.value(QStringLiteral("creator")).toObject();
                    const auto recordType=(musicCliAction_==QStringLiteral("recommend") || musicCliAction_==QStringLiteral("playlistTracks"))?QStringLiteral("song"):musicCliSearchType_;
                    auto creatorName=song.value(QStringLiteral("creatorNickName")).toString();
                    if(creatorName.isEmpty())creatorName=creator.value(QStringLiteral("nickname")).toString();
                    musicCliTracks_.append(QVariantMap{
                        {QStringLiteral("name"),song.value(QStringLiteral("name")).toString()},
                        {QStringLiteral("artist"),recordType==QStringLiteral("song")?artistNames.join(QStringLiteral(" / ")):creatorName},
                        {QStringLiteral("album"),album.value(QStringLiteral("name")).toString()},
                        {QStringLiteral("type"),recordType},
                        {QStringLiteral("encryptedId"),song.value(QStringLiteral("id")).toString()},
                        {QStringLiteral("originalId"),idString(song.value(QStringLiteral("originalId")))},
                        {QStringLiteral("duration"),static_cast<qint64>(song.value(QStringLiteral("duration")).toDouble())},
                        {QStringLiteral("trackCount"),static_cast<int>(song.value(QStringLiteral("trackCount")).toDouble())},
                        {QStringLiteral("coverUrl"),song.value(QStringLiteral("coverImgUrl")).toString()},
                        {QStringLiteral("playable"),recordType==QStringLiteral("playlist") || song.value(QStringLiteral("playFlag")).toBool()}
                    });
                }
            }
        }
        if(status!=QProcess::NormalExit || code!=0) {
            if(musicCliError_.isEmpty())musicCliError_=QStringLiteral("ncm-cli command failed (exit %1). Check setup and sign-in.").arg(code);
        }
        emit musicCliChanged();
    });
    connect(&musicCliProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if(error==QProcess::FailedToStart) {
            musicCliError_=QStringLiteral("Could not start ncm-cli. Check Node.js and the CLI installation.");
            emit musicCliChanged();
        }
    });
    connect(&musicPlayback_, &MusicPlayback::changed, this, [this] {
        musicCliError_=musicPlayback_.error();
        emit musicCliChanged();
    });
    connect(&musicPlayback_, &MusicPlayback::ended, this, &SystemTools::musicPlaybackEnded);
    QDir().mkpath(filesRoot());
    connect(&removableStorage_, &RemovableStorage::changed, this, &SystemTools::removableDevicesChanged);
    connect(&removableStorage_, &RemovableStorage::inserted, this, &SystemTools::removableDeviceInserted);
    connect(&removableStorage_, &RemovableStorage::ready, this, &SystemTools::removableDeviceReady);
    connect(&removableStorage_, &RemovableStorage::error, this, &SystemTools::removableDeviceError);
    auto *timer = new QTimer(this); timer->setInterval(5000); connect(timer,&QTimer::timeout,this,&SystemTools::refreshStatus); timer->start();
    connect(&bluetooth_,&QBluetoothLocalDevice::hostModeStateChanged,this,[this](auto) { refreshStatus(); });
    connect(&bluetooth_,&QBluetoothLocalDevice::errorOccurred,this,[this](auto error) {
        log("蓝牙操作失败：" + QString::number(static_cast<int>(error)));
        refreshStatus();
    });
    hotspotOpenedByTerminal_ = QFileInfo::exists(hotspotOwnershipMarker());
    refreshStatus();
    if (hotspotOpenedByTerminal_) {
        // Recover an AP lease after an unclean process exit. Give the PBR1
        // bridge time to announce attached modules and reacquire the lease.
        QTimer::singleShot(15000, this, [this] {
            if (!hotspotOpenedByTerminal_ || hotspotUserPinned_ ||
                !hotspotLeases_.isEmpty() || !pendingHotspotLeases_.isEmpty()) return;
            queryHotspot([this](bool known, bool active) {
                if (!known || !hotspotLeases_.isEmpty() ||
                    !pendingHotspotLeases_.isEmpty() || hotspotUserPinned_) return;
                if (!active) {
                    hotspotOpenedByTerminal_ = false;
                    setHotspotOwnershipMarker(false);
                    return;
                }
                setHotspot(false, [this](bool ok, bool stillActive) {
                    if (ok && !stillActive) {
                        hotspotOpenedByTerminal_ = false;
                        setHotspotOwnershipMarker(false);
                    }
                });
            });
        });
    }
}
bool SystemTools::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::FocusIn) {
        const auto name = QString::fromLatin1(watched->metaObject()->className());
        if (watched->inherits("QQuickTextInput") || watched->inherits("QQuickTextEdit") ||
            name.contains("TextInput") || name.contains("TextField") || name.contains("TextEdit") || name.contains("TextArea") ||
            name.contains("RenderWidget") || name.contains("WebEngine"))
            keyboardTarget_ = watched;
    }
    return QObject::eventFilter(watched, event);
}
void SystemTools::captureKeyboardTargetItem(QObject *target) {
    if (target) keyboardTarget_ = target;
}
void SystemTools::captureKeyboardTarget() {
    if (auto *focus = QGuiApplication::focusObject()) {
        const auto name = QString::fromLatin1(focus->metaObject()->className());
        if (focus->inherits("QQuickTextInput") || focus->inherits("QQuickTextEdit") ||
            name.contains("TextInput") || name.contains("TextField") || name.contains("TextEdit") || name.contains("TextArea") ||
            name.contains("RenderWidget") || name.contains("WebEngine")) keyboardTarget_ = focus;
    }
}
void SystemTools::keyboardCommit(const QString &text) {
    if (!keyboardTarget_ || text.isEmpty()) return;
    QInputMethodEvent event;
    event.setCommitString(text);
    QCoreApplication::sendEvent(keyboardTarget_, &event);
}
void SystemTools::keyboardBackspace() {
    if (!keyboardTarget_) return;
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QCoreApplication::sendEvent(keyboardTarget_, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Backspace, Qt::NoModifier);
    QCoreApplication::sendEvent(keyboardTarget_, &release);
}
void SystemTools::keyboardReturn() {
    if (!keyboardTarget_) return;
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCoreApplication::sendEvent(keyboardTarget_, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCoreApplication::sendEvent(keyboardTarget_, &release);
}
QString SystemTools::pinyinDictionary() const {
    static const QString contents = [] {
        QFile file(QStringLiteral(":/pinyin/pinyin_simp.dict.yaml"));
        if (!file.open(QIODevice::ReadOnly)) return QString{};
        return QString::fromUtf8(file.readAll());
    }();
    return contents;
}
QString SystemTools::pinyinPhraseDictionary() const {
    static const QString contents = [] {
        QFile file(QStringLiteral(":/pinyin/phrases.tsv"));
        if (!file.open(QIODevice::ReadOnly)) return QString{};
        return QString::fromUtf8(file.readAll());
    }();
    return contents;
}
void SystemTools::loadPinyinDictionaries() {
    QHash<QString,int> frequencies;
    QFile frequencyFile(QStringLiteral(":/pinyin/jieba-dict.txt"));
    if(frequencyFile.open(QIODevice::ReadOnly|QIODevice::Text)) {
        QTextStream stream(&frequencyFile);
        while(!stream.atEnd()) {
            const auto fields=stream.readLine().split(QLatin1Char(' '));
            if(fields.size()>1)frequencies.insert(fields[0],fields[1].toInt());
        }
    }
    auto parse = [](const QString &resource, auto &index, bool phraseTable) {
        QFile file(resource);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            const auto line = stream.readLine();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QStringLiteral("---")) || line == QStringLiteral("...")) continue;
            const auto columns = line.split(QLatin1Char('\t'));
            if (columns.size() < 2) continue;
            const auto code = columns[phraseTable ? 0 : 1].trimmed().toLower();
            if (code.isEmpty() || !QRegularExpression(QStringLiteral("^[a-zv]+$")).match(code).hasMatch()) continue;
            const auto word = columns[phraseTable ? 1 : 0].trimmed();
            if (word.isEmpty()) continue;
            bool ok = false;
            const int weight = columns.size() > 2 ? columns[2].toInt(&ok) : 0;
            auto &values = index[code];
            values.append({word, ok ? weight : 0});
        }
    };
    parse(QStringLiteral(":/pinyin/pinyin_simp.dict.yaml"), characterPinyin_, false);
    parse(QStringLiteral(":/pinyin/phrases.tsv"), phrasePinyin_, true);
    for(auto it=phrasePinyin_.begin();it!=phrasePinyin_.end();++it)
        for(auto &candidate:it.value())candidate.weight=qMax(1,frequencies.value(candidate.word,1));
    for(auto it=characterPinyin_.begin();it!=characterPinyin_.end();++it)
        for(auto &candidate:it.value())if(frequencies.contains(candidate.word))candidate.weight=frequencies.value(candidate.word);
    for (auto *index : {&characterPinyin_, &phrasePinyin_}) {
        for (auto it=index->begin();it!=index->end();++it) {
            std::stable_sort(it->begin(),it->end(),[](const PinyinCandidate &a,const PinyinCandidate &b){return a.weight>b.weight;});
            if(it->size()>64)it->resize(64);
        }
    }
}
QStringList SystemTools::pinyinCandidates(const QString &input) const {
    QString code=input.toLower();code.remove(QLatin1Char('\''));code.replace(QChar(0x00fc),QLatin1Char('v'));
    if (code.isEmpty() || code.size() > 32) return {};
    struct Path { QString text; double score; };
    QVector<QVector<Path>> lattice(code.size()+1);lattice[0].append({{},0});
    for(int offset=0;offset<code.size();++offset) {
        auto &paths=lattice[offset];
        std::stable_sort(paths.begin(),paths.end(),[](const Path &a,const Path &b){return a.score>b.score;});
        if(paths.size()>48)paths.resize(48);
        for(int length=1;length<=qMin(24,code.size()-offset);++length) {
            const auto syllable=code.mid(offset,length);
            QVector<PinyinCandidate> options=phrasePinyin_.value(syllable);
            options+=characterPinyin_.value(syllable);
            if(options.size()>32)options.resize(32);
            for(const auto &path:paths)for(const auto &candidate:options)
                lattice[offset+length].append({path.text+candidate.word,path.score+std::log(double(candidate.weight)+1.0)-std::log(60101967.0)});
        }
    }
    auto complete=lattice.last();
    for(auto &path:complete)path.score+=std::log1p(settings_.value("imeUsage/"+code+"/"+path.text,0).toInt())*2.0;
    if(complete.isEmpty() && code.size()>1) {
        for(auto it=phrasePinyin_.constBegin();it!=phrasePinyin_.constEnd();++it)
            if(it.key().startsWith(code))for(const auto &candidate:it.value())complete.append({candidate.word,std::log(double(candidate.weight)+1)-it.key().size()*0.1});
    }
    std::sort(complete.begin(), complete.end(), [](const Path &a, const Path &b) { return a.score > b.score; });
    QStringList result;
    QSet<QString> unique;
    for (const auto &path : complete) {
        if (unique.contains(path.text)) continue;
        unique.insert(path.text); result.append(path.text);
        if (result.size() >= 120) break;
    }
    return result;
}
void SystemTools::recordPinyinSelection(const QString &code,const QString &word) {
    if(code.isEmpty()||word.isEmpty())return;
    const auto key="imeUsage/"+code.toLower()+"/"+word;
    settings_.setValue(key,qMin(10000,settings_.value(key,0).toInt()+1));settings_.sync();
}
QString SystemTools::filesRoot() const {
    // Start the file application at the current user's home directory.
    // safePath() still rejects traversal, absolute paths and symlink escapes.
    return QDir::homePath();
}
SystemTools::~SystemTools() {
    pendingSpeech_.clear(); speechTimeout_.stop();
    speechProcess_.disconnect(this);
    if (speechProcess_.state()!=QProcess::NotRunning) {
        speechProcess_.kill(); speechProcess_.waitForFinished(1000);
    }
    if(musicCliProcess_.state()!=QProcess::NotRunning) {
        musicCliProcess_.kill(); musicCliProcess_.waitForFinished(1000);
    }
    // nmcli helpers are asynchronous children. Kill and reap them before the
    // QObject base destructor deletes QProcess children; otherwise a quick
    // window/application exit can leave "QProcess destroyed while running"
    // warnings and a short-lived background NetworkManager operation.
    const auto processes = findChildren<QProcess *>();
    for (auto *process : processes) {
        QObject::disconnect(process, nullptr, this, nullptr);
        if (process->state() == QProcess::NotRunning) continue;
        process->terminate();
        if (!process->waitForFinished(500)) {
            process->kill();
            process->waitForFinished(1500);
        }
    }
    // If this process created the AP solely to satisfy session leases, close
    // it on graceful application shutdown. A pre-existing or user-pinned AP is
    // never touched here. The profile remains autoconnect=no for crash/reboot
    // recovery, so an unclean exit cannot make NetworkManager start it at boot.
    if (hotspotOpenedByTerminal_ && !hotspotUserPinned_ && hotspotLeases_.isEmpty()) {
        QProcess cleanup;
        if (qEnvironmentVariableIntValue("ORANGE_PI_HOTSPOT_SUDO") == 1)
            cleanup.start("sudo", {"-n", "/usr/bin/nmcli", "--wait", "5", "connection", "down", "pi-module-ap"});
        else
            cleanup.start("nmcli", {"--wait", "5", "connection", "down", "pi-module-ap"});
        if (!cleanup.waitForStarted(1000)) return;
        if (!cleanup.waitForFinished(6000)) { cleanup.kill(); cleanup.waitForFinished(1000); }
        else if (cleanup.exitStatus() == QProcess::NormalExit && cleanup.exitCode() == 0)
            setHotspotOwnershipMarker(false);
    }
}
QUrl SystemTools::monochromeIcon(const QString &name, bool white) const {
    static QHash<QString,QUrl> cache;
    static const QRegularExpression valid("^[a-z0-9_]+$");
    const auto icon=valid.match(name).hasMatch()?name:QString("extension");
    const auto key=icon+(white?"/white":"/black");
    if(cache.contains(key))return cache.value(key);
    QFile file(":/icons/material/light/"+icon+".svg");
    if(!file.exists())file.setFileName(":/icons/material/dark/"+icon+".svg");
    if(!file.exists())file.setFileName(":/icons/material/light/extension.svg");
    if(!file.open(QIODevice::ReadOnly))return {};
    QString svg=QString::fromUtf8(file.readAll());
    static const QRegularExpression paint("(fill|stroke)=\"#[0-9a-fA-F]{3,8}\"");
    svg.replace(paint,QString("\\1=\"%1\"").arg(white?"#ffffff":"#000000"));
    const QUrl source(QString("data:image/svg+xml;base64,")+QString::fromLatin1(svg.toUtf8().toBase64()));
    cache.insert(key,source);return source;
}
QUrl SystemTools::wallpaper() const {
    const auto path=settings_.value("wallpaper").toString();
    return !path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).isReadable() ? QUrl::fromLocalFile(path) : QUrl{};
}
bool SystemTools::setWallpaper(const QUrl &url) {
    sampledWallpaper_ = QUrl{}; wallpaperSample_ = QImage{};
    if (url.isEmpty()) { settings_.remove("wallpaper");settings_.sync();emit changed();return true; }
    if (!url.isLocalFile()) { log("壁纸必须是本机图片文件");return false; }
    const auto path=QFileInfo(url.toLocalFile()).canonicalFilePath();
    QImageReader reader(path);
    const auto size=reader.size();
    if (path.isEmpty() || !reader.canRead() || !size.isValid() || qint64(size.width())*size.height()>100000000) {
        log("无法设置壁纸：图片无效或尺寸过大");return false;
    }
    reader.setAutoTransform(true);
    reader.setScaledSize(size.scaled(QSize(3840,2160),Qt::KeepAspectRatio));
    if (reader.read().isNull()) { log("无法设置壁纸："+reader.errorString());return false; }
    settings_.setValue("wallpaper",path);settings_.sync();emit changed();return true;
}
QColor SystemTools::wallpaperColor(double x, double y, double width, double height) const {
    const auto source=wallpaper();
    if (source!=sampledWallpaper_) {
        sampledWallpaper_=source;
        QImageReader reader(source.toLocalFile());reader.setAutoTransform(true);
        const auto size=reader.size();
        if(size.isValid())reader.setScaledSize(size.scaled(256,256,Qt::KeepAspectRatio));
        wallpaperSample_=source.isEmpty()?QImage{}:reader.read();
    }
    const QColor fallback("#08121f");
    if(wallpaperSample_.isNull() || width<=0 || height<=0)return fallback;
    const double scale=qMax(width/wallpaperSample_.width(),height/wallpaperSample_.height());
    const int px=qBound(0,int((x+(wallpaperSample_.width()*scale-width)/2)/scale),wallpaperSample_.width()-1);
    const int py=qBound(0,int((y+(wallpaperSample_.height()*scale-height)/2)/scale),wallpaperSample_.height()-1);
    double r=0,g=0,b=0;int count=0;
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){
        const auto c=wallpaperSample_.pixelColor(qBound(0,px+dx,wallpaperSample_.width()-1),qBound(0,py+dy,wallpaperSample_.height()-1));
        r+=c.redF()*c.alphaF()+fallback.redF()*(1-c.alphaF());
        g+=c.greenF()*c.alphaF()+fallback.greenF()*(1-c.alphaF());
        b+=c.blueF()*c.alphaF()+fallback.blueF()*(1-c.alphaF());++count;
    }
    return QColor::fromRgbF(r/count,g/count,b/count);
}
bool SystemTools::setFileWallpaper(const QString &relative) {
    const auto path=safePath(relative);
    if (path.isEmpty()) { log("壁纸文件路径无效");return false; }
    return setWallpaper(QUrl::fromLocalFile(path));
}
QString SystemTools::fileLocation(const QString &name) const {
    const auto type = name == "downloads" ? QStandardPaths::DownloadLocation : QStandardPaths::DocumentsLocation;
    const auto path = QStandardPaths::writableLocation(type);
    const auto relative = QDir(filesRoot()).relativeFilePath(path);
    return !relative.startsWith("../") && !QDir::isAbsolutePath(relative) && QFileInfo(path).isDir() ? relative : QString{};
}
bool SystemTools::createFile(const QString &relative, const QString &text) {
    const auto path = safePath(relative, true);
    const auto bytes = text.toUtf8();
    QFile file(path);
    if (path.isEmpty() || bytes.size() > 65536 || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        log("创建失败：路径无效或文件已存在：" + relative); return false;
    }
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    if (!ok) QFile::remove(path);
    log((ok ? "已创建文件：" : "创建文件失败：") + relative);
    return ok;
}
void SystemTools::rememberFile(const QString &relative) {
    const auto path = safePath(relative);
    if (path.isEmpty() || !QFileInfo(path).isFile()) return;
    auto recent = settings_.value("recentFiles").toStringList();
    recent.removeAll(relative); recent.prepend(relative);
    while (recent.size() > 50) recent.removeLast();
    settings_.setValue("recentFiles", recent);
}
QVariantList SystemTools::recentFiles() const {
    QVariantList result;
    for (const auto &relative : settings_.value("recentFiles").toStringList()) {
        const auto path = safePath(relative); const QFileInfo file(path);
        if (!path.isEmpty() && file.isFile())
            result.append(QVariantMap{{"name",file.fileName()},{"directory",false},{"bytes",file.size()},{"relative",relative}});
    }
    return result;
}
QString SystemTools::safePath(const QString &relative, bool creating) const {
    if (relative.startsWith("@usb/")) return removableStorage_.resolve(relative, creating);
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.split('/').contains("..")) return {};
    const auto root = QFileInfo(filesRoot()).canonicalFilePath();
    QFileInfo info(filesRoot() + '/' + relative);
    if (root.isEmpty() || info.isSymLink()) return {};
    const auto path = creating && !info.exists()
        ? QFileInfo(info.absolutePath()).canonicalFilePath() + '/' + info.fileName()
        : info.canonicalFilePath();
    return path.startsWith(root + '/') ? path : QString{};
}
bool SystemTools::write(const QString &relative, const QString &text) {
    const auto path = safePath(relative, true); const auto data = text.toUtf8();
    if (path.isEmpty() || data.size() > 65536) { log("保存失败：路径无效或文本超过 64 KiB"); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) { log("保存失败：" + file.errorString()); return false; }
    log("已保存：" + relative); return true;
}
bool SystemTools::makeDirectory(const QString &relative) {
    const auto path = safePath(relative, true); const bool ok = !path.isEmpty() && QDir().mkdir(path);
    log((ok ? "已创建目录：" : "创建目录失败：") + relative); return ok;
}
bool SystemTools::rename(const QString &from, const QString &to) {
    const auto source = safePath(from), target = safePath(to, true);
    const bool ok = !source.isEmpty() && !target.isEmpty() && !QFileInfo::exists(target) && QDir().rename(source, target);
    log((ok ? "已重命名：" : "重命名失败：") + from); return ok;
}
bool SystemTools::remove(const QString &relative) {
    const auto path = safePath(relative);
    const bool ok = !path.isEmpty() && (QFileInfo(path).isDir() ? QDir().rmdir(path) : QFile::remove(path));
    log((ok ? "已删除：" : "删除失败（目录必须为空）：") + relative); return ok;
}
QVariantList SystemTools::files(const QString &relative) const {
    const auto path = relative.startsWith("@usb/") ? removableStorage_.resolve(relative, false, true)
        : relative.isEmpty() ? QFileInfo(filesRoot()).canonicalFilePath() : safePath(relative);
    if (path.isEmpty()) return {};
    QVariantList out;
    for (const auto &f : QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
        out.append(QVariantMap{{"name",f.fileName()},{"directory",f.isDir()},{"bytes",f.size()}});
    return out;
}
QUrl SystemTools::fileUrl(const QString &relative) const {
    const auto path = safePath(relative);
    return path.isEmpty() ? QUrl{} : QUrl::fromLocalFile(path);
}
QString SystemTools::read(const QString &relative) const {
    const auto path = safePath(relative); QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return "无法读取";
    if (f.size() > 65536) return "文件超过 64 KiB，请使用外部编辑器";
    return QString::fromUtf8(f.readAll());
}
QString SystemTools::fileDisplayPath(const QString &relative) const {
    if (relative.startsWith("@usb/")) return removableStorage_.resolve(relative, false, true);
    return filesRoot() + (relative.isEmpty() ? QString{} : '/' + relative);
}
bool SystemTools::editable(const QString &relative) const {
    const auto path = safePath(relative); if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return false;
    const auto data = file.readAll();
    return !data.contains('\0') && QString::fromUtf8(data).toUtf8() == data;
}
bool SystemTools::openWithDefault(const QString &relative) {
    const auto path = safePath(relative);
    if (path.isEmpty() || !QFileInfo(path).isFile()) { log("无法打开：文件路径无效"); return false; }
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    log(opened ? "已调用系统默认应用：" + relative : "系统默认应用打开失败：" + relative);
    return opened;
}
QString SystemTools::network() const {
    QStringList out;
    for (auto i : QNetworkInterface::allInterfaces()) for (auto a : i.addressEntries())
        out.append(i.humanReadableName() + ": " + a.ip().toString());
    return out.join('\n');
}
void SystemTools::speak(const QString &s) {
    if (!settings_.value("speech",true).toBool() || s.trimmed().isEmpty()) return;
    if (qtChineseVoice_) {
        setSpeechActive(true);
        speech_.say(s.left(300));
        if (speech_.state() == QTextToSpeech::Error || speech_.state() == QTextToSpeech::Ready) setSpeechActive(false);
        return;
    }
    if (!speechProgram_.isEmpty()) {
        // Coalesce rapid status changes rather than spawning concurrent players
        // or growing an unbounded queue. No shell interpolation of message text.
        pendingSpeech_ = s.left(300); startPendingSpeech(); return;
    }
    log("中文语音引擎不可用，请安装 espeak-ng 或支持中文的 Qt TTS 插件");
}
void SystemTools::startPendingSpeech() {
    if (speechProcess_.state() != QProcess::NotRunning || pendingSpeech_.isEmpty()) return;
    if (!settings_.value("speech",true).toBool()) { pendingSpeech_.clear(); return; }
    const auto text = pendingSpeech_; pendingSpeech_.clear();
    setSpeechActive(true);
    // Debian's cmn voice enables "latin as English"; its Han dictionary may
    // emit pinyin that is then read as English. Use the Mandarin pinyin variant
    // for both Han input and dictionary expansion, with explicit UTF-8 bytes.
    speechProcess_.start(speechProgram_, {"-v", "cmn-latn-pinyin", "-b", "1", "-s", "150", "--stdin"});
    speechProcess_.write(text.toUtf8()); speechProcess_.closeWriteChannel();
    speechTimeout_.start();
}
void SystemTools::setSpeechEnabled(bool e) {
    settings_.setValue("speech",e);
    if(!e) { pendingSpeech_.clear(); speech_.stop(); speechTimeout_.stop(); speechProcess_.kill(); setSpeechActive(false); }
    emit changed();
}
void SystemTools::setSpeechActive(bool active) {
    if (speechActive_ == active) return;
    speechActive_ = active;
    emit audioFocusChanged();
}
void SystemTools::setDuckMediaForSpeech(bool enabled) {
    if (duckMediaForSpeech() == enabled) return;
    settings_.setValue("duckMediaForSpeech", enabled);
    settings_.sync();
    emit audioFocusChanged();
}
void SystemTools::setInterfaceScale(double scale) { settings_.setValue("interface_scale",qBound(1.0,scale,1.5)); emit changed(); }
void SystemTools::refreshStatus() {
    auto readText=[](const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()).trimmed():QString{}; };
    bool online=false,wifi=false;
    for (const auto &nic:QNetworkInterface::allInterfaces()) {
        if (!(nic.flags()&QNetworkInterface::IsUp) || !(nic.flags()&QNetworkInterface::IsRunning) || (nic.flags()&QNetworkInterface::IsLoopBack)) continue;
        bool addressed=false;
        for(const auto &address:nic.addressEntries()) if(!address.ip().isNull() && !address.ip().isLoopback() && !address.ip().isLinkLocal()) addressed=true;
        online=online||addressed;
        if(nic.type()==QNetworkInterface::Ieee80211) wifi=wifi||addressed;
    }
    int battery=-1;QString batteryState;bool externalPower=false;QString externalPowerType;
    const QDir power("/sys/class/power_supply");
    for(const auto &name:power.entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
        const QString prefix=power.filePath(name)+"/";
        const auto type=readText(prefix+"type");
        if(type=="Battery" && readText(prefix+"scope")!="Device") {
            bool ok=false;const int value=readText(prefix+"capacity").toInt(&ok);
            if(ok && value>=0 && value<=100) {battery=value;batteryState=readText(prefix+"status");}
        } else if(type=="Mains" || type=="USB" || type=="USB_C" || type=="USB_PD" ||
                  type=="USB_DCP" || type=="Wireless" || type=="Dock" || type=="UPS") {
            if(readText(prefix+"online")=="1") {externalPower=true;if(externalPowerType.isEmpty())externalPowerType=type;}
        }
    }
    const QString powerMode=battery>=0?QStringLiteral("battery"):externalPower?QStringLiteral("external"):QStringLiteral("other");
    qint64 total=0,available=0;
    for(const auto &line:readText("/proc/meminfo").split('\n')) {
        const auto parts=line.simplified().split(' ');
        if(parts.size()<2) continue;
        if(parts[0]=="MemTotal:") total=parts[1].toLongLong();
        if(parts[0]=="MemAvailable:") available=parts[1].toLongLong();
    }
    QStorageInfo storage(filesRoot());
    const auto bluetoothState=bluetooth_.isValid()?bluetooth_.hostMode():QBluetoothLocalDevice::HostPoweredOff;
    deviceStatus_={{"networkOnline",online},{"wifiConnected",wifi},
        {"hotspotActive",hotspotActive_},{"hotspotKnown",hotspotKnown_},
        {"networkLabel",wifi?"Wi-Fi 已连接":online?"网络接口已连接":"网络未连接"},
        {"bluetoothAvailable",bluetooth_.isValid()},
        {"bluetoothOn",bluetooth_.isValid() && bluetoothState!=QBluetoothLocalDevice::HostPoweredOff},
        {"batteryPresent",battery>=0},{"batteryPercent",battery},{"batteryState",batteryState},
        {"externalPower",externalPower},{"externalPowerType",externalPowerType},{"powerMode",powerMode},
        {"memoryLabel",total>0?QString("%1 / %2 GiB").arg((total-available)/1048576.0,0,'f',1).arg(total/1048576.0,0,'f',1):QString("未检测到")},
        {"storageLabel",storage.isValid()?QString("可用 %1 GiB").arg(storage.bytesAvailable()/1073741824.0,0,'f',1):QString("未检测到")}};
    emit changed();
    if (!hotspotBusy_ && !hotspotQueryBusy_) queryHotspot();
}
void SystemTools::setBluetoothEnabled(bool enabled) {
    if(!bluetooth_.isValid()) {log("未检测到蓝牙适配器");return;}
    if(enabled) bluetooth_.powerOn();else bluetooth_.setHostMode(QBluetoothLocalDevice::HostPoweredOff);
    log(enabled?"已请求开启蓝牙":"已请求关闭蓝牙");
    QTimer::singleShot(300,this,&SystemTools::refreshStatus);
}
void SystemTools::remember(const QString &url) {
    auto list = bookmarks(); if (!list.contains(url) && list.size() < 100) { list.append(url); settings_.setValue("bookmarks",list); }
    emit changed();
}
void SystemTools::removeBookmark(const QString &url) {
    auto list=bookmarks();
    list.removeAll(url);
    settings_.setValue("bookmarks",list);
    settings_.sync();
    emit changed();
}
QStringList SystemTools::bookmarks() const { return settings_.value("bookmarks").toStringList(); }
void SystemTools::log(const QString &s) {
    logs_.prepend(QDateTime::currentDateTime().toString(Qt::ISODate) + " " + s.left(2048));
    while (logs_.size() > 200) logs_.removeLast();
    const auto name = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/terminal.log";
    QFile output(name);
    if (output.size() > 1024*1024) { QFile::remove(name + ".1"); output.rename(name + ".1"); output.setFileName(name); }
    if (output.open(QIODevice::WriteOnly | QIODevice::Append)) output.write((logs_.first() + '\n').toUtf8());
    emit notification(s);
    emit changed();
}
void SystemTools::hotspot(bool enabled) {
    if (!enabled && (!hotspotLeases_.isEmpty() || !pendingHotspotLeases_.isEmpty())) {
        log("当前模块会话仍需要热点，暂不关闭");
        return;
    }
    if (enabled) hotspotUserPinned_ = true;
    else hotspotUserPinned_ = false;
    setHotspot(enabled, [this, enabled](bool ready, bool active) {
        if (ready && active == enabled && enabled) hotspotUserPinned_ = true;
        if (ready && !active && !enabled) {
            hotspotOpenedByTerminal_ = false;
            setHotspotOwnershipMarker(false);
        }
    });
}
void SystemTools::acquireHotspotLease(const QString &owner) {
    if (owner.isEmpty() || hotspotLeases_.contains(owner) || pendingHotspotLeases_.contains(owner)) return;
    pendingHotspotLeases_.insert(owner);
    processHotspotLeaseRequests();
}
void SystemTools::releaseHotspotLease(const QString &owner) {
    pendingHotspotLeases_.remove(owner);
    hotspotLeases_.remove(owner);
    if (!hotspotLeases_.isEmpty() || !pendingHotspotLeases_.isEmpty() ||
        !hotspotOpenedByTerminal_ || hotspotUserPinned_) return;
    setHotspot(false, [this](bool ready, bool active) {
        if (ready && !active) {
            hotspotOpenedByTerminal_ = false;
            setHotspotOwnershipMarker(false);
        }
    });
}
void SystemTools::processHotspotLeaseRequests() {
    if (pendingHotspotLeases_.isEmpty() || hotspotBusy_) return;
    queryHotspot([this](bool known, bool active) {
        if (pendingHotspotLeases_.isEmpty()) return;
        if (!known) { completeHotspotLeaseRequests(false); return; }
        if (active) { completeHotspotLeaseRequests(true); return; }
        // Persist ownership intent before running nmcli so an unclean exit
        // during activation can recover and release this app-owned AP later.
        hotspotOpenedByTerminal_ = true;
        setHotspotOwnershipMarker(true);
        setHotspot(true, [this](bool ready, bool enabled) {
            if (!(ready && enabled) && !hotspotUserPinned_) {
                hotspotOpenedByTerminal_ = false;
                setHotspotOwnershipMarker(false);
            }
            completeHotspotLeaseRequests(ready && enabled);
            if (pendingHotspotLeases_.isEmpty() && hotspotLeases_.isEmpty() &&
                hotspotOpenedByTerminal_ && !hotspotUserPinned_) {
                setHotspot(false, [this](bool offKnown, bool off) {
                    if (offKnown && !off) {
                        hotspotOpenedByTerminal_ = false;
                        setHotspotOwnershipMarker(false);
                    }
                });
            }
        });
    });
}
void SystemTools::completeHotspotLeaseRequests(bool ready) {
    const auto owners = pendingHotspotLeases_.values();
    pendingHotspotLeases_.clear();
    if (ready) for (const auto &owner : owners) hotspotLeases_.insert(owner);
    for (const auto &owner : owners) emit hotspotLeaseReady(owner, ready);
}
QString SystemTools::hotspotOwnershipMarker() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/hotspot-owned-by-terminal";
}
void SystemTools::setHotspotOwnershipMarker(bool owned) {
    const QString path = hotspotOwnershipMarker();
    if (owned) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile marker(path);
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            marker.write("pi-module-ap\n");
            marker.close();
        }
    } else QFile::remove(path);
}
void SystemTools::setHotspot(bool enabled, std::function<void(bool, bool)> done) {
    if (hotspotBusy_) {
        if (done) QTimer::singleShot(100, this, [this, enabled, done] { setHotspot(enabled, done); });
        return;
    }
    hotspotBusy_ = true; emit changed();
    auto activate=[this,enabled,done](bool known, bool active) {
        if (known && active == enabled) {
            hotspotBusy_ = false; emit changed();
            log(enabled ? "热点已开启" : "热点已关闭");
            if (done) done(true, active);
            QTimer::singleShot(0, this, &SystemTools::processHotspotLeaseRequests);
            return;
        }
        auto *p = new QProcess(this);
        auto timedOut = std::make_shared<bool>(false);
        QTimer::singleShot(35000,p,[p,timedOut] { if(p->state()!=QProcess::NotRunning){*timedOut=true;p->kill();} });
        connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,p,enabled,timedOut,done](int code,auto) {
            const auto error=QString::fromUtf8(p->readAllStandardError()).trimmed();
            p->deleteLater();
            queryHotspot([this,enabled,code,error,timedOut,done](bool known,bool active) {
                hotspotBusy_=false;emit changed();
                if (*timedOut) log("热点操作超时，请检查NetworkManager");
                else if (known && active==enabled) log(enabled?"热点已开启":"热点已关闭");
                else log("热点操作未完成："+(error.isEmpty()?QString(code?"NetworkManager返回失败":"无法确认实际状态"):error.left(240)));
                refreshStatus();
                if (done) done(known && active == enabled, active);
                QTimer::singleShot(0, this, &SystemTools::processHotspotLeaseRequests);
            });
        });
        connect(p,&QProcess::errorOccurred,this,[this,p,done,enabled](auto error) {
            if(error!=QProcess::FailedToStart)return;
            hotspotBusy_=false;emit changed();log("热点进程无法启动："+p->errorString());p->deleteLater();
            if (done) done(false, hotspotActive_);
            QTimer::singleShot(0, this, &SystemTools::processHotspotLeaseRequests);
        });
        QStringList args{"--wait","30","connection",enabled ? "up" : "down","pi-module-ap"};
        // Optional target deployment grants these two exact commands only.
        // Desktop installs keep using their normal NetworkManager policy.
        if (qEnvironmentVariableIntValue("ORANGE_PI_HOTSPOT_SUDO") == 1) {
            args.prepend("/usr/bin/nmcli"); args.prepend("-n");
            p->start("sudo", args);
        } else p->start("nmcli", args);
    };
    // A down request against an already inactive profile is a successful no-op.
    // Both paths confirm actual NM state; never infer AP state from Wi-Fi IP.
    queryHotspot(activate);
}
void SystemTools::requestPowerAction(const QString &action) {
    if (powerActionBusy_) return;
    const QString command = action == "poweroff" ? "poweroff" : action == "reboot" ? "reboot" : QString{};
    if (command.isEmpty()) { log("拒绝未知电源操作"); return; }
    powerActionBusy_ = true; emit changed();
    auto *process = new QProcess(this);
    auto *timeout = new QTimer(process); timeout->setSingleShot(true); timeout->setInterval(8000);
    connect(timeout, &QTimer::timeout, process, [process] {
        if (process->state() != QProcess::NotRunning) process->kill();
    });
    connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
        [this,process,timeout,command](int exitCode,QProcess::ExitStatus) {
            timeout->stop();
            const QString error=QString::fromUtf8(process->readAllStandardError()).trimmed().left(240);
            powerActionBusy_=false; emit changed();
            if (exitCode==0) log(command=="poweroff"?"已向系统提交关机请求":"已向系统提交重启请求");
            else log("电源操作失败（sudo/systemctl权限不足或系统拒绝）："+
                     (error.isEmpty()?QString("退出码 %1").arg(exitCode):error));
            process->deleteLater();
        });
    connect(process, &QProcess::errorOccurred, this, [this,process,timeout](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        timeout->stop(); powerActionBusy_=false; emit changed();
        log("无法启动sudo，未执行电源操作"); process->deleteLater();
    });
    // Fixed executable and strict action allowlist; -n avoids hidden prompts.
    process->start("sudo", {"-n", "/usr/bin/systemctl", command});
    timeout->start();
}
void SystemTools::queryHotspot(std::function<void(bool,bool)> done) {
    if (hotspotQueryBusy_) {
        if(done) QTimer::singleShot(100,this,[this,done] { queryHotspot(done); });
        return;
    }
    hotspotQueryBusy_=true;
    auto *p=new QProcess(this);
    auto finish=[this,p,done](bool ok) {
        if (!p || p->property("queryFinished").toBool()) return;
        p->setProperty("queryFinished", true);
        const auto names=QString::fromUtf8(p->readAllStandardOutput()).split('\n');
        hotspotKnown_=ok;hotspotActive_=ok && names.contains("pi-module-ap");
        hotspotQueryBusy_=false;
        deviceStatus_["hotspotActive"]=hotspotActive_;deviceStatus_["hotspotKnown"]=hotspotKnown_;
        emit changed();p->deleteLater();if(done)done(hotspotKnown_,hotspotActive_);
    };
    connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int code,auto) {finish(code==0);});
    connect(p,&QProcess::errorOccurred,this,[finish](auto error) {if(error==QProcess::FailedToStart)finish(false);});
    QTimer::singleShot(5000,p,[p] {if(p->state()!=QProcess::NotRunning)p->kill();});
    p->start("nmcli",{"-t","-f","NAME","connection","show","--active"});
}
