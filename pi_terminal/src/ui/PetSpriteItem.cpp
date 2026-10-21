#include "PetSpriteItem.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <QImageReader>
#include <QPainter>
#include <QSGRendererInterface>
#include <QQuickWindow>
#include <cmath>

namespace {
const QStringList actions = {"idle", "walk", "interact", "sit", "sleep"};
}
PetSpriteItem::PetSpriteItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(false);
    setRenderTarget(QQuickPaintedItem::Image);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        const bool openGl = window && window->rendererInterface()
            && window->rendererInterface()->graphicsApi() == QSGRendererInterface::OpenGL;
        setRenderTarget(openGl ? QQuickPaintedItem::FramebufferObject : QQuickPaintedItem::Image);
    });
}
void PetSpriteItem::setFolder(const QUrl &url) {
    if (url == folder_) return;
    const QString path = url.scheme() == "qrc" ? ":" + url.path() : url.toLocalFile();
    auto fail = [this](const QString &message) { error_ = message; emit changed(); };
    if (path.isEmpty()) { fail("无效角色目录"); return; }
    QFile file(path + "/animations.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) { fail("无法读取角色动画清单"); return; }
    auto json = QJsonDocument::fromJson(file.readAll()).object();
    const int fw = json.value("frameWidth").toInt(), fh = json.value("frameHeight").toInt();
    const double foot = json.value("footY").toDouble(-1);
    if (fw < 16 || fh < 16 || fw > 512 || fh > 512 || !std::isfinite(foot) || foot < 0 || foot > fh) {
        fail("角色帧尺寸/脚底位置无效"); return;
    }
    QImage incoming[5]; int counts[5]; double poseFeet[5] = {}, poseContacts[5] = {};
    int poseTop[5] = {fh, fh, fh, fh, fh};
    int poseBottom[5] = {};
    qint64 totalBytes = 0;
    for (int i = 0; i < 5; ++i) {
        if (i >= 3 && !json.value("actions").toObject().contains(actions[i])) {
            incoming[i] = incoming[0]; counts[i] = counts[0]; poseFeet[i] = poseFeet[0];
            poseContacts[i] = poseContacts[0];
            poseTop[i] = poseTop[0]; poseBottom[i] = poseBottom[0];
            auto entries = json.value("actions").toObject();
            auto fallback = entries.value("idle").toObject(); fallback.insert("supported", false);
            entries.insert(actions[i],fallback); json.insert("actions",entries); continue;
        }
        const auto anim = json.value("actions").toObject().value(actions[i]).toObject();
        counts[i] = anim.value("frames").toInt();
        const double fps = anim.value("fps").toDouble();
        const double poseFoot = anim.value("footY").toDouble(foot);
        if (!std::isfinite(poseFoot) || poseFoot<0 || poseFoot>fh) { fail("动作接地位置无效"); return; }
        QImageReader reader(path + '/' + actions[i] + ".png");
        const QSize size = reader.size();
        totalBytes += qint64(size.width()) * size.height() * 4;
        const int budget=qBound(64,json.value("textureBudgetMiB").toInt(64),256);
        if (counts[i] < 1 || counts[i] > 240 || !std::isfinite(fps) || fps <= 0 || fps > 60 || size.width() < fw || size.height() < fh ||
            size.width() > 2048 || size.height() > 8192 || size.width() % fw || size.height() % fh ||
            totalBytes > qint64(budget) * 1024 * 1024 || (size.width()/fw)*(size.height()/fh) < counts[i]) {
            fail("动画图集超限或帧数据无效"); return;
        }
        incoming[i] = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (incoming[i].isNull()) { fail("角色图集解码失败"); return; }
        QVariantList contacts, supportContacts;QList<int> heights;
        for (int frame = 0; frame < counts[i]; ++frame) {
            const int cols = size.width()/fw;
            bool visible = false;
            for (int y = (frame/cols)*fh; y < (frame/cols+1)*fh && !visible; ++y)
                for (int x = (frame%cols)*fw; x < (frame%cols+1)*fw; ++x)
                    if (qAlpha(incoming[i].pixel(x, y)) > 32) { visible = true; break; }
            if (!visible) { fail("图集中存在空白动画帧，拒绝切换"); return; }
            int bottom=0, boundsBottom=0;
            for(int y=(frame/cols+1)*fh-1;y>=(frame/cols)*fh && !bottom;--y)
                for(int x=(frame%cols)*fw;x<(frame%cols+1)*fw;++x)
                    if(qAlpha(incoming[i].pixel(x,y))>32){bottom=y-(frame/cols)*fh+1;break;}
            for(int y=(frame/cols+1)*fh-1;y>=(frame/cols)*fh && !boundsBottom;--y)
                for(int x=(frame%cols)*fw;x<(frame%cols+1)*fw;++x)
                    if(qAlpha(incoming[i].pixel(x,y))>0){boundsBottom=y-(frame/cols)*fh+1;break;}
            int top=fh, boundsTop=fh;
            for(int y=(frame/cols)*fh;y<(frame/cols+1)*fh && top==fh;++y)
                for(int x=(frame%cols)*fw;x<(frame%cols+1)*fw;++x)
                    if(qAlpha(incoming[i].pixel(x,y))>32){top=y-(frame/cols)*fh;break;}
            for(int y=(frame/cols)*fh;y<(frame/cols+1)*fh && boundsTop==fh;++y)
                for(int x=(frame%cols)*fw;x<(frame%cols+1)*fw;++x)
                    if(qAlpha(incoming[i].pixel(x,y))>0){boundsTop=y-(frame/cols)*fh;break;}
            contacts.append(bottom);
            int support=bottom;
            if(i==3 || i==4) {
                QVector<int> rowWidths(fh,0);
                int left=fw,right=0;
                for(int y=top;y<bottom;++y)for(int x=0;x<fw;++x)
                    if(qAlpha(incoming[i].pixel((frame%cols)*fw+x,(frame/cols)*fh+y))>32) {
                        ++rowWidths[y];left=qMin(left,x);right=qMax(right,x+1);
                    }
                if(i==3) {
                    // Match Ark-Pets exactly: the Sit animation's 50-unit
                    // AnimType offset moves the skeleton upward while the
                    // window's adjusted origin keeps the seat at the ground.
                    // Convert display units to source atlas pixels using the
                    // same idle-height normalization as PetSpriteItem.
                    const double reference=json.value("referenceHeight").toDouble(fh*0.8);
                    const double target=json.value("targetHeight").toDouble(176);
                    const int seatOffset=qRound(50*reference/target);
                    support=qBound(top+1,bottom-seatOffset,bottom);
                } else {
                    // A lone hair/tail pixel is not the lying body's support.
                    // Require a broad body contact, retaining thin fringe below
                    // the taskbar plane where it is naturally occluded.
                    const int minWidth=qMax(3,qRound((right-left)*0.15));
                    for(int y=bottom-1;y>=top;--y)if(rowWidths[y]>=minWidth){support=y+1;break;}
                }
            }
            supportContacts.append(support);
            poseTop[i] = qMin(poseTop[i], boundsTop);
            poseBottom[i] = qMax(poseBottom[i], boundsBottom);
            if(i==0) {
                heights.append(bottom-top);
            }
        }
        // Derive from decoded pixels for legacy caches too, so old characters
        // immediately receive correct frame-by-frame grounding without download.
        auto entries=json.value("actions").toObject();auto entry=entries.value(actions[i]).toObject();
        entry.insert("frameFeet",QJsonArray::fromVariantList(contacts));entries.insert(actions[i],entry);json.insert("actions",entries);
        entry.insert("frameContacts",QJsonArray::fromVariantList(supportContacts));entries.insert(actions[i],entry);json.insert("actions",entries);
        QList<double> sortedContacts;
        sortedContacts.reserve(contacts.size());
        for (const auto &contact : contacts) sortedContacts.append(contact.toDouble());
        std::sort(sortedContacts.begin(), sortedContacts.end());
        poseFeet[i] = sortedContacts.isEmpty() ? poseFoot : sortedContacts[sortedContacts.size()/2];
        QList<int> sortedSupport;
        for(const auto &contact:supportContacts)sortedSupport.append(contact.toInt());
        std::sort(sortedSupport.begin(),sortedSupport.end());
        poseContacts[i]=sortedSupport[sortedSupport.size()/2];
        if(i==0 && !heights.isEmpty()) {
            std::sort(heights.begin(),heights.end());
            json.insert("referenceHeight",heights[heights.size()/2]);json.insert("targetHeight",176);
        }
    }
    // Follow Ark-Pets' fixed-canvas model and action-type vertical offsets.
    // Sit's seat line is the
    // offset-adjusted contact; sleep uses its broad body support (not a single
    // hair/tail pixel) so the character lies on the taskbar.
    const int sharedFoot = qRound(poseFeet[0]);
    // Ark-Pets AnimClip.AnimType.offsetY reference values.
    const int poseOffsets[5] = {5, 5, 5, 50, 25};
    int actionContacts[5] = {}, actionOffsets[5] = {};
    int minVisibleTop = fh, maxVisibleBottom = 0;
    for (int i = 0; i < 5; ++i) {
        actionContacts[i] = qRound(poseContacts[i]);
        actionOffsets[i] = sharedFoot - actionContacts[i];
        minVisibleTop = qMin(minVisibleTop, poseTop[i] + actionOffsets[i]);
        maxVisibleBottom = qMax(maxVisibleBottom, poseBottom[i] + actionOffsets[i]);
    }
    const int canvasTopPadding = qMax(0, -minVisibleTop);
    const int canvasBottomPadding = qMax(0, maxVisibleBottom - fh);
    const int canvasHeight = fh + canvasTopPadding + canvasBottomPadding;
    QJsonArray offsetsForInfo, poseOffsetsForInfo, contactsForInfo;
    for (int i = 0; i < 5; ++i) {
        actionOffsets[i] += canvasTopPadding;
        offsetsForInfo.append(actionOffsets[i]);
        poseOffsetsForInfo.append(poseOffsets[i]);
        contactsForInfo.append(actionContacts[i]);
    }
    json.insert("canvasHeight", canvasHeight);
    json.insert("groundAnchorY", sharedFoot + canvasTopPadding);
    json.insert("actionOffsetsY", offsetsForInfo);
    json.insert("poseOffsetsY", poseOffsetsForInfo);
    json.insert("actionContactY", contactsForInfo);
    // Commit all three decoded sheets together. Failure above preserves old pet.
    for (int i = 0; i < 5; ++i) {
        images_[i] = incoming[i]; counts_[i] = counts[i]; actionOffsetsY_[i] = actionOffsets[i];
    }
    groundAnchorY_ = sharedFoot + canvasTopPadding;
    fw_ = fw; fh_ = fh; info_ = json.toVariantMap(); folder_ = url;
    frame_ = 0; error_.clear(); update(); emit changed(); emit frameChanged();
}
double PetSpriteItem::frameFootY() const {
    // The canvas anchor is shared across actions; pose-specific foot offsets
    // are drawn inside the canvas rather than moving the QML item itself.
    return groundAnchorY_;
}
int PetSpriteItem::actionIndex() const { return qMax(0, int(actions.indexOf(action_))); }
QRect PetSpriteItem::frameRect() const {
    const int i = actionIndex(), columns = qMax(1, images_[i].width()/fw_);
    const int frame = qMax(0, frame_) % counts_[i];
    return QRect((frame%columns)*fw_, (frame/columns)*fh_, fw_, fh_);
}
void PetSpriteItem::setAction(const QString &value) {
    if (!actions.contains(value) || value == action_) return;
    action_ = value; frame_ = 0; update(); emit frameChanged();
}
void PetSpriteItem::setCurrentFrame(int value) {
    value = qMax(0, value);
    if (frame_ == value) return;
    frame_ = value; update(); emit frameChanged();
}
void PetSpriteItem::setMirrored(bool value) {
    if (mirrored_ == value) return;
    mirrored_ = value; update(); emit frameChanged();
}
bool PetSpriteItem::contains(const QPointF &point) const {
    if (!ready() || width() <= 0 || height() <= 0 || !boundingRect().contains(point)) return false;
    const int index = actionIndex();
    const QRect r = frameRect();
    const double scale = width() / fw_;
    int x = qBound(0, int(point.x()*fw_/width()), fw_-1);
    if (mirrored_) x = fw_-1-x;
    const int y = int(point.y()/scale) - actionOffsetsY_[index];
    if (y < 0 || y >= fh_) return false;
    return qAlpha(images_[actionIndex()].pixel(r.x()+x, r.y()+y)) > 32;
}
void PetSpriteItem::paint(QPainter *painter) {
    // Ark-Pets renders into a fixed-size transparent FBO: clear the full target
    // on every frame, then draw the current pose. Do the same here so changing
    // action/frame never leaves pixels from the previous sprite image behind.
    painter->save();
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    painter->fillRect(boundingRect(), Qt::transparent);
    if (ready() && width() > 0 && height() > 0) {
        const int index = actionIndex();
        const double scale = width() / fw_;
        const QRectF target(0, actionOffsetsY_[index] * scale, fw_ * scale, fh_ * scale);
        if (mirrored_) {
            painter->translate(width(), 0);
            painter->scale(-1, 1);
        }
        painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(target, images_[index], frameRect());
    }
    painter->restore();
}
