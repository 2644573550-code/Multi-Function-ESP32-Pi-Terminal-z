#include "VlcVideoItem.h"

#include <QMutexLocker>
#include <QFileInfo>
#include <QPainter>
#include <QtCore/qglobal.h>
#include <vlc/vlc.h>
#include <vlc/libvlc_version.h>
#include <cmath>
#include <cstring>

#if LIBVLC_VERSION_MAJOR != 3
#error "This backend requires the VLC 3.x SDK (VLC 4 has a different API)."
#endif

VlcVideoItem::VlcVideoItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(true);
    timer_.setInterval(33); // Bound scene updates; never enqueue one task per frame.
    connect(&timer_, &QTimer::timeout, this, &VlcVideoItem::refreshStatus);
    outputRestartTimer_.setSingleShot(true);
    connect(&outputRestartTimer_, &QTimer::timeout, this, [this] {
        if (!outputRestartPending_) return;
        outputRestartPending_ = false;
        outputRestartPosition_ = 0;
        if (outputRestartPaused_ && player_) {
            libvlc_media_player_pause(player_);
            playing_ = false;
        }
        outputRestartPaused_ = false;
        emit statusChanged();
    });
}

VlcVideoItem::~VlcVideoItem() {
    timer_.stop();
    // stop/release join VLC callbacks before their opaque pointer/buffers die.
    if (player_) {
        libvlc_media_player_stop(player_);
        libvlc_media_player_release(player_);
    }
    if (instance_) libvlc_release(instance_);
    qFreeAligned(pixels_);
}

bool VlcVideoItem::ensurePlayer() {
    if (player_) return true;
    if (!instance_) {
        const char *args[] = {"--no-video-title-show", "--no-osd", "--quiet"};
        instance_ = libvlc_new(3, args);
        if (!instance_) {
            setError(QStringLiteral("VLC initialization failed; check VLC plugins/runtime."));
            return false;
        }
    }
    player_ = libvlc_media_player_new(instance_);
    if (!player_) {
        libvlc_release(instance_);
        instance_ = nullptr;
        setError(QStringLiteral("VLC media player creation failed."));
        return false;
    }
    libvlc_video_set_callbacks(player_, lock, unlock, display, this);
    libvlc_video_set_format_callbacks(player_, setup, nullptr);
    if (libvlc_event_attach(libvlc_media_player_event_manager(player_),
                           libvlc_MediaPlayerEncounteredError, event, this) != 0) {
        libvlc_media_player_release(player_); player_ = nullptr;
        libvlc_release(instance_); instance_ = nullptr;
        setError(QStringLiteral("VLC error callback registration failed."));
        return false;
    }
    applyAudio();
    return true;
}

void VlcVideoItem::setError(const QString &message) {
    if (error_ == message) return;
    error_ = message;
    emit errorChanged();
}

void VlcVideoItem::setSource(const QUrl &source) {
    if (source_ == source) return;
    stop();
    source_ = source;
    duration_ = 0;
    seekable_ = false;
    decodedFrames_ = 0;
    playbackError_ = false;
    presentedFrames_ = 0;
    {
        QMutexLocker guard(&frameMutex_);
        frame_ = QImage();
    }
    setError({});
    update();
    emit sourceChanged();
    emit statusChanged();
}

void VlcVideoItem::play() {
    if (source_.isEmpty()) return;
    if (source_.isLocalFile()) {
        const QFileInfo file(source_.toLocalFile());
        if (!file.isFile() || !file.isReadable()) {
            setError(QStringLiteral("Video file does not exist or is not readable."));
            return;
        }
    }
    if (!source_.isValid() || (!source_.isLocalFile()
        && source_.scheme() != QStringLiteral("http")
        && source_.scheme() != QStringLiteral("https"))) {
        setError(QStringLiteral("Unsupported media URL (use file, http or https)."));
        return;
    }
    if (!ensurePlayer()) return;
    playbackError_ = false;
    setError({});
    libvlc_media_t *current = libvlc_media_player_get_media(player_);
    // VLC 3 does not reliably restart a media player left in Ended. Recreate
    // the player and media, then seek after the new decoder reports seekable.
    if (current && libvlc_media_player_get_state(player_) == libvlc_Ended) {
        const qint64 resumeAt = duration_ > 0 && position_ >= duration_
            ? 0 : qBound<qint64>(0, position_, duration_);
        libvlc_media_release(current);
        current = nullptr;
        libvlc_media_player_stop(player_);
        libvlc_media_player_release(player_);
        player_ = nullptr;
        if (!ensurePlayer()) return;
        if (resumeAt > 0) {
            outputRestartPending_ = true;
            outputRestartPaused_ = false;
            outputRestartPosition_ = resumeAt;
            outputRestartTimer_.start(3000);
        }
    }
    // setSource()/stop() clears the attached media; pause/resume retains it.
    if (!current) {
        const QByteArray encoded = source_.toEncoded();
        libvlc_media_t *media = libvlc_media_new_location(instance_, encoded.constData());
        if (!media) { setError(QStringLiteral("VLC could not open media.")); return; }
        libvlc_media_player_set_media(player_, media);
        libvlc_media_release(media);
    } else {
        libvlc_media_release(current);
    }
    if (libvlc_media_player_play(player_) < 0) {
        setError(QStringLiteral("VLC could not start playback."));
        return;
    }
    loading_ = true;
    timer_.setInterval(33);
    timer_.start();
    emit statusChanged();
}

