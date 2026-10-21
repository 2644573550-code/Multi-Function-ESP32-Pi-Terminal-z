#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>

// Controls the user's default PulseAudio / PipeWire-Pulse sink, never media
// streams individually. All commands are asynchronous and slider writes coalesce.
class SystemVolume : public QObject {
    Q_OBJECT
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
public:
    explicit SystemVolume(QObject *parent = nullptr);
    ~SystemVolume() override;
    int volume() const { return volume_; }
    bool muted() const { return muted_; }
    bool available() const { return available_; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setVolume(int value);
    Q_INVOKABLE void setMuted(bool muted);
signals:
    void changed();
private:
    enum Operation { Idle, ReadVolume, ReadMute, WriteVolume, WriteMute };
    void pump();
    void complete(bool success);
    QProcess process_;
    QTimer timeout_, coalesce_;
    Operation operation_ = Idle;
    QString program_;
    int volume_ = 0, readVolume_ = 0, pendingVolume_ = -1, pendingMute_ = -1;
    bool muted_ = false, available_ = false, refreshPending_ = false, readMutePending_ = false;
};
