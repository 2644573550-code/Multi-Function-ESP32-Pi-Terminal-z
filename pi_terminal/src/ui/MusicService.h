#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QSettings>
#include <functional>
#include <QProcess>
#include <QPointer>

// Third-party API adapter only. Audio is decoded by Qt, never by Node or a CLI.
class MusicService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(QString qrUrl READ qrUrl NOTIFY changed)
    Q_PROPERTY(QString account READ account NOTIFY changed)
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(bool lyricsBusy READ lyricsBusy NOTIFY lyricsChanged)
public:
    explicit MusicService(QObject *parent = nullptr);
    ~MusicService() override;
    QString endpoint() const;
    bool busy() const { return busy_; }
    QString error() const { return error_; }
    QVariantList rows() const { return rows_; }
    QString qrUrl() const { return qrUrl_; }
    QString account() const { return account_; }
    QVariantList lyrics() const { return lyrics_; }
    bool lyricsBusy() const { return lyricsBusy_; }
    Q_INVOKABLE void setEndpoint(const QString &url);
    Q_INVOKABLE void search(const QString &keyword, bool playlists);
    Q_INVOKABLE void playlist(const QString &id, int offset);
    Q_INVOKABLE void myPlaylists(const QString &scope);
    Q_INVOKABLE void recommendations();
    Q_INVOKABLE void resolve(const QString &id);
    Q_INVOKABLE void cancelPlaybackRequest();
    Q_INVOKABLE QUrl cachedAudioForTrack(const QString &id);
    Q_INVOKABLE void cacheAudioForSeek(const QString &id, const QString &source);
    Q_INVOKABLE void cancelSeekAudioCache(const QString &id);
    Q_INVOKABLE void login();
    Q_INVOKABLE void checkLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void fetchLyrics(const QString &id);
signals:
    void changed();
    void rowsChanged();
    void lyricsChanged();
    void sourceReady(QString id, QUrl source);
    void sourceFailed(QString id, QString message);
    void seekAudioCacheProgress(QString id, qint64 received, qint64 total);
    void seekAudioCacheReady(QString id, QUrl localSource);
    void seekAudioCacheFailed(QString id, QString message);
private:
    struct SeekCacheJob;
    using Callback = std::function<void(QJsonObject, QString)>;
    void request(const QString &path, const QMap<QString, QString> &params, Callback callback);
    void browse(const QString &path, const QMap<QString, QString> &params, bool playlists);
    bool localService() const;
    void ensureService(std::function<void(QString)> callback, int attempts = 0);
    QString seekCacheDirectory() const;
    qint64 seekCacheBytes(const QString &excludePath = {}) const;
    bool makeSeekCacheSpace(qint64 requiredBytes, const QString &excludePath);
    void cancelSeekCacheJob();
    QProcess service_;
    QNetworkAccessManager network_;
    QSettings settings_;
    QVariantList rows_;
    QString error_, qrUrl_, qrKey_, account_, cookie_;
    QString playlistOwner_, playlistScope_;
    QVariantList lyrics_;
    QHash<QString, QVariantList> lyricCache_;
    std::unique_ptr<SeekCacheJob> seekCacheJob_;
    quint64 seekCacheGeneration_ = 0;
    bool lyricsBusy_ = false;
    bool busy_ = false;
    quint64 browsing_ = 0, playback_ = 0, login_ = 0, lyricsRequest_ = 0;
};
