#include "MusicService.h"
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QTimer>
#include <QNetworkRequest>
#include <memory>
#include <algorithm>

namespace {
constexpr qint64 kMaxSeekCacheBytes = 512LL * 1024 * 1024;
QString idText(const QJsonValue &value) {
    return value.isString() ? value.toString() : QString::number(static_cast<qint64>(value.toDouble()));
}
QString credentialPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/music-session";
}
}

struct MusicService::SeekCacheJob {
    QString id;
    QString path;
    QUrl source;
    std::unique_ptr<QSaveFile> file;
    QPointer<QNetworkReply> reply;
    qint64 received = 0;
    qint64 total = -1;
    qint64 otherCacheBytes = 0;
    QString failure;
    quint64 generation = 0;
};

MusicService::MusicService(QObject *parent) : QObject(parent) {
    QFile file(credentialPath());
    if (file.open(QIODevice::ReadOnly)) cookie_ = QString::fromUtf8(file.readAll());
    if (!cookie_.isEmpty()) account_ = QStringLiteral("已保存登录");
}

MusicService::~MusicService() {
    cancelSeekCacheJob();
    if (service_.state() != QProcess::NotRunning) {
        service_.terminate();
        if (!service_.waitForFinished(1500)) { service_.kill(); service_.waitForFinished(1000); }
    }
}

bool MusicService::localService() const {
    const QUrl url(endpoint());
    return url.scheme() == "http" && url.host() == "127.0.0.1" && url.port(80) == 3000;
}

void MusicService::ensureService(std::function<void(QString)> callback, int attempts) {
    if (!localService()) { callback({}); return; }
    auto *socket = new QTcpSocket(this);
    auto done = std::make_shared<bool>(false);
    connect(socket, &QTcpSocket::connected, this, [socket, done, callback] {
        if (*done) return;
        *done = true; socket->deleteLater(); callback({});
    });
    QTimer::singleShot(200, socket, [this, socket, done, callback, attempts] {
        if (*done) return;
        *done = true; socket->abort(); socket->deleteLater();
        if (attempts >= 50) { callback(QStringLiteral("本机音乐 API 启动超时，请检查安装")); return; }
        if (service_.state() == QProcess::NotRunning) {
            if (attempts > 0) { callback(QStringLiteral("本机音乐 API 已退出，请运行音乐依赖安装脚本")); return; }
            const auto node = QStandardPaths::findExecutable("node");
            QString script = QCoreApplication::applicationDirPath() + "/../libexec/orange-pi-terminal/music-api/server.cjs";
            if (!QFileInfo::exists(script)) script = QStringLiteral(TERMINAL_MUSIC_API_SOURCE) + "/server.cjs";
            if (node.isEmpty() || !QFileInfo::exists(script)) {
                callback(QStringLiteral("缺少音乐 API 或 Node.js，请运行 scripts/install-music-api.sh")); return;
            }
            const auto state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/music-api";
            QDir().mkpath(state);
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("HOST", "127.0.0.1"); env.insert("PORT", "3000"); env.insert("TMPDIR", state);
            service_.setProcessEnvironment(env);
            service_.setWorkingDirectory(state);
            service_.setStandardOutputFile(state + "/service.log", QIODevice::Append);
            service_.setStandardErrorFile(state + "/service.log", QIODevice::Append);
            service_.start(node, {script});
        }
        ensureService(callback, attempts + 1);
    });
    socket->connectToHost("127.0.0.1", 3000);
}

QString MusicService::endpoint() const {
    return settings_.value("musicApiEndpoint", "http://127.0.0.1:3000").toString();
}

void MusicService::setEndpoint(const QString &value) {
    QUrl url(value.trimmed());
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https") ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) {
        error_ = QStringLiteral("请输入有效的 API 地址（http/https）"); emit changed(); return;
    }
    if (endpoint() == value.trimmed()) return;
    cancelPlaybackRequest(); ++browsing_; ++login_;
    busy_ = false; rows_.clear(); qrKey_.clear(); qrUrl_.clear(); account_.clear(); cookie_.clear();
    QFile::remove(credentialPath());
    settings_.setValue("musicApiEndpoint", value.trimmed());
    error_.clear(); emit rowsChanged(); emit changed();
}

