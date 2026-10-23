#include "GstVideoItem.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QTimer>

#include <chrono>

namespace {

bool gstReady = false;

qint64 nowNs()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

bool gstElementAvailable(const char *name)
{
    GstElementFactory *factory = gst_element_factory_find(name);
    if (!factory)
        return false;
    gst_object_unref(factory);
    return true;
}

// 管线照抄 Kodi 的 demux → decode → 有界队列结构：
// - souphttpsrc 限时/限次重试（Kodi 直播重试 6×1s 的等价物，soup 用 retries）；
// - hlsdemux 用 connection-speed 卡码率（Kodi inputstream.adaptive 的 bandwidth.max）；
// - tsdemux 之后用 queue 吸收分片突发（Kodi 默认 4s 队列，这里按字节限 6MB）；
// - 解码后不再做 CPU 色彩转换之外的任何处理，NV12 直接进 GL 的 Y/UV 两张纹理。
QString pipelineFor(const QString &url, bool hardware)
{
    QString escaped = url;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    const QString decoder = hardware ? QStringLiteral("v4l2h264dec") : QStringLiteral("avdec_h264");
    const QString source = QStringLiteral("souphttpsrc location=\"%1\" is-live=true "
                                          "timeout=10 retries=5 keep-alive=true")
                               .arg(escaped);

    // 0 = 不限制；单位 kbps。给的码率上限会直接影响 hlsdemux 选哪一路变体。
    const int connectionSpeed = qEnvironmentVariableIntValue("TERMINAL_GST_CONNECTION_SPEED");
    const QString hls = connectionSpeed > 0
        ? QStringLiteral("hlsdemux connection-speed=%1").arg(connectionSpeed)
        : QStringLiteral("hlsdemux");

    const int queueBytes = qEnvironmentVariableIntValue("TERMINAL_GST_QUEUE_BYTES");
    const QString queue = QStringLiteral("queue max-size-buffers=0 max-size-time=0 max-size-bytes=%1")
                              .arg(queueBytes > 0 ? queueBytes : 6000000);

    // 音频默认开启；TERMINAL_GST_AUDIO=0 可回退到纯视频（排查音频设备问题时用）
    const bool withAudio = qEnvironmentVariable("TERMINAL_GST_AUDIO") != QStringLiteral("0");
    QString pipeline = source + QStringLiteral(" ! ") + hls + QStringLiteral(" ! ") + queue
        + QStringLiteral(" ! tsdemux name=demux")
        + QStringLiteral(" demux. ! queue ! h264parse ! ") + decoder
        + QStringLiteral(" ! videoconvert ! video/x-raw,format=NV12 ! ")
        + QStringLiteral("appsink name=sink sync=false max-buffers=4 drop=true "
                         "emit-signals=false");
    if (withAudio) {
        // 音频分支用 3 秒的 leaky queue：
        // - 重建管线/网络抖动时，宁可丢一点音频也不能把 tsdemux 堵死（视频会停）；
        // - 但缓冲要足够长，否则视频一慢音频就先被丢，听起来就是“卡”。
        pipeline += QStringLiteral(" demux. ! queue leaky=downstream max-size-buffers=0")
            + QStringLiteral(" max-size-time=3000000000 max-size-bytes=0")
            + QStringLiteral(" ! decodebin ! audioconvert ! audioresample")
            + QStringLiteral(" ! volume name=vol ! autoaudiosink");
    }
    return pipeline;
}

// NVIDIA 之外的 VC4 只有 GL 2.1：没有 R8/RG8 时必须退回 Luminance/LuminanceAlpha。
// 这里的纹理上传直接带 GL_UNPACK_ROW_LENGTH 从 GstVideoFrame 的行指针拷，
// 不再先 memcpy 到自定义缓冲（省掉每帧一次 1080p 的全帧拷贝）。
class GstRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions {
public:
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        auto *video = static_cast<GstVideoItem *>(item);
        if (pending_) {
            video->releaseFrame(pending_);
            pending_ = nullptr;
        }
        int seq = 0;
        int w = 0, h = 0, sy = 0, suv = 0;
        pending_ = video->acquireFrame(nowNs(), &seq, &w, &h, &sy, &suv);
        if (pending_) {
            frameSeq_ = seq;
            frameWidth_ = w;
            frameHeight_ = h;
            strideY_ = sy;
            strideUV_ = suv;
        }
    }

