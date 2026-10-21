#include "MusicPlayback.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace {
bool rejected(const QByteArray &output) {
    const auto object = QJsonDocument::fromJson(output).object();
    return (object.value("success").isBool() && !object.value("success").toBool()) ||
        (object.value("code").isDouble() && object.value("code").toInt() != 200);
}
}

MusicPlayback::MusicPlayback(QObject *parent) : QObject(parent) {
    deadline_.setSingleShot(true);
    deadline_.setInterval(20000);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        qWarning() << "MUSIC_SWITCH_TIMEOUT" << generation_ << phase_ << progress_ << job_.state();
        fail(QStringLiteral("切歌超时（%1），请重试").arg(phase_));
        ++generation_;
        pending_.clear();
        job_.kill(); query_.kill(); control_.kill();
    });
    connect(&job_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            const auto output = job_.readAllStandardOutput();
            const auto error = job_.readAllStandardError().trimmed();
            qInfo() << "MUSIC_JOB_FINISHED" << jobGeneration_ << phase_ << code << status << "bytes" << output.size();
            if (jobGeneration_ != generation_) {
                if (!pending_.isEmpty()) beginStop();
                return;
            }
            if (status != QProcess::NormalExit || code != 0 || rejected(output)) {
                fail(error.isEmpty() ? QStringLiteral("播放命令失败或歌曲不可用") : QString::fromUtf8(error));
                return;
            }
            if (phase_ == QStringLiteral("stop")) {
                progress_ = 50;
                const auto arguments = pending_;
                pending_.clear();
                startJob(arguments, QStringLiteral("play"));
            } else {
                // Dispatch success is not playback success. Wait for engine state.
                accepted_ = true;
                if (progress_ != 100) progress_ = 75;
                pollTimer_.start();
                poll();
            }
            emit changed();
        });
    connect(&job_, &QProcess::started, this, [this] {
        if (jobGeneration_ != generation_ || phase_ != QStringLiteral("play")) return;
        // A CLI wrapper may remain attached. Engine polling must not depend on
        // its exit; that would turn successful playback into a switch timeout.
        accepted_ = true;
        dispatchTime_.start();
        progress_ = 75;
        pollTimer_.start();
        poll();
        emit changed();
    });
    connect(&job_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && jobGeneration_ == generation_)
            fail(QStringLiteral("无法启动在线播放器，请检查 Node.js 和 ncm-cli"));
    });
    connect(&query_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            const auto output = query_.readAllStandardOutput();
            query_.readAllStandardError();
            if (queryGeneration_ != generation_ || status != QProcess::NormalExit || code != 0 || rejected(output)) return;
            const auto object = QJsonDocument::fromJson(output).object().value("state").toObject();
            if (object.isEmpty()) return;
            const auto name = object.value("status").toString().toLower();
            if (switching()) qInfo() << "MUSIC_SWITCH_STATE" << generation_ << name;
            const bool playing = name == QStringLiteral("playing");
            const bool paused = name == QStringLiteral("paused");
            if (switching() && accepted_ && name == QStringLiteral("stopped") &&
                dispatchTime_.isValid() && dispatchTime_.elapsed() > 6000 && job_.state() == QProcess::NotRunning) {
                fail(QStringLiteral("播放服务已接收请求，但歌曲未能播放（可能无音源、无播放权限或音频服务异常），请换一首重试"));
                return;
            }
            const double previousPosition = state_.value("position").toDouble();
            const double previousDuration = state_.value("duration").toDouble();
            state_ = object.toVariantMap();
            if (switching() && accepted_ && (playing || paused)) {
                progress_ = 100;
                deadline_.stop();
            }
            const bool finished = !switching() && wasPlaying_ && !playing && !paused &&
                previousDuration > 0 && previousPosition >= previousDuration - 2;
            wasPlaying_ = playing || (paused && wasPlaying_);
            emit changed();
            if (finished) emit ended();
        });
    connect(&control_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            const auto output = control_.readAllStandardOutput();
            const auto error = control_.readAllStandardError();
            if (controlGeneration_ != generation_) return;
            if (status != QProcess::NormalExit || code != 0 || rejected(output)) {
                error_ = error.isEmpty() ? QStringLiteral("播放控制失败") : QString::fromUtf8(error).trimmed();
                emit changed();
            }
            poll();
        });
    connect(&control_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && controlGeneration_ == generation_) {
            error_ = QStringLiteral("无法启动播放控制命令");
            emit changed();
        }
    });
    pollTimer_.setInterval(1000);
    connect(&pollTimer_, &QTimer::timeout, this, &MusicPlayback::poll);
}

MusicPlayback::~MusicPlayback() {
    for (auto *process : {&job_, &query_, &control_}) {
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
}

void MusicPlayback::configure(const QString &program, const QProcessEnvironment &environment) {
    program_ = program;
    environment_ = environment;
}

bool MusicPlayback::play(const QStringList &arguments) {
    if (program_.isEmpty() || arguments.isEmpty()) return false;
    ++generation_;
    pending_ = arguments;
    accepted_ = false;
    wasPlaying_ = false;
    state_.clear();
    error_.clear();
    progress_ = 10;
    deadline_.start();
    // Late results from any old play/state command cannot confirm the new song.
    query_.kill();
    control_.kill();
    if (job_.state() != QProcess::NotRunning) job_.kill();
    else beginStop();
    emit changed();
    return true;
}

void MusicPlayback::beginStop() {
    startJob({QStringLiteral("stop"), QStringLiteral("--output"), QStringLiteral("json")}, QStringLiteral("stop"));
}

void MusicPlayback::startJob(const QStringList &arguments, const QString &phase) {
    phase_ = phase;
    jobGeneration_ = generation_;
    qInfo() << "MUSIC_JOB_START" << generation_ << phase;
    job_.setProcessEnvironment(environment_);
    job_.start(program_, arguments);
}

void MusicPlayback::poll() {
    if (!accepted_ || query_.state() != QProcess::NotRunning) return;
    queryGeneration_ = generation_;
    query_.setProcessEnvironment(environment_);
    query_.start(program_, {QStringLiteral("state"), QStringLiteral("--output"), QStringLiteral("json")});
    // A hung status command must not permanently disable polling.
    const auto serial = ++querySerial_;
    QTimer::singleShot(4000, &query_, [this, serial] {
        if (serial == querySerial_ && query_.state() != QProcess::NotRunning) query_.kill();
    });
}

bool MusicPlayback::control(const QString &action, const QString &value) {
    if (action == QStringLiteral("state")) { poll(); return true; }
    if (switching() || program_.isEmpty() || control_.state() != QProcess::NotRunning) return false;
    if (action != QStringLiteral("pause") && action != QStringLiteral("resume") && action != QStringLiteral("stop") &&
        action != QStringLiteral("volume") && action != QStringLiteral("seek")) return false;
    if (action == QStringLiteral("stop")) {
        ++generation_;
        query_.kill();
        accepted_ = false; wasPlaying_ = false;
        pollTimer_.stop(); state_.clear();
    }
    QStringList arguments{action};
    if (!value.isEmpty()) arguments.append(value);
    control_.setProcessEnvironment(environment_);
    controlGeneration_ = generation_;
    control_.start(program_, arguments);
    emit changed();
    return true;
}

void MusicPlayback::fail(const QString &message) {
    error_ = message;
    progress_ = 0;
    accepted_ = false;
    pending_.clear();
    deadline_.stop();
    pollTimer_.stop();
    emit changed();
}