void MusicService::request(const QString &path, const QMap<QString, QString> &params, Callback callback) {
    ensureService([this, path, params, callback](QString startupError) {
    if (!startupError.isEmpty()) { callback({}, startupError); return; }
    QUrl url(endpoint() + (endpoint().endsWith('/') ? path.mid(1) : path));
    QNetworkRequest request;
    QUrlQuery query;
    for (auto it = params.cbegin(); it != params.cend(); ++it) query.addQueryItem(it.key(), it.value());
    // Login credentials are sent in a POST body, not URL/logs.
    if (!cookie_.isEmpty()) query.addQueryItem("cookie", cookie_);
    request.setUrl(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setTransferTimeout(12000);
    auto *reply = network_.post(request, query.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, callback] {
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        QString error;
        if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        if (object.contains("code") && object.value("code").toInt() != 200 && object.value("code").toInt() < 800)
            error = object.value("message").toString(object.value("msg").toString(QStringLiteral("API 请求失败")));
        if (object.isEmpty() && error.isEmpty()) error = QStringLiteral("API 返回格式错误");
        reply->deleteLater();
        callback(object, error);
    });
    });
}

void MusicService::browse(const QString &path, const QMap<QString, QString> &params, bool playlists) {
    const auto owner = path == "/user/playlist" ? params.value("uid") : QString{};
    const auto scope = playlistScope_;
    const auto generation = ++browsing_;
    rows_.clear(); busy_ = true; error_.clear(); emit rowsChanged(); emit changed();
    request(path, params, [this, generation, playlists, owner, scope](QJsonObject object, QString error) {
        if (generation != browsing_) return;
        busy_ = false; error_ = error;
        auto records = object.value("songs").toArray();
        if (records.isEmpty() && playlists) records = object.value("playlist").toArray();
        if (records.isEmpty()) records = object.value("data").toObject().value("dailySongs").toArray();
        if (records.isEmpty()) records = object.value("playlist").toObject().value("tracks").toArray();
        const auto result = object.value("result").toObject();
        if (records.isEmpty()) records = result.value(playlists ? "playlists" : "songs").toArray();
        for (const auto &value : records) {
            const auto song = value.toObject();
            if (!owner.isEmpty()) {
                const bool own = idText(song.value("creator").toObject().value("userId")) == owner;
                if ((scope == "created") != own) continue;
            }
            const auto album = song.value("al").toObject().isEmpty() ? song.value("album").toObject() : song.value("al").toObject();
            auto artists = song.value("ar").toArray();
            if (artists.isEmpty()) artists = song.value("artists").toArray();
            QStringList names;
            for (const auto &artist : artists) names.append(artist.toObject().value("name").toString());
            const auto originalId = idText(song.value("id"));
            rows_.append(QVariantMap{{"id", originalId}, {"originalId", originalId}, {"encryptedId", originalId}, {"name", song.value("name").toString()},
                {"artist", playlists ? song.value("creator").toObject().value("nickname").toString() : names.join(" / ")},
                {"cover", playlists ? song.value("coverImgUrl").toString() : album.value("picUrl").toString()},
                {"ownerId", idText(song.value("creator").toObject().value("userId"))},
                {"duration", song.value("dt").toDouble(song.value("duration").toDouble())}, {"playlist", playlists}});
        }
        emit rowsChanged(); emit changed();
    });
}

void MusicService::search(const QString &keyword, bool playlists) {
    if (keyword.trimmed().isEmpty()) return;
    browse("/cloudsearch", {{"keywords", keyword.trimmed()}, {"type", playlists ? "1000" : "1"}, {"limit", "50"}}, playlists);
}

void MusicService::playlist(const QString &id, int offset) {
    browse("/playlist/track/all", {{"id", id}, {"limit", "50"}, {"offset", QString::number(qMax(0, offset))}}, false);
}

void MusicService::myPlaylists(const QString &scope) {
    request("/login/status", {}, [this, scope](QJsonObject object, QString error) {
        const auto profile = object.value("data").toObject().value("profile").toObject();
        if (!error.isEmpty() || profile.isEmpty()) { error_ = QStringLiteral("请先扫码登录"); emit changed(); return; }
        const auto uid = idText(profile.value("userId"));
        playlistOwner_ = uid;
        playlistScope_ = scope;
        browse("/user/playlist", {{"uid", uid}, {"limit", "100"}}, true);
    });
}