    void render() override
    {
        if (!program_ && !initGl())
            return;
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (pending_ && frameSeq_ != uploadedSeq_)
            uploadFrame();

        if (frameWidth_ <= 0 || frameHeight_ <= 0 || !texY_ || !texUV_)
            return;

        const QSize fbo = framebufferObject()->size();
        if (fbo.isEmpty())
            return;
        const float srcA = float(frameWidth_) / float(frameHeight_);
        const float dstA = float(fbo.width()) / float(fbo.height());
        float sx = 1.f, sy = 1.f;
        if (srcA > dstA)
            sy = dstA / srcA;
        else
            sx = srcA / dstA;
        const float quad[] = {
            -sx, -sy, 0.f, 0.f, sx, -sy, 1.f, 0.f, -sx, sy, 0.f, 1.f,
            sx,  -sy, 1.f, 0.f, sx, sy,  1.f, 1.f, -sx, sy, 0.f, 1.f,
        };
        program_->bind();
        vbo_.bind();
        vbo_.allocate(quad, sizeof(quad));
        const int pos = program_->attributeLocation("aPos");
        const int tex = program_->attributeLocation("aTex");
        program_->enableAttributeArray(pos);
        program_->enableAttributeArray(tex);
        program_->setAttributeBuffer(pos, GL_FLOAT, 0, 2, 4 * sizeof(float));
        program_->setAttributeBuffer(tex, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
        texY_->bind(0);
        texUV_->bind(1);
        program_->setUniformValue("uY", 0);
        program_->setUniformValue("uUV", 1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        program_->disableAttributeArray(pos);
        program_->disableAttributeArray(tex);
        vbo_.release();
        texY_->release(0);
        texUV_->release(1);
        program_->release();

        ++renderCount_;
        if (renderLogClock_.elapsed() >= 5000) {
            const qint64 ms = renderLogClock_.restart();
            qInfo("GST_RENDER fps=%.1f frames=%d uploads=%d upload_ms=%.1f",
                  renderCount_ * 1000.0 / qMax<qint64>(1, ms), renderCount_, uploadCount_,
                  uploadCount_ ? double(uploadMsTotal_) / uploadCount_ : 0.0);
            renderCount_ = 0;
            uploadCount_ = 0;
            uploadMsTotal_ = 0;
        }
    }

private:
    void uploadFrame()
    {
        GstBuffer *buffer = gst_sample_get_buffer(pending_);
        GstCaps *caps = gst_sample_get_caps(pending_);
        GstVideoInfo info;
        if (!buffer || !caps || !gst_video_info_from_caps(&info, caps))
            return;
        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ))
            return;

        const qint64 start = nowNs();
        uploadPlane(&texY_, 0, useRg_ ? GL_RED : GL_LUMINANCE, frame.data[0], frameWidth_,
                    frameHeight_, strideY_, GL_UNSIGNED_BYTE);
        uploadPlane(&texUV_, 1, useRg_ ? GL_RG : GL_LUMINANCE_ALPHA, frame.data[1],
                    frameWidth_ / 2, frameHeight_ / 2, strideUV_, GL_UNSIGNED_BYTE);
        uploadMsTotal_ += int((nowNs() - start) / 1000000);
        ++uploadCount_;
        uploadedSeq_ = frameSeq_;

        gst_video_frame_unmap(&frame);
    }

