#pragma once
#include <QObject>
#include <QVariantList>
#include <QTextToSpeech>
#include <QProcess>
#include <QTimer>
#include <functional>
#include <QSettings>
#include <QBluetoothLocalDevice>
#include <QNetworkAccessManager>
#include <QVariantMap>
#include <QUrl>
#include <QImage>
#include <QColor>
#include <QSet>
#include <QPointer>
#include <QHash>
#include <QVector>
#include "MusicPlayback.h"
#include "RemovableStorage.h"
#include "SystemVolume.h"
class SystemTools : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList logs READ logs NOTIFY changed)
    Q_PROPERTY(QString network READ network NOTIFY changed)
    Q_PROPERTY(QString filesRoot READ filesRoot CONSTANT)
    Q_PROPERTY(QVariantList removableDevices READ removableDevices NOTIFY removableDevicesChanged)
    Q_PROPERTY(bool speechEnabled READ speechEnabled NOTIFY changed)
    Q_PROPERTY(SystemVolume* systemVolume READ systemVolume CONSTANT)
    Q_PROPERTY(bool duckMediaForSpeech READ duckMediaForSpeech NOTIFY audioFocusChanged)
    Q_PROPERTY(bool speechActive READ speechActive NOTIFY audioFocusChanged)
    Q_PROPERTY(double mediaGain READ mediaGain NOTIFY audioFocusChanged)
    Q_PROPERTY(double interfaceScale READ interfaceScale NOTIFY changed)
    Q_PROPERTY(QVariantMap deviceStatus READ deviceStatus NOTIFY changed)
    Q_PROPERTY(bool hotspotBusy READ hotspotBusy NOTIFY changed)
    Q_PROPERTY(bool powerActionBusy READ powerActionBusy NOTIFY changed)
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY changed)
    Q_PROPERTY(double launcherOpacity READ launcherOpacity NOTIFY changed)
    Q_PROPERTY(double moduleOpacity READ moduleOpacity NOTIFY changed)
    Q_PROPERTY(bool energySaving READ energySaving NOTIFY changed)
    Q_PROPERTY(QString uiLanguage READ uiLanguage NOTIFY changed)
    Q_PROPERTY(bool musicCliAvailable READ musicCliAvailable NOTIFY musicCliChanged)
    Q_PROPERTY(bool musicNetworkOnline READ musicNetworkOnline NOTIFY musicNetworkChanged)
    Q_PROPERTY(bool musicCliAuthenticated READ musicCliAuthenticated NOTIFY musicCliChanged)
    Q_PROPERTY(bool musicCliBusy READ musicCliBusy NOTIFY musicCliChanged)
    Q_PROPERTY(bool musicCliLoading READ musicCliLoading NOTIFY musicCliChanged)
    Q_PROPERTY(int musicCliPlayProgress READ musicCliPlayProgress NOTIFY musicCliChanged)
    Q_PROPERTY(bool musicPlaybackSwitching READ musicPlaybackSwitching NOTIFY musicCliChanged)
    Q_PROPERTY(QString musicCliOutput READ musicCliOutput NOTIFY musicCliChanged)
    Q_PROPERTY(QString musicCliError READ musicCliError NOTIFY musicCliChanged)
    Q_PROPERTY(QString musicCliAction READ musicCliAction NOTIFY musicCliChanged)
    Q_PROPERTY(QVariantList musicCliTracks READ musicCliTracks NOTIFY musicCliChanged)
    Q_PROPERTY(QVariantMap musicCliPlaybackState READ musicCliPlaybackState NOTIFY musicCliChanged)
    Q_PROPERTY(int musicCliPlaylistOffset READ musicCliPlaylistOffset NOTIFY musicCliChanged)
    Q_PROPERTY(int musicCliPlaylistTotal READ musicCliPlaylistTotal NOTIFY musicCliChanged)
    Q_PROPERTY(bool musicCliHasNextPlaylistPage READ musicCliHasNextPlaylistPage NOTIFY musicCliChanged)