void MusicService::recommendations() { browse("/recommend/songs", {}, false); }

void MusicService::cancelPlaybackRequest() { ++playback_; }

QString MusicService::seekCacheDirectory() const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/online-audio");
}

qint64 MusicService::seekCacheBytes(const QString &excludePath) const {
    QDir dir(seekCacheDirectory());
    if (!dir.exists()) return 0;
    static const QRegularExpression cacheName(QStringLiteral("^[a-f0-9]{64}\\.(mp3|m4a|aac|flac|ogg|oga|opus|wav)$"),
                                               QRegularExpression::CaseInsensitiveOption);
    qint64 total = 0;
    for (const auto &entry : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (entry.absoluteFilePath() == excludePath || !cacheName.match(entry.fileName()).hasMatch()) continue;
        total += entry.size();
    }
    return total;
}

bool MusicService::makeSeekCacheSpace(qint64 requiredBytes, const QString &excludePath) {
    if (requiredBytes < 0 || requiredBytes > kMaxSeekCacheBytes) return false;
    QDir dir(seekCacheDirectory());
    static const QRegularExpression cacheName(QStringLiteral("^[a-f0-9]{64}\\.(mp3|m4a|aac|flac|ogg|oga|opus|wav)$"),
                                               QRegularExpression::CaseInsensitiveOption);
    auto entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const QFileInfo &entry) {
        return entry.absoluteFilePath() == excludePath || !cacheName.match(entry.fileName()).hasMatch();
    }), entries.end());
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });
    qint64 current = seekCacheBytes(excludePath);
    for (const auto &entry : entries) {
        if (current + requiredBytes <= kMaxSeekCacheBytes) break;
        if (QFile::remove(entry.absoluteFilePath())) current -= entry.size();
    }
    return current + requiredBytes <= kMaxSeekCacheBytes;
}

void MusicService::cancelSeekCacheJob() {
    ++seekCacheGeneration_;
    if (!seekCacheJob_) return;
    if (seekCacheJob_->reply) {
        seekCacheJob_->reply->disconnect(this);
        seekCacheJob_->reply->abort();
        seekCacheJob_->reply->deleteLater();
    }
    seekCacheJob_.reset(); // QSaveFile removes the uncommitted partial file.
}

void MusicService::cancelSeekAudioCache(const QString &id) {
    if (seekCacheJob_ && seekCacheJob_->id == id) cancelSeekCacheJob();
}

QUrl MusicService::cachedAudioForTrack(const QString &requestedId) {
    const QString id = requestedId.trimmed();
    if (id.isEmpty() || id.size() > 256) return {};
    const QByteArray cacheIdentity = endpoint().toUtf8() + '\0' + id.toUtf8();
    const QByteArray key = QCryptographicHash::hash(cacheIdentity, QCryptographicHash::Sha256).toHex();
    QDir dir(seekCacheDirectory());
    const auto files = dir.entryInfoList(QStringList(QString::fromLatin1(key) + QStringLiteral(".*")), QDir::Files, QDir::Time);
    for (const auto &file : files) {
        if (file.size() <= 0) continue;
        QFile touch(file.absoluteFilePath());
        if (touch.open(QIODevice::ReadOnly)) touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        return QUrl::fromLocalFile(file.absoluteFilePath());
    }
    return {};
}