    void uploadPlane(QOpenGLTexture **texture, int unit, GLenum format, const void *data, int w,
                     int h, int stride, GLenum type)
    {
        if (!*texture || (*texture)->width() != w || (*texture)->height() != h) {
            delete *texture;
            *texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
            QOpenGLTexture::TextureFormat textureFormat = QOpenGLTexture::LuminanceFormat;
            if (format == GL_RG)
                textureFormat = QOpenGLTexture::RG8_UNorm;
            else if (format == GL_RED)
                textureFormat = QOpenGLTexture::R8_UNorm;
            else if (format == GL_LUMINANCE_ALPHA)
                textureFormat = QOpenGLTexture::LuminanceAlphaFormat;
            (*texture)->setFormat(textureFormat);
            (*texture)->setSize(w, h);
            (*texture)->setMipLevels(1);
            (*texture)->allocateStorage();
            (*texture)->setMinificationFilter(QOpenGLTexture::Nearest);
            (*texture)->setMagnificationFilter(QOpenGLTexture::Nearest);
            (*texture)->setWrapMode(QOpenGLTexture::ClampToEdge);
            if (!(*texture)->isCreated())
                qWarning("GstVideoItem: 纹理创建失败 (%dx%d)", w, h);
        }
        (*texture)->bind(unit);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // GL_UNPACK_ROW_LENGTH 单位是“像素”，LA 每像素 2 字节，UV 行宽要除以 2。
        glPixelStorei(GL_UNPACK_ROW_LENGTH,
                      (format == GL_LUMINANCE_ALPHA || format == GL_RG) ? stride / 2 : stride);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    bool initGl()
    {
        initializeOpenGLFunctions();
        if (auto *ctx = QOpenGLContext::currentContext()) {
            useRg_ = ctx->hasExtension("GL_EXT_texture_rg") || ctx->hasExtension("GL_ARB_texture_rg");
            qInfo("GST_GL version=%s rg_texture=%d",
                  reinterpret_cast<const char *>(glGetString(GL_VERSION)), useRg_ ? 1 : 0);
        }
        static const char *vertexShader = R"(
            attribute highp vec4 aPos;
            attribute highp vec2 aTex;
            varying highp vec2 vTex;
            void main() { gl_Position = aPos; vTex = aTex; }
        )";
        // GL 2.1（Pi VC4）没有 R8/RG8：Y 用 Luminance，UV 用 LuminanceAlpha（r=U, a=V）。
        static const char *fragmentShader = R"(
            varying highp vec2 vTex;
            uniform sampler2D uY;
            uniform sampler2D uUV;
            void main() {
                highp float y = texture2D(uY, vTex).r;
                highp vec4 uv4 = texture2D(uUV, vTex);
                highp vec2 uv = vec2(uv4.r, uv4.a) - vec2(0.5);
                highp vec3 rgb = vec3(y + 1.402 * uv.y,
                                      y - 0.344 * uv.x - 0.714 * uv.y,
                                      y + 1.772 * uv.x);
                gl_FragColor = vec4(rgb, 1.0);
            }
        )";
        // RG 纹理：r=U、g=V；LuminanceAlpha：r=U、a=V。两者 shader 只差 UV 取值。
        static const char *fragmentShaderRg = R"(
            varying highp vec2 vTex;
            uniform sampler2D uY;
            uniform sampler2D uUV;
            void main() {
                highp float y = texture2D(uY, vTex).r;
                highp vec2 uv = texture2D(uUV, vTex).rg - vec2(0.5);
                highp vec3 rgb = vec3(y + 1.402 * uv.y,
                                      y - 0.344 * uv.x - 0.714 * uv.y,
                                      y + 1.772 * uv.x);
                gl_FragColor = vec4(rgb, 1.0);
            }
        )";
        program_ = new QOpenGLShaderProgram;
        if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)
            || !program_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                  useRg_ ? fragmentShaderRg : fragmentShader)
            || !program_->link()) {
            qWarning("GstVideoItem: 着色器失败: %s", qPrintable(program_->log()));
            delete program_;
            program_ = nullptr;
            return false;
        }
        vbo_.create();
        vbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        renderLogClock_.start();
        return true;
    }

    GstSample *pending_ = nullptr;
    int frameSeq_ = 0;
    int uploadedSeq_ = -1;
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    int strideY_ = 0;
    int strideUV_ = 0;
    bool useRg_ = false;
    QOpenGLTexture *texY_ = nullptr;
    QOpenGLTexture *texUV_ = nullptr;
    QOpenGLShaderProgram *program_ = nullptr;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QElapsedTimer renderLogClock_;
    int renderCount_ = 0;
    int uploadCount_ = 0;
    int uploadMsTotal_ = 0;
};

} // namespace

