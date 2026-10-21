#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QUrl>
#include <QVariantMap>
#include <QPainter>

// A fixed transparent canvas. Every update repaints the complete canvas, first
// clearing it, then compositing the selected atlas frame at its action offset.
class PetSpriteItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QUrl folder READ folder WRITE setFolder NOTIFY changed)
    Q_PROPERTY(QString action READ action WRITE setAction NOTIFY frameChanged)
    Q_PROPERTY(int currentFrame READ currentFrame WRITE setCurrentFrame NOTIFY frameChanged)
    Q_PROPERTY(bool mirrored READ mirrored WRITE setMirrored NOTIFY frameChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(double frameFootY READ frameFootY NOTIFY frameChanged)
public:
    explicit PetSpriteItem(QQuickItem *parent = nullptr);
    QUrl folder() const { return folder_; }
    void setFolder(const QUrl &url);
    QString action() const { return action_; }
    void setAction(const QString &value);
    int currentFrame() const { return frame_; }
    void setCurrentFrame(int value);
    bool mirrored() const { return mirrored_; }
    void setMirrored(bool value);
    bool ready() const { return !images_[0].isNull(); }
    QString error() const { return error_; }
    QVariantMap info() const { return info_; }
    double frameFootY() const;
    bool contains(const QPointF &point) const override;
    void paint(QPainter *painter) override;
signals:
    void changed();
    void frameChanged();
private:
    int actionIndex() const;
    QRect frameRect() const;
    QUrl folder_;
    QString action_ = "idle", error_;
    QVariantMap info_;
    QImage images_[5];
    int counts_[5] = {1, 1, 1, 1, 1};
    int actionOffsetsY_[5] = {};
    double groundAnchorY_ = 184;
    int frame_ = 0, fw_ = 144, fh_ = 192;
    bool mirrored_ = false;
};