void MusicService::cacheAudioForSeek(const QString &requestedId, const QString &sourceText) {
    const QString id = requestedId.trimmed();
    const QUrl source(sourceText.trimmed());
    QString validationError;
    if (id.isEmpty() || id.size() > 256) validationError = QStringLiteral("曲目标识无效");
    else if (!source.isValid()) validationError = QStringLiteral("音源 URL 格式无效（长度 %1，协议 %2）").arg(sourceText.size()).arg(source.scheme());
    else if (source.host().isEmpty()) validationError = QStringLiteral("音源 URL 缺少主机名");
    else if (source.scheme() != QStringLiteral("http") && source.scheme() != QStringLiteral("https")) validationError = QStringLiteral("只支持 HTTP/HTTPS 音源缓存");
    else if (!source.userInfo().isEmpty()) validationError = QStringLiteral("不接受内嵌用户凭据的音源 URL");
    if (!validationError.isEmpty()) {
        emit seekAudioCacheFailed(id, validationError);
        return;
    }
    if (seekCacheJob_ && seekCacheJob_->id == id && seekCacheJob_->source == source) return;
    cancelSeekCacheJob();

    const QString directory = seekCacheDirectory();
    if (!QDir().mkpath(directory)) {
        emit seekAudioCacheFailed(id, QStringLiteral("无法创建本地音频缓存目录"));
        return;
    }
    const QByteArray cacheIdentity = endpoint().toUtf8() + '\0' + id.toUtf8();
    const QByteArray key = QCryptographicHash::hash(cacheIdentity, QCryptographicHash::Sha256).toHex();
    QString extension = QFileInfo(source.path()).suffix().toLower();
    static const QSet<QString> supportedExtensions = {
        QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("flac"),
        QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("wav")
    };
    if (!supportedExtensions.contains(extension)) extension = QStringLiteral("mp3");
    const QString path = directory + QLatin1Char('/') + QString::fromLatin1(key) + QLatin1Char('.') + extension;
    const QUrl cachedSource = cachedAudioForTrack(id);
    if (cachedSource.isLocalFile()) {
        emit seekAudioCacheReady(id, cachedSource);
        return;
    }

    if (!makeSeekCacheSpace(0, path)) {
        emit seekAudioCacheFailed(id, QStringLiteral("本地音频缓存空间不足"));
        return;
    }
    seekCacheJob_ = std::make_unique<SeekCacheJob>();
    auto *job = seekCacheJob_.get();
    job->id = id;
    job->source = source;
    job->path = path;
    job->otherCacheBytes = seekCacheBytes(path);
    job->generation = ++seekCacheGeneration_;
    job->file = std::make_unique<QSaveFile>(path);
    job->file->setDirectWriteFallback(false);
    if (!job->file->open(QIODevice::WriteOnly)) {
        const QString message = job->file->errorString();
        seekCacheJob_.reset();
        emit seekAudioCacheFailed(id, QStringLiteral("无法写入音频缓存：") + message);
        return;
    }
    job->file->setPermissions(QFile::ReadOwner | QFile::WriteOwner);

    QNetworkRequest request(source);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    job->reply = network_.get(request);
    const quint64 generation = job->generation;
    connect(job->reply, &QNetworkReply::metaDataChanged, this, [this, generation] {
        if (!seekCacheJob_ || seekCacheJob_->generation != generation || !seekCacheJob_->reply) return;
        seekCacheJob_->total = seekCacheJob_->reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const int status = seekCacheJob_->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400) {
            seekCacheJob_->failure = QStringLiteral("音源服务器返回 HTTP %1").arg(status);
            seekCacheJob_->reply->abort();
        }
    });
    connect(job->reply, &QIODevice::readyRead, this, [this, generation] {
        if (!seekCacheJob_ || seekCacheJob_->generation != generation || !seekCacheJob_->reply || !seekCacheJob_->file) return;
        const QByteArray bytes = seekCacheJob_->reply->readAll();
        if (bytes.isEmpty()) return;
        const qint64 requestedBytes=seekCacheJob_->received + bytes.size();
        if (seekCacheJob_->otherCacheBytes + requestedBytes > kMaxSeekCacheBytes &&
            !makeSeekCacheSpace(requestedBytes, seekCacheJob_->path)) {
            seekCacheJob_->failure = QStringLiteral("音频缓存超过 512 MiB 上限或磁盘空间不足");
            seekCacheJob_->reply->abort();
            return;
        }
        if (seekCacheJob_->otherCacheBytes + requestedBytes > kMaxSeekCacheBytes)
            seekCacheJob_->otherCacheBytes = seekCacheBytes(seekCacheJob_->path);
        if (seekCacheJob_->file->write(bytes) != bytes.size()) {
            seekCacheJob_->failure = QStringLiteral("写入音频缓存失败：") + seekCacheJob_->file->errorString();
            seekCacheJob_->reply->abort();
            return;
        }
        seekCacheJob_->received += bytes.size();
        emit seekAudioCacheProgress(seekCacheJob_->id, seekCacheJob_->received, seekCacheJob_->total);
    });
    connect(job->reply, &QNetworkReply::downloadProgress, this, [this, generation](qint64 received, qint64 total) {
        if (!seekCacheJob_ || seekCacheJob_->generation != generation) return;
        seekCacheJob_->total = total;
        emit seekAudioCacheProgress(seekCacheJob_->id, qMax(received, seekCacheJob_->received), total);
    });
    connect(job->reply, &QNetworkReply::finished, this, [this, generation] {
        if (!seekCacheJob_ || seekCacheJob_->generation != generation || !seekCacheJob_->reply) return;
        auto *reply = seekCacheJob_->reply.data();
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty()) {
            const qint64 requestedBytes=seekCacheJob_->received + tail.size();
            if ((seekCacheJob_->otherCacheBytes + requestedBytes > kMaxSeekCacheBytes &&
                 !makeSeekCacheSpace(requestedBytes, seekCacheJob_->path)) ||
                seekCacheJob_->file->write(tail) != tail.size()) {
                seekCacheJob_->failure = QStringLiteral("写入音频缓存失败：磁盘空间不足");
            } else {
                if (seekCacheJob_->otherCacheBytes + requestedBytes > kMaxSeekCacheBytes)
                    seekCacheJob_->otherCacheBytes = seekCacheBytes(seekCacheJob_->path);
                seekCacheJob_->received += tail.size();
            }
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString failure = seekCacheJob_->failure;
        if (failure.isEmpty() && reply->error() != QNetworkReply::NoError) failure = reply->errorString();
        if (failure.isEmpty() && status >= 400) failure = QStringLiteral("音源服务器返回 HTTP %1").arg(status);
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        if (failure.isEmpty() && (contentType.startsWith(QStringLiteral("text/")) || contentType.contains(QStringLiteral("json"))))
            failure = QStringLiteral("音源服务器没有返回音频数据");
        if (failure.isEmpty() && seekCacheJob_->received < 1024) failure = QStringLiteral("音源数据过短，缓存无效");
        const QString id = seekCacheJob_->id;
        const QString path = seekCacheJob_->path;
        const qint64 received = seekCacheJob_->received;
        if (failure.isEmpty() && !seekCacheJob_->file->commit()) failure = QStringLiteral("提交音频缓存失败：") + seekCacheJob_->file->errorString();
        reply->deleteLater();
        seekCacheJob_.reset();
        if (!failure.isEmpty()) {
            emit seekAudioCacheFailed(id, failure);
            return;
        }
        emit seekAudioCacheProgress(id, received, received);
        emit seekAudioCacheReady(id, QUrl::fromLocalFile(path));
    });
}