// ── 管线线程：创建/销毁都在这里，GUI 线程不再被 200ms+ 的建管线卡住 ──
class GstPlayerWorker : public QObject {
    Q_OBJECT
public:
    explicit GstPlayerWorker(GstVideoItem *owner) : owner_(owner) {}
    ~GstPlayerWorker() override { teardown(); }

public slots:
    void start(const QString &url);
    void stop() { teardown(); }
    void setPaused(bool paused)
    {
        if (pipeline_)
            gst_element_set_state(pipeline_, paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
    }
    void setVolume(double volume)
    {
        if (!pipeline_)
            return;
        if (GstElement *volumeElement = gst_bin_get_by_name(GST_BIN(pipeline_), "vol")) {
            g_object_set(volumeElement, "volume", volume, nullptr);
            gst_object_unref(volumeElement);
        }
    }

signals:
    void started(bool hardware);
    void failed(const QString &message);
    void eos();

private:
    void teardown();
    void pollBus();

    GstVideoItem *owner_ = nullptr;
    GstElement *pipeline_ = nullptr;
    GstElement *sink_ = nullptr;
    QTimer *busTimer_ = nullptr;
};

void GstPlayerWorker::start(const QString &url)
{
    teardown();
    if (url.isEmpty())
        return;

    const bool hardware = QFileInfo::exists(QStringLiteral("/dev/video10"))
        && gstElementAvailable("v4l2h264dec");
    qInfo("GST_BUILD_BEGIN hardware=%d connection_speed=%d", hardware ? 1 : 0,
          qEnvironmentVariableIntValue("TERMINAL_GST_CONNECTION_SPEED"));

    GError *error = nullptr;
    QElapsedTimer clock;
    clock.start();
    pipeline_ = gst_parse_launch(pipelineFor(url, hardware).toUtf8().constData(), &error);
    if (!pipeline_ || error) {
        const QString message = error ? QString::fromUtf8(error->message)
                                      : QStringLiteral("播放管线创建失败");
        if (error)
            g_error_free(error);
        if (pipeline_) {
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
        }
        emit failed(message);
        return;
    }

    sink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (sink_) {
        GstAppSinkCallbacks callbacks{};
        callbacks.new_sample = [](GstAppSink *sink, gpointer user) -> GstFlowReturn {
            auto *self = static_cast<GstVideoItem *>(user);
            GstSample *sample = gst_app_sink_pull_sample(sink);
            if (!sample)
                return GST_FLOW_EOS;
            self->enqueueSample(sample); // 接管引用
            return GST_FLOW_OK;
        };
        gst_app_sink_set_callbacks(GST_APP_SINK(sink_), &callbacks, owner_, nullptr);
    }

    if (!busTimer_) {
        busTimer_ = new QTimer(this);
        busTimer_->setInterval(250);
        QObject::connect(busTimer_, &QTimer::timeout, this, [this] { pollBus(); });
    }
    busTimer_->start();

    const GstStateChangeReturn state = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    qInfo("GST_BUILD_MS %lld state=%d", clock.elapsed(), int(state));
    if (state == GST_STATE_CHANGE_FAILURE) {
        emit failed(QStringLiteral("播放管线无法启动"));
        teardown();
        return;
    }
    emit started(hardware);
}

void GstPlayerWorker::teardown()
{
    QElapsedTimer clock;
    clock.start();
    if (busTimer_)
        busTimer_->stop();
    if (sink_) {
        gst_object_unref(sink_);
        sink_ = nullptr;
    }
    if (pipeline_) {
        qInfo("GST_TEARDOWN");
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        qInfo("GST_TEARDOWN_MS %lld", clock.elapsed());
    }
}

void GstPlayerWorker::pollBus()
{
    if (!pipeline_)
        return;
    GstBus *bus = gst_element_get_bus(pipeline_);
    if (!bus)
        return;
    while (GstMessage *message = gst_bus_pop_filtered(
               bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
            emit eos();
        } else {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            const QString text = error ? QString::fromUtf8(error->message)
                                       : QStringLiteral("播放错误");
            if (error)
                g_error_free(error);
            g_free(debug);
            emit failed(text);
        }
        gst_message_unref(message);
    }
    gst_object_unref(bus);
}

GstVideoItem::GstVideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    setFlag(ItemHasContents, true);
    if (!gstReady) {
        gst_init(nullptr, nullptr);
        gstReady = true;
    }

