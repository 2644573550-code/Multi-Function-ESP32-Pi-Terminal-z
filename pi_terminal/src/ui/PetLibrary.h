#pragma once
#include <QObject>
#include <QProcess>
#include <QUrl>
#include <QVariantList>
#include <QHash>

class PetLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QUrl currentFolder READ currentFolder NOTIFY changed)
    Q_PROPERTY(QString currentModelId READ currentModelId NOTIFY changed)
    Q_PROPERTY(bool hasCatalog READ hasCatalog NOTIFY changed)
    Q_PROPERTY(double sizeMultiplier READ sizeMultiplier WRITE setSizeMultiplier NOTIFY sizeChanged)
    Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY qualityChanged)
public:
    explicit PetLibrary(QObject *parent = nullptr);
    ~PetLibrary() override;
    bool busy() const { return process_.state() != QProcess::NotRunning; }
    QString status() const { return status_; }
    QVariantList rows() const { return rows_; }
    QUrl currentFolder() const { return folder_; }
    QString currentModelId() const { return currentId_; }
    bool hasCatalog() const { return hasCatalog_; }
    double sizeMultiplier() const { return sizeMultiplier_; }
    void setSizeMultiplier(double value);
    QString quality() const {return quality_;}
    void setQuality(const QString &value);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void download(const QString &id, bool licenseAccepted);
    Q_INVOKABLE void resetToAmiya();
    Q_INVOKABLE void useInstalled(const QString &id);
    Q_INVOKABLE QUrl previewAtlas(const QString &id) const;
signals:
    void changed();
    void sizeChanged();
    void qualityChanged();
private:
    void start(const QStringList &args);
    void readOutput();
    void updateInstalled();
    void loadCachedCatalog();
    QProcess process_;
    QByteArray output_;
    QString status_;
    QVariantList rows_;
    QUrl folder_ = QUrl("qrc:/arkpets/amiya");
    QUrl pendingFolder_;
    QString currentId_ = "002_amiya";
    QHash<QString,QUrl> installed_;
    bool hasCatalog_ = false;
    double sizeMultiplier_ = 1.0;
    QString quality_="standard";
};