void MusicService::resolve(const QString &id) {
    const auto generation = ++playback_;
    error_.clear(); emit changed();
    request("/song/url", {{"id", id}, {"br", "320000"}}, [this, generation, id](QJsonObject object, QString error) {
        if (generation != playback_) return;
        const auto data = object.value("data").toArray();
        const auto track = data.isEmpty() ? QJsonObject{} : data.first().toObject();
        QUrl url(track.value("url").toString());
        if (error.isEmpty() && (!url.isValid() || url.host().isEmpty() ||
            (url.scheme() != "http" && url.scheme() != "https"))) error = QStringLiteral("该歌曲暂无音源或播放权限");
        if (error.isEmpty() && track.value("freeTrialInfo").isObject()) error = QStringLiteral("该歌曲仅提供试听，请检查账号权限");
        if (!error.isEmpty()) { error_ = error; emit changed(); emit sourceFailed(id, error); return; }
        emit sourceReady(id, url);
    });
}

void MusicService::login() {
    const auto generation = ++login_;
    qrKey_.clear(); qrUrl_.clear(); error_.clear(); emit changed();
    request("/login/qr/key", {{"timestamp", QString::number(QDateTime::currentMSecsSinceEpoch())}},
        [this, generation](QJsonObject object, QString error) {
            if (generation != login_) return;
            if (!error.isEmpty()) { error_ = error; emit changed(); return; }
            qrKey_ = object.value("data").toObject().value("unikey").toString();
            request("/login/qr/create", {{"key", qrKey_}, {"qrimg", "true"}}, [this, generation](QJsonObject object, QString error) {
                if (generation != login_) return;
                error_ = error; qrUrl_ = object.value("data").toObject().value("qrimg").toString(); emit changed();
            });
        });
}