void VlcVideoItem::pause() {
    if (!player_) return;
    libvlc_media_player_set_pause(player_, 1);
    refreshStatus();
}

void VlcVideoItem::stop() {
    timer_.stop();
    outputRestartTimer_.stop();
    outputRestartPending_ = false;
    if (player_) {
        libvlc_media_player_stop(player_);
        libvlc_media_player_set_media(player_, nullptr);
    }
    playing_ = loading_ = seekable_ = false;
    position_ = 0;
    decodedFrames_ = 0;
    presentedFrames_ = 0;
    {
        QMutexLocker guard(&frameMutex_);
        frame_ = QImage();
    }
    update();
    emit statusChanged();
}

void VlcVideoItem::setPosition(qint64 position) {
    if (!player_ || position < 0) return;
    const auto state = libvlc_media_player_get_state(player_);
    const bool ended = state == libvlc_Ended;
    if (!seekable_ && !ended) return;
    const qint64 target = qBound<qint64>(0, position, duration_);
    libvlc_media_player_set_time(player_, target);
    position_ = target;
    // VLC can keep reporting Ended after a successful seek. Reading status
    // immediately would replace the requested position with the end position.
    if (ended) emit statusChanged();
    else refreshStatus();
}

void VlcVideoItem::setVolume(qreal volume) {
    if (!std::isfinite(volume)) return;
    volume = qBound<qreal>(0, volume, 1);
    if (qFuzzyCompare(volume_, volume)) return;
    volume_ = volume;
    applyAudio();
    emit volumeChanged();
}

void VlcVideoItem::setMuted(bool muted) {
    if (muted_ == muted) return;
    muted_ = muted;
    applyAudio();
    emit mutedChanged();
}

void VlcVideoItem::setOutputHeight(int height) {
    if (height != 480 && height != 720 && height != 1080) return;
    if (outputHeight_ == height) return;

    const auto state = player_ ? libvlc_media_player_get_state(player_) : libvlc_NothingSpecial;
    const bool restart = state == libvlc_Playing || state == libvlc_Paused ||
                         state == libvlc_Opening || state == libvlc_Buffering;
    const bool wasPaused = state == libvlc_Paused;
    const qint64 resumePosition = seekable_ ? position_ : 0;

    outputHeight_ = height;
    emit outputHeightChanged();
    if (!restart) {
        // A stopped/ended item will use this output cap on its next play.
        if (state == libvlc_Ended) stop();
        return;
    }

    stop();
    if (player_) {
        // Stopping and reusing a VLC player may leave its vmem output pipeline
        // in the old negotiated format. Releasing it guarantees the selected
        // output cap is negotiated from scratch.
        libvlc_media_player_release(player_);
        player_ = nullptr;
    }
    if (!ensurePlayer()) return;
    outputRestartPending_ = true;
    outputRestartPaused_ = wasPaused;
    outputRestartPosition_ = resumePosition;
    play();
    outputRestartTimer_.start(3000);
}

void VlcVideoItem::applyAudio() {
    if (!player_) return;
    libvlc_audio_set_volume(player_, qRound(volume_ * 100));
    libvlc_audio_set_mute(player_, muted_);
}

