#include "BlurItem.h"
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QSGTextureProvider>
#include <QSGTexture>
#include <QtQuick/qsgtexture_platform.h>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <QDebug>
#include <array>
#include <memory>
#include <atomic>
#include <QOpenGLContext>
#include <unordered_map>
#include <QSGSimpleTextureNode>

namespace {
std::atomic<quint64> renderCount{0}, pyramidCount{0}, allocationCount{0};
thread_local std::unordered_map<QOpenGLContext *,std::weak_ptr<QOpenGLShaderProgram>> programs;
}

QVariantMap BlurItem::renderingStats() const {
    return {{"renders",qulonglong(renderCount.load())},{"pyramids",qulonglong(pyramidCount.load())},
            {"allocations",qulonglong(allocationCount.load())}};
}

QSGNode *BlurItem::updatePaintNode(QSGNode *node,UpdatePaintNodeData *data) {
    node=QQuickFramebufferObject::updatePaintNode(node,data);
    // Keep the native texture provider/render callback alive without drawing
    // an otherwise occluded fullscreen cache quad into every desktop frame.
    if(renderOnly_)if(auto *texture=dynamic_cast<QSGSimpleTextureNode *>(node))texture->setRect(QRectF{});
    return node;
}

void BlurItem::geometryChange(const QRectF &geometry,const QRectF &previous) {
    QQuickFramebufferObject::geometryChange(geometry,previous);
    if(geometry.size()!=previous.size())update();
}

class BlurRenderer : public QObject, public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions {
    QPointer<QSGTextureProvider> provider_;
    std::shared_ptr<QOpenGLShaderProgram> program_;
    QOpenGLBuffer vertices_{QOpenGLBuffer::VertexBuffer};
    std::array<std::unique_ptr<QOpenGLFramebufferObject>, 4> levels_;
    std::array<std::unique_ptr<QOpenGLFramebufferObject>, 4> upsample_;
    bool initialized_ = false;
    bool hasContent_ = false;
    float strength_ = 1;
    float mix_ = 1;
    int levelCount_ = 4;
    float radius_ = 0;
    float resolutionScale_ = 1;
    int reportedLevels_ = 0;
    QSize requestedSize_;
    bool reportReady_ = false;
    const bool probeAlpha_=qEnvironmentVariableIntValue("PI_REPAIR_PROBE_CACHE")==1;
    QString name_;
    QPointer<BlurItem> item_;
    int revision_ = 0, reportedRevision_ = -1;
    int completedRevision_ = -1;
    void draw(GLuint texture, QSize size, QVector2D step, GLuint original = 0, float blend = 1, float radius = 0) {
        glViewport(0, 0, size.width(), size.height());
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, original); glActiveTexture(GL_TEXTURE0);
        program_->bind(); program_->setUniformValue("image", 0); program_->setUniformValue("stepSize", step);
        program_->setUniformValue("originalImage",1); program_->setUniformValue("blendRatio",blend);
        program_->setUniformValue("outputSize",QVector2D(size.width(),size.height()));program_->setUniformValue("cornerRadius",radius);
        // Qt's previous draw may leave an ARRAY_BUFFER bound. Client pointers
        // then become offsets into that buffer, producing invalid draw calls.
        vertices_.bind();
        program_->enableAttributeArray(0); program_->setAttributeBuffer(0, GL_FLOAT, 0, 2);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); program_->disableAttributeArray(0);
        vertices_.release(); program_->release();
    }
