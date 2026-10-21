#pragma once
#include <QQuickFramebufferObject>
#include <QPointer>
#include <QVariantMap>
class BlurItem : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(float strength READ strength WRITE setStrength NOTIFY strengthChanged)
    Q_PROPERTY(float mixAmount READ mixAmount WRITE setMixAmount NOTIFY strengthChanged)
    Q_PROPERTY(int levels READ levels WRITE setLevels NOTIFY strengthChanged)
    Q_PROPERTY(float cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY strengthChanged)
    Q_PROPERTY(float resolutionScale READ resolutionScale WRITE setResolutionScale NOTIFY strengthChanged)
    Q_PROPERTY(int renderRevision READ renderRevision WRITE setRenderRevision NOTIFY strengthChanged)
    Q_PROPERTY(bool renderOnly READ renderOnly WRITE setRenderOnly NOTIFY strengthChanged)
    Q_PROPERTY(int sampledAlpha READ sampledAlpha NOTIFY sampledAlphaChanged)
public:
    explicit BlurItem(QQuickItem *parent=nullptr):QQuickFramebufferObject(parent) { setTextureFollowsItemSize(false); }
    Renderer *createRenderer() const override;
    Q_INVOKABLE QVariantMap renderingStats() const;
    int renderRevision() const { return renderRevision_; }
    void setRenderRevision(int value) { if(renderRevision_==value)return;renderRevision_=value;emit strengthChanged();update(); }
    bool renderOnly() const { return renderOnly_; }
    void setRenderOnly(bool value) { if(renderOnly_==value)return;renderOnly_=value;emit strengthChanged();update(); }
    int sampledAlpha() const { return sampledAlpha_; }
    void recordFrame(int revision,int alpha) { if(sampledAlpha_!=alpha){sampledAlpha_=alpha;emit sampledAlphaChanged();}emit frameReady(revision); }
    QQuickItem *source() const { return source_; }
    void setSource(QQuickItem *item) {
        if (source_ == item) return;
        if (source_) disconnect(source_, nullptr, this, nullptr);
        source_ = item;
        if (item) connect(item, &QObject::destroyed, this, [this] { source_ = nullptr; emit sourceChanged(); update(); });
        emit sourceChanged(); update();
    }
    float strength() const { return strength_; }
    void setStrength(float value) { strength_ = qBound(0.1f, value, 4.0f); emit strengthChanged(); update(); }
    float mixAmount() const { return mix_; }
    void setMixAmount(float value) { mix_ = qBound(0.0f,value,1.0f); emit strengthChanged(); update(); }
    int levels() const { return levels_; }
    float resolutionScale() const { return resolutionScale_; }
    void setResolutionScale(float value) {
        value = qBound(0.25f,value,1.0f);
        if (resolutionScale_ == value) return;
        resolutionScale_ = value; emit strengthChanged(); update();
    }
    float cornerRadius() const { return radius_; }
    void setCornerRadius(float value) { radius_=qMax(0.0f,value);emit strengthChanged();update(); }
    void setLevels(int value) { levels_ = qBound(1,value,4); emit strengthChanged(); update(); }
signals:
    void sourceChanged();
    void strengthChanged();
    void frameReady(int revision);
    void sampledAlphaChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *node,UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &geometry,const QRectF &previous) override;
private:
    QPointer<QQuickItem> source_;
    float strength_ = 1.0f;
    float mix_ = 1.0f;
    int levels_ = 4;
    float radius_ = 0;
    float resolutionScale_ = 1;
    int renderRevision_ = 0;
    bool renderOnly_ = false;
    int sampledAlpha_ = -1;
};