    worker_ = new GstPlayerWorker(this);
    worker_->moveToThread(&workerThread_);
    connect(worker_, &GstPlayerWorker::started, this, &GstVideoItem::onWorkerStarted);
    connect(worker_, &GstPlayerWorker::failed, this, &GstVideoItem::onWorkerFailed);
    connect(worker_, &GstPlayerWorker::eos, this, &GstVideoItem::onWorkerEos);
    workerThread_.setObjectName(QStringLiteral("gst-player"));
    workerThread_.start();

    startTimer_ = new QTimer(this);
    startTimer_->setSingleShot(true);
    startTimer_->setInterval(20000);
    connect(startTimer_, &QTimer::timeout, this, &GstVideoItem::onConnectTimeout);

    statsTimer_ = new QTimer(this);
    statsTimer_->setInterval(5000);
    connect(statsTimer_, &QTimer::timeout, this, &GstVideoItem::onStatsTick);
    statsTimer_->start();

    renderTick_ = new QTimer(this);
    renderTick_->setInterval(8);
    connect(renderTick_, &QTimer::timeout, this, [this] {
        if (playing_ && hasDueFrame())
            update();
    });
}

GstVideoItem::~GstVideoItem()
{
    if (startTimer_)
        startTimer_->stop();
    if (statsTimer_)
        statsTimer_->stop();
    if (renderTick_)
        renderTick_->stop();
    if (worker_ && workerThread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, "stop", Qt::BlockingQueuedConnection);
        workerThread_.quit();
        workerThread_.wait(5000);
    }
    delete worker_;
    worker_ = nullptr;
    clearFrames();
}

void GstVideoItem::setSource(const QString &source)
{
    if (source_ == source)
        return;
    source_ = source;
    emit sourceChanged();
    release();
}

void GstVideoItem::setError(const QString &text)
{
    if (errorText_ == text)
        return;
    errorText_ = text;
    emit errorTextChanged();
}

void GstVideoItem::clearFrames()
{
    QMutexLocker locker(&frameMutex_);
    for (Slot &slot : slots_) {
        if (slot.sample) {
            gst_sample_unref(slot.sample);
            slot.sample = nullptr;
        }
    }
    frameSpace_.wakeAll();
    anchorPts_ = -1;
    anchorWall_ = 0;
    width_ = height_ = 0;
}