public:
    void synchronize(QQuickFramebufferObject *item) override {
        auto *blur = static_cast<BlurItem *>(item);
        item_=blur;revision_=blur->renderRevision();
        reportReady_=blur->renderOnly();name_=blur->objectName();
        const qreal dpr=blur->window()?blur->window()->effectiveDevicePixelRatio():1;
        // Qt compares the FBO with the *unscaled* item size when follows-size
        // is enabled. We own invalidation because our FBO is intentionally smaller.
        const QSize requested(qRound(blur->width()*dpr),qRound(blur->height()*dpr));
        if(requestedSize_!=requested){requestedSize_=requested;invalidateFramebufferObject();}
        if (resolutionScale_ != blur->resolutionScale()) {
            resolutionScale_ = blur->resolutionScale(); invalidateFramebufferObject();
        }
        strength_ = blur->strength();
        mix_ = blur->mixAmount(); levelCount_ = blur->levels();
        radius_=blur->cornerRadius()*(blur->window()?blur->window()->effectiveDevicePixelRatio():1)*resolutionScale_;
        auto *provider = blur->source() && blur->source()->isTextureProvider() ? blur->source()->textureProvider() : nullptr;
        if (provider_ != provider) {
            if (provider_) disconnect(provider_, nullptr, this, nullptr);
            provider_ = provider;
            if (provider_) connect(provider_, &QSGTextureProvider::textureChanged, this, [this] { update(); }, Qt::DirectConnection);
        }
    }
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        ++allocationCount;
        hasContent_=false;
        return new QOpenGLFramebufferObject(QSize(qMax(1,qRound(size.width()*resolutionScale_)),
            qMax(1,qRound(size.height()*resolutionScale_))), QOpenGLFramebufferObject::NoAttachment);
    }
    void render() override {
        ++renderCount;
        // Adding/removing ShaderEffect consumers dirties the scene-graph node,
        // but must not re-blur a frozen atlas whose capture revision is unchanged.
        if(reportReady_ && hasContent_ && completedRevision_==revision_) {
            QQuickOpenGLUtils::resetOpenGLState();return;
        }
        if (!initialized_) {
            initializeOpenGLFunctions();
            if (!vertices_.create() || !vertices_.bind()) { QQuickOpenGLUtils::resetOpenGLState(); return; }
            static const GLfloat points[] = {-1,-1, 1,-1, -1,1, 1,1};
            vertices_.allocate(points, sizeof(points)); vertices_.release();
            program_=programs[QOpenGLContext::currentContext()].lock();
            if(!program_) {
            program_=std::make_shared<QOpenGLShaderProgram>();
            program_->addShaderFromSourceCode(QOpenGLShader::Vertex,
                "attribute vec2 vertex; varying vec2 uv; void main(){ uv=(vertex+1.0)*0.5; gl_Position=vec4(vertex,0.,1.); }");
            program_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
                "uniform sampler2D image; uniform sampler2D originalImage; uniform float blendRatio; uniform vec2 stepSize; varying vec2 uv;"
                "uniform vec2 outputSize; uniform float cornerRadius;"
                "void main(){vec4 c=texture2D(image,uv);"
                "if(stepSize.x!=0.0 || stepSize.y!=0.0){c*=0.25;"
                "c+=(texture2D(image,uv+vec2(stepSize.x,0.))+texture2D(image,uv-vec2(stepSize.x,0.))"
                "+texture2D(image,uv+vec2(0.,stepSize.y))+texture2D(image,uv-vec2(0.,stepSize.y)))*0.125;"
                "c+=(texture2D(image,uv+stepSize)+texture2D(image,uv-stepSize)"
                "+texture2D(image,uv+vec2(stepSize.x,-stepSize.y))+texture2D(image,uv+vec2(-stepSize.x,stepSize.y)))*0.0625;}"
                "if(blendRatio<1.0)c=mix(texture2D(originalImage,uv),c,blendRatio);"
                "if(cornerRadius>0.0){float r=min(cornerRadius,min(outputSize.x,outputSize.y)*0.5);"
                "vec2 q=abs((uv-0.5)*outputSize)-(outputSize*0.5-vec2(r));"
                "float d=length(max(q,vec2(0.0)))+min(max(q.x,q.y),0.0)-r;c*=1.0-smoothstep(-1.0,1.0,d);}gl_FragColor=c; }");
            program_->bindAttributeLocation("vertex", 0);program_->link();
            programs[QOpenGLContext::currentContext()]=program_;
            }
            initialized_=program_->isLinked();
        }
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        if(!framebufferObject()->isValid()){QQuickOpenGLUtils::resetOpenGLState();return;}
        if(!hasContent_){glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);}
        if (!initialized_ || !provider_ || !provider_->texture()) { QQuickOpenGLUtils::resetOpenGLState(); return; }
        if (auto *dynamic = dynamic_cast<QSGDynamicTexture *>(provider_->texture())) dynamic->updateTexture();
        auto *native = provider_->texture()->nativeInterface<QNativeInterface::QSGOpenGLTexture>();
        if (!native) { QQuickOpenGLUtils::resetOpenGLState(); return; }
        // updateTexture() can render a layer and change the current target/state.
        framebufferObject()->bind();
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
        // Retain the last valid frame while a replacement source is pending.
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        GLuint texture = native->nativeTexture(); QSize size = framebufferObject()->size();
        if(reportedLevels_!=levelCount_) {
            reportedLevels_=levelCount_;
            qInfo()<<"FBO_BLUR_RENDER"<<name_<<"levels="<<levelCount_<<"size="<<size<<"shader="<<initialized_;
        }
        QSize inputSize = provider_->texture()->textureSize();
        ++pyramidCount;
        for (int i = 0; i < levelCount_; ++i) {
            size = QSize(qMax(1, size.width()/2), qMax(1,size.height()/2));
            if (!levels_[i] || levels_[i]->size() != size) { levels_[i] = std::make_unique<QOpenGLFramebufferObject>(size); ++allocationCount; }
            levels_[i]->bind(); draw(texture, size, QVector2D(strength_/qMax(1,inputSize.width()),strength_/qMax(1,inputSize.height())));
            texture = levels_[i]->texture(); inputSize = size;
        }
        for (int i = levelCount_-2; i >= 0; --i) {
            // Separate target: never sample the framebuffer being written.
            // Blend the coarse result with the retained finer mip level.
            const auto targetSize=levels_[i]->size();
            if(!upsample_[i] || upsample_[i]->size()!=targetSize){upsample_[i]=std::make_unique<QOpenGLFramebufferObject>(targetSize);++allocationCount;}
            upsample_[i]->bind();draw(texture,targetSize,QVector2D(strength_/size.width(),strength_/size.height()),levels_[i]->texture(),0.75f);
            texture=upsample_[i]->texture();size=targetSize;
        }
        framebufferObject()->bind(); draw(texture, framebufferObject()->size(), QVector2D(0,0),native->nativeTexture(),mix_,radius_);
        hasContent_=true;
        completedRevision_=revision_;
        if(reportReady_ && reportedRevision_!=revision_) {
            reportedRevision_=revision_;
            const auto item=item_;const int revision=revision_;
            int alpha=-1;
            if(probeAlpha_) {
                alpha=255;GLubyte pixel[4]={0,0,0,0};const auto output=framebufferObject()->size();
                for(int y:{1,3})for(int x:{1,3}) {
                    glReadPixels(output.width()*x/4,output.height()*y/4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                    alpha=qMin(alpha,int(pixel[3]));
                }
            }
            if(item)QMetaObject::invokeMethod(item,[item,revision,alpha]{if(item)item->recordFrame(revision,alpha);},Qt::QueuedConnection);
        }
        QQuickOpenGLUtils::resetOpenGLState();
    }
};
QQuickFramebufferObject::Renderer *BlurItem::createRenderer() const { return new BlurRenderer; }
