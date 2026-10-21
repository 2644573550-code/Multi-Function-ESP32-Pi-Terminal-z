#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QElapsedTimer>

// Transport only: browsing and queue policy must never own these processes.
class MusicPlayback : public QObject {
    Q_OBJECT
public:
    explicit MusicPlayback(QObject *parent = nullptr);
    ~MusicPlayback() override;
    void configure(const QString &program, const QProcessEnvironment &environment);
    bool play(const QStringList &arguments);
    bool control(const QString &action, const QString &value = {});
    QVariantMap state() const { return state_; }
    QString error() const { return error_; }
    int progress() const { return progress_; }
    bool switching() const { return progress_ > 0 && progress_ < 100; }
signals:
    void changed();
    void ended();
private:
    void beginStop();
    void startJob(const QStringList &arguments, const QString &phase);
    void poll();
    void fail(const QString &message);
    QString program_, phase_, error_;
    QStringList pending_;
    QProcessEnvironment environment_;
    QProcess job_, query_, control_;
    QTimer pollTimer_, deadline_;
    QVariantMap state_;
    quint64 generation_ = 0, jobGeneration_ = 0, queryGeneration_ = 0;
    quint64 querySerial_ = 0;
    quint64 controlGeneration_ = 0;
    int progress_ = 0;
    bool accepted_ = false, wasPlaying_ = false;
    QElapsedTimer dispatchTime_;
};