void MusicService::checkLogin() {
    const auto generation = login_;
    if (qrKey_.isEmpty()) return;
    request("/login/qr/check", {{"key", qrKey_}, {"timestamp", QString::number(QDateTime::currentMSecsSinceEpoch())}},
        [this, generation](QJsonObject object, QString error) {
            if (generation != login_) return;
            error_ = error;
            if (object.value("code").toInt() == 803) {
                cookie_ = object.value("cookie").toString();
                account_ = QStringLiteral("已登录"); qrKey_.clear(); qrUrl_.clear();
                QDir().mkpath(QFileInfo(credentialPath()).absolutePath());
                QSaveFile file(credentialPath());
                if (file.open(QIODevice::WriteOnly)) {
                    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
                    file.write(cookie_.toUtf8()); file.commit();
                }
            } else if (object.value("code").toInt() == 800) {
                error_ = QStringLiteral("二维码已过期，请重新登录"); qrKey_.clear(); qrUrl_.clear();
            }
            emit changed();
        });
}

void MusicService::logout() {
    ++login_; cancelPlaybackRequest();
    cookie_.clear(); account_.clear(); qrKey_.clear(); qrUrl_.clear(); QFile::remove(credentialPath()); emit changed();
}

void MusicService::fetchLyrics(const QString &id) {
    const auto generation = ++lyricsRequest_;
    if (lyricCache_.contains(id)) {
        lyrics_ = lyricCache_.value(id); lyricsBusy_ = false; emit lyricsChanged(); return;
    }
    lyrics_.clear(); lyricsBusy_ = true; error_.clear(); emit lyricsChanged(); emit changed();
    request("/lyric", {{"id", id}}, [this, generation, id](QJsonObject object, QString error) {
        if (generation != lyricsRequest_) return;
        lyricsBusy_ = false;
        if (!error.isEmpty()) { error_ = error; emit lyricsChanged(); emit changed(); return; }
        const auto lrc = object.value("lrc").toObject().value("lyric").toString();
        const auto translated = object.value("tlyric").toObject().value("lyric").toString();
        static const QRegularExpression linePattern(QStringLiteral("\\[(\\d+):(\\d+(?:\\.\\d+)?)\\]"));
        static const QRegularExpression offsetPattern(QStringLiteral("\\[offset:([+-]?\\d+)\\]"));
        auto parse = [](const QString &text) {
            QVariantList lines;
            const auto offsetMatch = offsetPattern.match(text);
            const int offset = offsetMatch.hasMatch() ? offsetMatch.captured(1).toInt() : 0;
            for (const auto &line : text.split('\n')) {
                auto matches = linePattern.globalMatch(line);
                QString content = line; content.remove(linePattern); content = content.trimmed();
                while (matches.hasNext()) {
                    const auto match = matches.next();
                    const int time = match.captured(1).toInt() * 60000 + qRound(match.captured(2).toDouble() * 1000) + offset;
                    lines.append(QVariantMap{{"time", qMax(0, time)}, {"text", content}});
                }
            }
            std::stable_sort(lines.begin(), lines.end(), [](const QVariant &a, const QVariant &b) {
                return a.toMap().value("time").toInt() < b.toMap().value("time").toInt();
            });
            return lines;
        };
        QHash<int, QString> translations;
        for (const auto &line : parse(translated)) {
            const auto row = line.toMap();
            translations.insert(row.value("time").toInt(), row.value("text").toString());
        }
        for (const auto &line : parse(lrc)) {
            auto row = line.toMap(); row.insert("translation", translations.value(row.value("time").toInt()));
            lyrics_.append(row);
        }
        if (lyricCache_.size() >= 32) lyricCache_.clear();
        lyricCache_.insert(id, lyrics_);
        emit lyricsChanged(); emit changed();
    });
}
