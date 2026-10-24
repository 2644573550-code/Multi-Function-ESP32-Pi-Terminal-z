#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QUrl>
#include <atomic>

struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_event_t;

// Application-owned integration of the VLC 3.x public API. No platform window
// handles: video remains in the scene, including Wayland and QML overlays.
class VlcVideoItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY statusChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY statusChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY statusChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY statusChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY statusChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(int outputHeight READ outputHeight WRITE setOutputHeight NOTIFY outputHeightChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(qulonglong decodedFrames READ decodedFrames NOTIFY statusChanged)
public:
    explicit VlcVideoItem(QQuickItem *parent = nullptr);
    ~VlcVideoItem() override;
    QUrl source() const { return source_; }
    void setSource(const QUrl &source);
    qint64 position() const { return position_; }
    void setPosition(qint64 position);
    qint64 duration() const { return duration_; }
    bool playing() const { return playing_; }
    bool loading() const { return loading_; }
    bool seekable() const { return seekable_; }
    qreal volume() const { return volume_; }
    void setVolume(qreal volume);
    bool muted() const { return muted_; }
    void setMuted(bool muted);
    int outputHeight() const { return outputHeight_; }
    void setOutputHeight(int height);
    QString errorString() const { return error_; }
    qulonglong decodedFrames() const { return decodedFrames_.load(); }
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    void paint(QPainter *painter) override;
signals:
    void sourceChanged();
    void statusChanged();
    void volumeChanged();
    void mutedChanged();
    void outputHeightChanged();
    void errorChanged();
private:
    bool ensurePlayer();
    void refreshStatus();
    void applyAudio();
    void setError(const QString &message);
    static unsigned setup(void **opaque, char *chroma, unsigned *width,
                          unsigned *height, unsigned *pitches, unsigned *lines);
    static void *lock(void *opaque, void **planes);
    static void unlock(void *opaque, void *picture, void *const *planes);
    static void display(void *opaque, void *picture);
    static void event(const libvlc_event_t *event, void *opaque);
    libvlc_instance_t *instance_ = nullptr;
    libvlc_media_player_t *player_ = nullptr;
    QTimer timer_;
    QTimer outputRestartTimer_;
    QUrl source_;
    qint64 position_ = 0, duration_ = 0;
    bool playing_ = false, loading_ = false, seekable_ = false, muted_ = false;
    int outputHeight_ = 480;
    bool outputRestartPending_ = false, outputRestartPaused_ = false;
    qint64 outputRestartPosition_ = 0;
    qreal volume_ = 0.8;
    QString error_;
    QMutex frameMutex_;
    uchar *pixels_ = nullptr;
    unsigned frameWidth_ = 0, frameHeight_ = 0, pitch_ = 0;
    QImage frame_;
    std::atomic<qulonglong> decodedFrames_{0};
    std::atomic<bool> playbackError_{false};
    qulonglong presentedFrames_ = 0;
};