void GstVideoItem::enqueueSample(GstSample *sample)
{
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    GstVideoInfo info;
    if (!buffer || !caps || !gst_video_info_from_caps(&info, caps)
        || GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_NV12) {
        if (!loggedFirstFrame_) {
            loggedFirstFrame_ = true;
            qWarning("GST_UNSUPPORTED_FORMAT %s", caps ? gst_caps_to_string(caps) : "?");
        }
        gst_sample_unref(sample);
        return;
    }

    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    if (!GST_CLOCK_TIME_IS_VALID(pts)) {
        gst_sample_unref(sample);
        return;
    }

    const int w = GST_VIDEO_INFO_WIDTH(&info);
    const int h = GST_VIDEO_INFO_HEIGHT(&info);
    const qint64 now = nowNs();
    const qint64 qpts = qint64(pts);

    {
        QMutexLocker locker(&frameMutex_);
        if (!loggedFirstFrame_) {
            loggedFirstFrame_ = true;
            qInfo("GST_FIRST_FRAME %dx%d NV12 strideY=%d strideUV=%d", w, h, info.stride[0],
                  info.stride[1]);
        }
        width_ = w;
        height_ = h;
        strideY_ = info.stride[0];
        strideUV_ = info.stride[1];
        if (videoSize_ != QSize(w, h)) {
            const QSize size(w, h);
            QMetaObject::invokeMethod(this, [this, size] {
                if (videoSize_ == size)
                    return;
                videoSize_ = size;
                emit videoSizeChanged();
            }, Qt::QueuedConnection);
        }

        if (anchorPts_ < 0 || qpts + 500000000LL < anchorPts_) {
            anchorPts_ = qpts;
            anchorWall_ = now;
        }

        // Kodi VideoPlayerVideo：迟到 > 500ms 直接跳；直播换段/切源 pts 前跳同样要重锚定，
        // 否则队列里全是“未来帧”，渲染端一帧都等不到。
        const qint64 lateMs = ((now - anchorWall_) - (qpts - anchorPts_)) / 1000000;
        if (lateMs > 500 || lateMs < -1000) {
            int dropped = 0;
            for (Slot &slot : slots_) {
                if (slot.sample) {
                    gst_sample_unref(slot.sample);
                    slot.sample = nullptr;
                    ++dropped;
                }
            }
            if (dropped)
                droppedSkip_.fetch_add(dropped, std::memory_order_relaxed);
            if (lateMs > 500)
                droppedLate_.fetch_add(1, std::memory_order_relaxed);
            anchorPts_ = qpts;
            anchorWall_ = now;
        }

        // 队列满 → 阻塞解码线程（回压），最多等 400ms。绝不能丢最旧的帧：
        // 那些正是马上要显示的，丢了渲染端就会长时间拿不到“到期帧”。
        int target = -1;
        for (int i = 0; i < kSlots && target < 0; ++i)
            if (!slots_[i].sample)
                target = i;
        if (target < 0) {
            QElapsedTimer wait;
            wait.start();
            while (target < 0 && wait.elapsed() < 400) {
                frameSpace_.wait(&frameMutex_, 50);
                for (int i = 0; i < kSlots && target < 0; ++i)
                    if (!slots_[i].sample)
                        target = i;
            }
            if (target < 0) {
                gst_sample_unref(sample);
                droppedSkip_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        slots_[target].sample = sample;
        slots_[target].pts = qpts;
        slots_[target].seq = ++slotSeq_;
    }

    sampleCount_.fetch_add(1, std::memory_order_relaxed);
}

GstSample *GstVideoItem::acquireFrame(qint64 now, int *seq, int *width, int *height,
                                      int *strideY, int *strideUV)
{
    QMutexLocker locker(&frameMutex_);
    if (anchorPts_ < 0)
        return nullptr;
    const qint64 due = anchorPts_ + (now - anchorWall_);

    int best = -1;
    for (int i = 0; i < kSlots; ++i) {
        if (!slots_[i].sample)
            continue;
        if (slots_[i].pts > due + 15000000LL) // 还没到期，再等一帧
            continue;
        if (best < 0 || slots_[i].pts > slots_[best].pts)
            best = i;
    }
    if (best < 0)
        return nullptr;

    // Kodi RenderManager：显示最新到期帧，中间帧直接进 discard。
    int skipped = 0;
    for (int i = 0; i < kSlots; ++i) {
        if (i != best && slots_[i].sample && slots_[i].pts < slots_[best].pts) {
            gst_sample_unref(slots_[i].sample);
            slots_[i].sample = nullptr;
            ++skipped;
        }
    }
    if (skipped)
        droppedSkip_.fetch_add(skipped, std::memory_order_relaxed);

    GstSample *sample = slots_[best].sample;
    slots_[best].sample = nullptr;
    frameSpace_.wakeAll();
    if (seq)
        *seq = slots_[best].seq;
    if (width)
        *width = width_;
    if (height)
        *height = height_;
    if (strideY)
        *strideY = strideY_;
    if (strideUV)
        *strideUV = strideUV_;
    return sample;
}

void GstVideoItem::releaseFrame(GstSample *sample)
{
    if (sample)
        gst_sample_unref(sample);
}

bool GstVideoItem::hasDueFrame()
{
    QMutexLocker locker(&frameMutex_);
    if (anchorPts_ < 0)
        return false;
    const qint64 due = anchorPts_ + (nowNs() - anchorWall_);
    for (const Slot &slot : slots_) {
        if (slot.sample && slot.pts <= due + 15000000LL)
            return true;
    }
    return false;
}

void GstVideoItem::setVolume(qreal volume)
{
    volume_ = qBound<qreal>(0.0, volume, 1.0);
    if (worker_)
        QMetaObject::invokeMethod(worker_, "setVolume", Qt::QueuedConnection, Q_ARG(double, volume_));
}

void GstVideoItem::play()
{
    if (source_.isEmpty() || !worker_)
        return;
    pausedAtMs_ = 0;
    starting_ = true;
    playing_ = false;
    QMetaObject::invokeMethod(worker_, "start", Qt::QueuedConnection, Q_ARG(QString, source_));
    startTimer_->start();
    qInfo("GST_PLAY_REQUEST %s", qPrintable(source_));
}

void GstVideoItem::resume()
{
    const qint64 pausedFor = pausedAtMs_ > 0 ? (QDateTime::currentMSecsSinceEpoch() - pausedAtMs_) : 0;
    if (pipelineAlive_ && pausedFor > 0 && pausedFor < 3000) {
        clearFrames();
        pausedAtMs_ = 0;
        if (worker_)
            QMetaObject::invokeMethod(worker_, "setPaused", Qt::QueuedConnection, Q_ARG(bool, false));
        if (!playing_) {
            playing_ = true;
            renderTick_->start();
            if (worker_)
                QMetaObject::invokeMethod(worker_, "setVolume", Qt::QueuedConnection, Q_ARG(double, volume_));
            emit playingChanged();
        }
        return;
    }
    play();
}

void GstVideoItem::pause()
{
    pausedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    if (worker_)
        QMetaObject::invokeMethod(worker_, "setPaused", Qt::QueuedConnection, Q_ARG(bool, true));
    startTimer_->stop();
    starting_ = false;
    renderTick_->stop();
    playing_ = false;
    emit playingChanged();
}

void GstVideoItem::stop()
{
    pipelineAlive_ = false;
    if (worker_)
        QMetaObject::invokeMethod(worker_, "stop", Qt::QueuedConnection);
    startTimer_->stop();
    starting_ = false;
    renderTick_->stop();
    clearFrames();
    playing_ = false;
    emit playingChanged();
}

void GstVideoItem::release()
{
    pipelineAlive_ = false;
    pausedAtMs_ = 0;
    if (worker_)
        QMetaObject::invokeMethod(worker_, "stop", Qt::QueuedConnection);
    startTimer_->stop();
    starting_ = false;
    renderTick_->stop();
    clearFrames();
    loggedFirstFrame_ = false;
    if (!videoSize_.isEmpty()) {
        videoSize_ = QSize();
        emit videoSizeChanged();
    }
    playing_ = false;
    emit playingChanged();
}

void GstVideoItem::onWorkerStarted(bool hardware)
{
    startTimer_->stop();
    starting_ = false;
    pipelineAlive_ = true;
    if (hardwareDecode_ != hardware) {
        hardwareDecode_ = hardware;
        emit hardwareDecodeChanged();
    }
    playing_ = true;
    renderTick_->start();
    if (worker_)
        QMetaObject::invokeMethod(worker_, "setVolume", Qt::QueuedConnection, Q_ARG(double, volume_));
    emit playingChanged();
}

void GstVideoItem::onWorkerFailed(const QString &message)
{
    startTimer_->stop();
    starting_ = false;
    pipelineAlive_ = false;
    renderTick_->stop();
    clearFrames();
    playing_ = false;
    emit playingChanged();
    setError(message);
}

void GstVideoItem::onWorkerEos()
{
    renderTick_->stop();
    playing_ = false;
    emit playingChanged();
}

void GstVideoItem::onConnectTimeout()
{
    if (!starting_)
        return;
    starting_ = false;
    setError(QStringLiteral("连接超时"));
    if (worker_)
        QMetaObject::invokeMethod(worker_, "stop", Qt::QueuedConnection);
}

void GstVideoItem::onStatsTick()
{
    const int samples = sampleCount_.load(std::memory_order_relaxed);
    const int late = droppedLate_.load(std::memory_order_relaxed);
    const int skipped = droppedSkip_.load(std::memory_order_relaxed);
    qInfo("GST_STAT samples=%d drop_late=%d drop_skip=%d", samples - lastSampleCount_,
          late - lastDroppedLate_, skipped - lastDroppedSkip_);
    lastSampleCount_ = samples;
    lastDroppedLate_ = late;
    lastDroppedSkip_ = skipped;
}

QQuickFramebufferObject::Renderer *GstVideoItem::createRenderer() const
{
    return new GstRenderer;
}

#include "GstVideoItem.moc"