public:
    Q_INVOKABLE QString saveCameraFrame(const QString &frame, const QUrl &destination);
    Q_INVOKABLE QString cameraPhotoPath() const;
    bool musicCliAvailable() const;
    bool musicNetworkOnline() const { return musicNetworkOnline_; }
    bool musicCliAuthenticated() const { return musicCliAuthenticated_; }
    bool musicCliBusy() const { return musicCliProcess_.state() != QProcess::NotRunning; }
    bool musicCliLoading() const {
        return musicCliProcess_.state() != QProcess::NotRunning && musicCliAction_ != QStringLiteral("state") &&
            musicCliAction_ != QStringLiteral("volume") && musicCliAction_ != QStringLiteral("seek");
    }
    int musicCliPlayProgress() const { return musicPlayback_.progress(); }
    bool musicPlaybackSwitching() const { return musicPlayback_.switching(); }
    QString musicCliOutput() const { return musicCliOutput_; }
    QString musicCliError() const { return musicCliError_; }
    QString musicCliAction() const { return musicCliAction_; }
    QVariantList musicCliTracks() const { return musicCliTracks_; }
    QVariantMap musicCliPlaybackState() const { return musicPlayback_.state(); }
    int musicCliPlaylistOffset() const { return musicCliPlaylistOffset_; }
    int musicCliPlaylistTotal() const { return musicCliPlaylistTotal_; }
    bool musicCliHasNextPlaylistPage() const;
    Q_INVOKABLE bool launchMusicCliSetup(const QString &action);
    Q_INVOKABLE bool musicCliSearch(const QString &keyword, const QString &type = QStringLiteral("song"));
    Q_INVOKABLE bool musicCliControl(const QString &action);
    Q_INVOKABLE bool musicCliPlaylists(const QString &scope);
    Q_INVOKABLE bool musicCliPlaylistTracks(const QString &playlistId, int offset, int total);
    Q_INVOKABLE bool musicCliPlay(const QString &encryptedId, const QString &originalId, const QString &type = QStringLiteral("song"));
    Q_INVOKABLE bool musicCliVolume(int volume);
    Q_INVOKABLE bool musicCliCheckLogin();
    Q_INVOKABLE bool musicCliSeek(int positionSeconds);
    double launcherOpacity() const { return qBound(0.0,settings_.value("launcherOpacity",0.65).toDouble(),1.0); }
    double moduleOpacity() const { return qBound(0.0,settings_.value("moduleOpacity",0.65).toDouble(),1.0); }
    Q_INVOKABLE void setLauncherOpacity(double value);
    Q_INVOKABLE void setModuleOpacity(double value);
    bool energySaving() const { return settings_.value("energySaving", true).toBool(); }
    QString uiLanguage() const { return settings_.value("uiLanguage", QStringLiteral("zh")).toString(); }
    Q_INVOKABLE void setEnergySaving(bool value);
    Q_INVOKABLE void setUiLanguage(const QString &language);
    explicit SystemTools(QObject *parent = nullptr);
    ~SystemTools() override;
    QStringList logs() const { return logs_; }
    QString network() const;
    QString filesRoot() const;
    QVariantList removableDevices() const { return removableStorage_.devices(); }
    Q_INVOKABLE void openRemovableDevice(const QString &id) { removableStorage_.open(id); }
    Q_INVOKABLE QString fileDisplayPath(const QString &relative) const;
    Q_INVOKABLE QVariantList files(const QString &relative) const;
    Q_INVOKABLE QUrl fileUrl(const QString &relative) const;
    Q_INVOKABLE QString fileLocation(const QString &name) const;
    Q_INVOKABLE QVariantMap localMusicMetadata(const QString &relative) const;
    Q_INVOKABLE bool createFile(const QString &relative, const QString &text);
    Q_INVOKABLE QVariantList recentFiles() const;
    Q_INVOKABLE void rememberFile(const QString &relative);
    Q_INVOKABLE QString read(const QString &relative) const;
    Q_INVOKABLE bool editable(const QString &relative) const;
    Q_INVOKABLE bool openWithDefault(const QString &relative);
    Q_INVOKABLE bool write(const QString &relative, const QString &text);
    Q_INVOKABLE bool makeDirectory(const QString &relative);
    Q_INVOKABLE bool rename(const QString &from, const QString &to);
    Q_INVOKABLE bool remove(const QString &relative);
    bool speechEnabled() const { return settings_.value("speech", true).toBool(); }
    SystemVolume *systemVolume() { return &systemVolume_; }
    bool duckMediaForSpeech() const { return settings_.value("duckMediaForSpeech", true).toBool(); }
    bool speechActive() const { return speechActive_; }
    double mediaGain() const { return duckMediaForSpeech() && speechActive_ ? 0.2 : 1.0; }
    Q_INVOKABLE void setDuckMediaForSpeech(bool enabled);
    double interfaceScale() const { return qBound(1.0, settings_.value("interface_scale", 1.0).toDouble(), 1.5); }
    Q_INVOKABLE void setInterfaceScale(double scale);
    QVariantMap deviceStatus() const { return deviceStatus_; }
    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void setBluetoothEnabled(bool enabled);
    Q_INVOKABLE void speak(const QString &text);
    Q_INVOKABLE void remember(const QString &url);
    Q_INVOKABLE void removeBookmark(const QString &url);
    Q_INVOKABLE QStringList bookmarks() const;
    Q_INVOKABLE void setSpeechEnabled(bool enabled);
    Q_INVOKABLE void hotspot(bool enabled);
    void acquireHotspotLease(const QString &owner);
    void releaseHotspotLease(const QString &owner);
    Q_INVOKABLE bool powerActionBusy() const { return powerActionBusy_; }
    Q_INVOKABLE void requestPowerAction(const QString &action);
    QUrl wallpaper() const;
    Q_INVOKABLE QColor wallpaperColor(double x, double y, double width, double height) const;
    Q_INVOKABLE QUrl monochromeIcon(const QString &name, bool white) const;
    Q_INVOKABLE bool setWallpaper(const QUrl &url);
    Q_INVOKABLE bool setFileWallpaper(const QString &relative);
    Q_INVOKABLE void captureKeyboardTarget();
    Q_INVOKABLE void captureKeyboardTargetItem(QObject *target);
    Q_INVOKABLE void keyboardCommit(const QString &text);
    Q_INVOKABLE void keyboardBackspace();
    Q_INVOKABLE void keyboardReturn();
    Q_INVOKABLE QString pinyinDictionary() const;
    Q_INVOKABLE QString pinyinPhraseDictionary() const;
    Q_INVOKABLE QStringList pinyinCandidates(const QString &code) const;
    Q_INVOKABLE void recordPinyinSelection(const QString &code, const QString &word);
    bool hotspotBusy() const { return hotspotBusy_; }
    void log(const QString &text);
