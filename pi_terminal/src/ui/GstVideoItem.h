#pragma once

#include <QElapsedTimer>
#include <QSize>
#include <QMutex>
#include <QQuickFramebufferObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>
#include <atomic>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

class GstPlayerWorker;

// 自研播放内核（Kodi 模型）：
//   souphttpsrc ! hlsdemux ! queue ! tsdemux ! h264parse ! v4l2h264dec
//     ! videoconvert ! video/x-raw,format=NV12 ! appsink
//
// 管线在独立线程（GstPlayerWorker）里创建/销毁：换台时 release + 重建要 200ms+，
// 放 GUI 线程会明显卡一下；放工作线程还能保证旧管线彻底释放后再建新管线
// （Pi 上只有一个 /dev/video10，抢设备会直接起不来）。
//
// 解码线程只把 GstSample 挂进 6 槽环形队列（Kodi NUM_BUFFERS=6），渲染线程按 pts
// 取“此刻该显示的那一帧”；迟到 > 500ms 重新锚定；队列满则回压解码线程，
// 绝不把即将显示的旧帧丢掉。
class GstVideoItem : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(bool hardwareDecode READ hardwareDecode NOTIFY hardwareDecodeChanged)
    // 当前源的画面尺寸（用于播放区按比例自适应、消掉黑边）
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)

public:
    explicit GstVideoItem(QQuickItem *parent = nullptr);
    ~GstVideoItem() override;

    QString source() const { return source_; }
    void setSource(const QString &source);
    bool playing() const { return playing_; }
    QString errorText() const { return errorText_; }
    bool hardwareDecode() const { return hardwareDecode_; }
    QSize videoSize() const { return videoSize_; }

    Q_INVOKABLE void play();
    // 短时间暂停后直接 PAUSED→PLAYING，不用重建管线；长时间暂停则重新起播
    Q_INVOKABLE void resume();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void release();
    // 0.0–1.0，作用在管线的 volume 元素上（静音就是设 0）
    Q_INVOKABLE void setVolume(qreal volume);

    Renderer *createRenderer() const override;

    // 解码线程（GStreamer streaming thread）调用，接管 sample 引用
    void enqueueSample(GstSample *sample);

    // ── 渲染线程接口 ──
    // 取此刻该显示的采样（返回已加引用，渲染完必须 releaseFrame）。
    // 返回 nullptr 表示“还没有到期的帧”，渲染器应继续显示上一帧。
    GstSample *acquireFrame(qint64 nowNs, int *seq, int *width, int *height, int *strideY,
                            int *strideUV);
    void releaseFrame(GstSample *sample);

signals:
    void sourceChanged();
    void playingChanged();
    void errorTextChanged();
    void hardwareDecodeChanged();
    void videoSizeChanged();

private slots:
    void onWorkerStarted(bool hardware);
    void onWorkerFailed(const QString &message);
    void onWorkerEos();
    void onConnectTimeout();
    void onStatsTick();

private:
    friend class GstPlayerWorker;

    // Kodi 的 render buffer 数（NUM_BUFFERS=6）
    static constexpr int kSlots = 6;

    struct Slot {
        GstSample *sample = nullptr;
        qint64 pts = 0;   // 纳秒，媒体时间
        int seq = 0;
    };

    void setError(const QString &text);
    void clearFrames();
    bool hasDueFrame();                   // GUI 线程：是否有到期的帧

    QString source_;
    QString errorText_;
    bool playing_ = false;
    bool starting_ = false;
    bool hardwareDecode_ = false;
    QSize videoSize_;
    bool pipelineAlive_ = false;
    qint64 pausedAtMs_ = 0;
    qreal volume_ = 1.0;
    bool loggedFirstFrame_ = false;

    QThread workerThread_;
    GstPlayerWorker *worker_ = nullptr;
    QTimer *startTimer_ = nullptr;   // 起播超时（源不可用时尽快报错换下一个）
    QTimer *statsTimer_ = nullptr;

    // Kodi 的渲染线程按显示刷新率轮询渲染队列，这里用同样的办法：
    // 8ms 检查一次是否有到期帧，有才 update()，避免每帧都整窗重绘。
    QTimer *renderTick_ = nullptr;

    // ── 帧队列（解码线程写、渲染线程读）──
    QMutex frameMutex_;
    QWaitCondition frameSpace_;
    Slot slots_[kSlots];
    int slotSeq_ = 0;
    qint64 anchorPts_ = -1;    // 起播时第一帧的 pts
    qint64 anchorWall_ = 0;    // 起播时刻（steady clock 纳秒）
    int width_ = 0;
    int height_ = 0;
    int strideY_ = 0;
    int strideUV_ = 0;

    // ── 统计 ──
    std::atomic<int> sampleCount_{0};
    std::atomic<int> droppedLate_{0};
    std::atomic<int> droppedSkip_{0};
    int lastSampleCount_ = 0;
    int lastDroppedLate_ = 0;
    int lastDroppedSkip_ = 0;
};