void VlcVideoItem::refreshStatus() {
    if (!player_) return;
    const auto state = libvlc_media_player_get_state(player_);
    const bool wasPlaying = playing_;
    playing_ = state == libvlc_Playing;
    loading_ = state == libvlc_Opening || state == libvlc_Buffering;
    duration_ = qMax<qint64>(0, libvlc_media_player_get_length(player_));
    if (state == libvlc_Ended) {
        if (duration_ > 0) position_ = duration_;
        // Keep the last in-playback seekability result while VLC is Ended.
    } else {
        position_ = qMax<qint64>(0, libvlc_media_player_get_time(player_));
        seekable_ = libvlc_media_player_is_seekable(player_) != 0;
    }
    if (outputRestartPending_ && state == libvlc_Playing &&
        (outputRestartPosition_ <= 0 || (seekable_ && duration_ > 0))) {
        if (outputRestartPosition_ > 0 && seekable_) {
            libvlc_media_player_set_time(player_, qBound<qint64>(0, outputRestartPosition_, duration_));
            position_ = qBound<qint64>(0, outputRestartPosition_, duration_);
        }
        outputRestartPending_ = false;
        outputRestartTimer_.stop();
        if (outputRestartPaused_) {
            libvlc_media_player_pause(player_);
            playing_ = false;
        }
        outputRestartPosition_ = 0;
        outputRestartPaused_ = false;
    }
    if (playing_ && !wasPlaying) applyAudio();
    const auto frames = decodedFrames_.load();
    if (frames != presentedFrames_) { presentedFrames_ = frames; update(); }
    const bool failed = playbackError_.exchange(false) || state == libvlc_Error;
    if (failed) {
        playing_ = loading_ = seekable_ = false;
        setError(QStringLiteral("VLC could not decode or read this media."));
    }
    if (state == libvlc_Ended || failed)
        timer_.stop();
    else timer_.setInterval(state == libvlc_Paused ? 250 : 33);
    emit statusChanged();
}

unsigned VlcVideoItem::setup(void **opaque, char *chroma, unsigned *width,
                             unsigned *height, unsigned *pitches, unsigned *lines) {
    auto *self = static_cast<VlcVideoItem *>(*opaque);
    if (!*width || !*height) return 0;
    // VLC RV32 frames are copied into Qt Quick memory; keep a user-selectable
    // resolution cap so Raspberry Pi 3 users can trade detail for CPU/bandwidth.
    const unsigned maxHeight = unsigned(self->outputHeight_);
    const unsigned maxWidth = (maxHeight * 16U) / 9U;
    const QSize size = QSize(int(*width), int(*height)).scaled(
        int(maxWidth), int(maxHeight), Qt::KeepAspectRatio);
    *width = qMin(*width, unsigned(qMax(1, size.width())));
    *height = qMin(*height, unsigned(qMax(1, size.height())));
    pitches[0] = (*width * 4 + 31) & ~31U;
    lines[0] = (*height + 31) & ~31U;
    std::memcpy(chroma, "RV32", 4);
    QMutexLocker guard(&self->frameMutex_);
    qFreeAligned(self->pixels_);
    self->pixels_ = static_cast<uchar *>(qMallocAligned(size_t(pitches[0]) * lines[0], 32));
    self->frameWidth_ = *width;
    self->frameHeight_ = *height;
    self->pitch_ = pitches[0];
    return self->pixels_ ? 1 : 0;
}

void *VlcVideoItem::lock(void *opaque, void **planes) {
    auto *self = static_cast<VlcVideoItem *>(opaque);
    self->frameMutex_.lock();
    planes[0] = self->pixels_;
    return nullptr;
}

void VlcVideoItem::unlock(void *opaque, void *, void *const *) {
    auto *self = static_cast<VlcVideoItem *>(opaque);
    self->frame_ = QImage(self->pixels_, int(self->frameWidth_), int(self->frameHeight_),
                          int(self->pitch_), QImage::Format_RGB32).copy();
    self->frameMutex_.unlock();
}

void VlcVideoItem::display(void *opaque, void *) {
    static_cast<VlcVideoItem *>(opaque)->decodedFrames_.fetch_add(1);
}

void VlcVideoItem::event(const libvlc_event_t *, void *opaque) {
    // An error may transition to Stopped between polls. Remember it without
    // touching QObject/QML from a VLC worker thread or queuing stale callbacks.
    static_cast<VlcVideoItem *>(opaque)->playbackError_.store(true);
}

void VlcVideoItem::paint(QPainter *painter) {
    painter->fillRect(boundingRect(), Qt::black);
    QImage image;
    { QMutexLocker guard(&frameMutex_); image = frame_; }
    if (image.isNull()) return;
    const QSizeF scaled = QSizeF(image.size()).scaled(boundingRect().size(), Qt::KeepAspectRatio);
    const QRectF target((width() - scaled.width()) / 2, (height() - scaled.height()) / 2,
                        scaled.width(), scaled.height());
    painter->drawImage(target, image);
}
