#include "SystemVolume.h"
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

SystemVolume::SystemVolume(QObject *parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    timeout_.setInterval(2500);
    coalesce_.setSingleShot(true);
    coalesce_.setInterval(80);
    connect(&coalesce_, &QTimer::timeout, this, &SystemVolume::pump);
    connect(&timeout_, &QTimer::timeout, this, [this] { process_.kill(); });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) { complete(code == 0 && status == QProcess::NormalExit); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false);
    });
}

SystemVolume::~SystemVolume() {
    process_.disconnect(this);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}

void SystemVolume::refresh() {
    refreshPending_ = true;
    pump();
}

void SystemVolume::setVolume(int value) {
    if (!available_) return;
    pendingVolume_ = qBound(0, value, 100);
    if (!coalesce_.isActive()) coalesce_.start();
}

void SystemVolume::setMuted(bool muted) {
    if (!available_) return;
    pendingMute_ = muted ? 1 : 0;
    pump();
}

void SystemVolume::pump() {
    if (operation_ != Idle || coalesce_.isActive()) return;
    program_ = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (program_.isEmpty()) {
        available_ = false;
        pendingVolume_ = pendingMute_ = -1;
        refreshPending_ = readMutePending_ = false;
        emit changed();
        return;
    }
    QStringList args;
    if (pendingVolume_ >= 0) {
        readMutePending_ = false;
        operation_ = WriteVolume;
        args = {"set-sink-volume", "@DEFAULT_SINK@", QString::number(pendingVolume_) + '%'};
        pendingVolume_ = -1;
    } else if (pendingMute_ >= 0) {
        readMutePending_ = false;
        operation_ = WriteMute;
        args = {"set-sink-mute", "@DEFAULT_SINK@", QString::number(pendingMute_)};
        pendingMute_ = -1;
    } else if (readMutePending_) {
        operation_ = ReadMute;
        readMutePending_ = false;
        args = {"get-sink-mute", "@DEFAULT_SINK@"};
    } else if (refreshPending_) {
        operation_ = ReadVolume;
        refreshPending_ = false;
        args = {"get-sink-volume", "@DEFAULT_SINK@"};
    } else return;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    process_.setProcessEnvironment(env);
    process_.start(program_, args);
    timeout_.start();
}

void SystemVolume::complete(bool success) {
    if (operation_ == Idle) return;
    timeout_.stop();
    const auto output = QString::fromUtf8(process_.readAllStandardOutput()).trimmed();
    process_.readAllStandardError();
    const auto operation = operation_;
    operation_ = Idle;
    if (success && operation == ReadVolume) {
        const auto match = QRegularExpression(QStringLiteral("(\\d+)%")).match(output);
        success = match.hasMatch();
        if (success) { readVolume_ = qBound(0, match.captured(1).toInt(), 100); readMutePending_ = true; }
    } else if (success && operation == ReadMute) {
        success = output == "Mute: yes" || output == "Mute: no";
        if (success) { volume_ = readVolume_; muted_ = output == "Mute: yes"; available_ = true; emit changed(); }
    } else if (success) refreshPending_ = true;
    if (!success) {
        available_ = false;
        pendingVolume_ = pendingMute_ = -1;
        refreshPending_ = readMutePending_ = false;
        emit changed();
    }
    // Avoid starting another QProcess inside its own finished/error emission.
    QTimer::singleShot(0, this, &SystemVolume::pump);
}