signals:
    void audioFocusChanged();
    void changed();
    void removableDevicesChanged();
    void removableDeviceInserted(QString name);
    void removableDeviceReady(QString location);
    void removableDeviceError(QString message);
    void notification(QString message);
    void hotspotLeaseReady(QString owner, bool ready);
    void musicCliChanged();
    void musicNetworkChanged();
    void musicPlaybackEnded();
private:
    SystemVolume systemVolume_;
    bool speechActive_ = false;
    void setSpeechActive(bool active);
    RemovableStorage removableStorage_;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QPointer<QObject> keyboardTarget_;
    struct PinyinCandidate { QString word; int weight = 0; };
    QHash<QString, QVector<PinyinCandidate>> characterPinyin_;
    QHash<QString, QVector<PinyinCandidate>> phrasePinyin_;
    void loadPinyinDictionaries();
    QTextToSpeech speech_;
    QProcess speechProcess_;
    QProcess musicCliProcess_;
    MusicPlayback musicPlayback_;
    QString musicCliAction_, musicCliOutput_, musicCliError_, musicCliSearchType_;
    QVariantList musicCliTracks_;
    QVariantMap musicCliPlaybackState_;
    QString musicCliPlaylistId_;
    int musicCliPlaylistOffset_ = 0, musicCliPlaylistTotal_ = 0;
    bool musicCliAuthenticated_ = false;
    bool musicNetworkOnline_ = false;
    QNetworkAccessManager musicNetworkManager_;
    QTimer musicNetworkProbeTimer_;
    void probeMusicNetwork();
    QTimer speechTimeout_;
    QString speechProgram_, pendingSpeech_;
    bool qtChineseVoice_ = false;
    void startPendingSpeech();
    QStringList logs_;
    QSettings settings_;
    QBluetoothLocalDevice bluetooth_;
    QVariantMap deviceStatus_;
    bool hotspotBusy_ = false;
    bool powerActionBusy_ = false;
    bool hotspotActive_ = false, hotspotKnown_ = false, hotspotQueryBusy_ = false;
    QSet<QString> hotspotLeases_;
    QSet<QString> pendingHotspotLeases_;
    bool hotspotOpenedByTerminal_ = false;
    bool hotspotUserPinned_ = false;
    void queryHotspot(std::function<void(bool, bool)> done = {});
    void setHotspot(bool enabled, std::function<void(bool, bool)> done = {});
    void processHotspotLeaseRequests();
    void completeHotspotLeaseRequests(bool ready);
    QString hotspotOwnershipMarker() const;
    void setHotspotOwnershipMarker(bool owned);
    mutable QUrl sampledWallpaper_;
    mutable QImage wallpaperSample_;
    QString safePath(const QString &relative, bool creating = false) const;
    mutable QHash<QString, QVariantMap> localMusicMetadataCache_;
    bool prepareMusicCliProcess();
};
