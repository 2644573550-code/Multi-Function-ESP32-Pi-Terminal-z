#include "PetLibrary.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <cmath>

namespace {
QString libraryRoot() { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/arkpets"; }
QJsonObject readManifest(const QString &path, qint64 limit = 65536) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}

PetLibrary::PetLibrary(QObject *parent) : QObject(parent) {
    QSettings settings;
    const double savedSize=settings.value("arkpets/sizeMultiplier",1.0).toDouble();
    sizeMultiplier_=std::isfinite(savedSize)?qBound(0.5,savedSize,2.0):1.0;
    const QString quality=settings.value("arkpets/quality","standard").toString();
    if(QStringList{"economy","standard","high"}.contains(quality))quality_=quality;
    const auto saved = settings.value("arkpets/currentFolder").toUrl();
    if (saved.isLocalFile() && QFileInfo::exists(saved.toLocalFile()+"/animations.json")) {
        folder_ = saved;
        currentId_ = readManifest(saved.toLocalFile()+"/animations.json").value("model").toString("002_amiya");
    }
    loadCachedCatalog();
    updateInstalled();
    connect(&process_, &QProcess::readyReadStandardOutput, this, &PetLibrary::readOutput);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::started, this, &PetLibrary::changed);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { status_ = "无法启动 Python3 转换进程"; emit changed(); }
    });
    connect(&process_, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
        readOutput();
        if (code == 0 && exit == QProcess::NormalExit && pendingFolder_.isLocalFile()) {
            folder_ = pendingFolder_; QSettings().setValue("arkpets/currentFolder", folder_);
            currentId_ = readManifest(folder_.toLocalFile()+"/animations.json").value("model").toString();
        } else if (code != 0 && status_.isEmpty()) status_ = "下载/转换失败，保留当前角色";
        pendingFolder_ = QUrl(); updateInstalled(); emit changed();
    });
}
void PetLibrary::setSizeMultiplier(double value) {
    if(!std::isfinite(value))return;
    value=qBound(0.5,value,2.0);
    if(qAbs(sizeMultiplier_-value)<0.00001)return;
    sizeMultiplier_=value;
    QSettings().setValue("arkpets/sizeMultiplier",value);
    emit sizeChanged();
}
void PetLibrary::setQuality(const QString &value) {
    if(busy() || !QStringList{"economy","standard","high"}.contains(value) || quality_==value)return;
    quality_=value;QSettings().setValue("arkpets/quality",value);
    updateInstalled();emit qualityChanged();emit changed();
}
PetLibrary::~PetLibrary() {
    if (busy()) {
        // Worker handles termination by stopping its active converter and
        // cleaning only its temporary job directory.
        process_.terminate();
        if (!process_.waitForFinished(3000)) { process_.kill(); process_.waitForFinished(); }
    }
}
void PetLibrary::readOutput() {
    output_ += process_.readAllStandardOutput();
    if (output_.size() > 4*1024*1024) { process_.kill(); status_ = "转换进程输出超限"; output_.clear(); emit changed(); return; }
    int pos;
    while ((pos = output_.indexOf('\n')) >= 0) {
        const auto line = output_.left(pos); output_.remove(0, pos+1);
        const auto object = QJsonDocument::fromJson(line).object();
        if (object.contains("status")) status_ = object.value("status").toString();
        if (object.contains("error")) status_ = object.value("error").toString();
        if (object.contains("rows")) { rows_ = object.value("rows").toVariant().toList(); hasCatalog_=!rows_.isEmpty(); updateInstalled(); }
        if (object.contains("folder")) pendingFolder_ = QUrl(object.value("folder").toString());
        emit changed();
    }
}
void PetLibrary::start(const QStringList &args) {
    if (busy()) return;
    QString script = QCoreApplication::applicationDirPath()+"/../libexec/orange-pi-terminal/arkpets/character_worker.py";
    if (!QFileInfo::exists(script)) script = QStringLiteral(ARKPETS_CONVERTER_SOURCE) + "/character_worker.py";
    if (!QFileInfo::exists(script)) { status_ = "未找到角色转换工具"; emit changed(); return; }
    output_.clear(); pendingFolder_ = QUrl(); status_ = "正在启动角色任务";
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/arkpets";
    // Qt SDK's Python venv is often ahead of the system interpreter in PATH
    // and lacks Pillow. Use the target's explicitly configured/system Python.
    process_.start(qEnvironmentVariable("ARKPETS_PYTHON", "/usr/bin/python3"),
                   QStringList{script} + args + QStringList{"--root", root});
    emit changed();
}
void PetLibrary::refresh() { start({"catalog"}); }
void PetLibrary::download(const QString &id, bool licenseAccepted) {
    if (!licenseAccepted) { status_ = "请先确认素材与转换工具授权说明"; emit changed(); return; }
    start({"install", "--model", id, "--accept-licenses", "--quality",quality_});
}
void PetLibrary::resetToAmiya() {
    if (busy()) return;
    folder_ = QUrl("qrc:/arkpets/amiya"); currentId_ = "002_amiya";
    QSettings().remove("arkpets/currentFolder"); updateInstalled(); emit changed();
}
void PetLibrary::loadCachedCatalog() {
    const auto data = readManifest(libraryRoot()+"/catalog.json", 8*1024*1024).value("data").toObject();
    hasCatalog_ = !data.isEmpty();
    for (auto it = data.begin(); it != data.end(); ++it) {
        const auto value = it.value().toObject();
        const QString type=value.value("type").toString();
        if (type != "Operator" && type != "DynIllust") continue;
        const QString name = value.value("name").toString(it.key());
        const QString skin = value.value("skinGroupName").toString("默认服装");
        const QString english = value.value("appellation").toString();
        rows_.append(QVariantMap{{"id",it.key()}, {"name",name+" · "+skin}, {"character",name},
            {"modelName",skin}, {"appellation",english}, {"category",type=="DynIllust"?"illustration":value.value("style").toString()=="BuildingDefault"?"default":"skin"},
            {"search",it.key()+" "+name+" "+english}});
    }
}
void PetLibrary::updateInstalled() {
    installed_.clear(); installed_.insert("002_amiya", QUrl("qrc:/arkpets/amiya"));
    const QDir dir(libraryRoot()+"/characters");
    for (const auto &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.isSymLink()) continue;
        const auto manifest = readManifest(entry.absoluteFilePath()+"/animations.json");
        const QString id = manifest.value("model").toString();
        if (!id.isEmpty()) {
            const QUrl candidate=QUrl::fromLocalFile(entry.absoluteFilePath());
            const bool matching=manifest.value("quality").toString()==quality_ && manifest.value("conversionVersion").toInt()>=4;
            const auto previous=readManifest(installed_.value(id).toLocalFile()+"/animations.json");
            const bool previousMatching=previous.value("quality").toString()==quality_ && previous.value("conversionVersion").toInt()>=4;
            if(!installed_.contains(id) || (matching&&!previousMatching) || (matching==previousMatching && candidate==folder_)) installed_.insert(id,candidate);
        }
    }
    if (rows_.isEmpty()) rows_.append(QVariantMap{{"id","002_amiya"},{"name","阿米娅 · 默认服装"},
        {"character","阿米娅"},{"modelName","默认服装"},{"appellation","Amiya"},{"category","default"},{"search","阿米娅 Amiya 002_amiya"}});
    for (auto &row : rows_) {
        auto map = row.toMap(); const QString id = map.value("id").toString();
        map.insert("installed", installed_.contains(id));
        map.insert("current", id==currentId_ && installed_.value(id)==folder_);
        const QUrl local=installed_.value(id);
        const auto manifest=local.isLocalFile()?readManifest(local.toLocalFile()+"/animations.json"):readManifest(":/arkpets/amiya/animations.json");
        const bool needsUpgrade=manifest.value("conversionVersion").toInt()<4 || manifest.value("quality").toString()!=quality_;
        map.insert("needsUpgrade",needsUpgrade);
        row = map;
    }
}
void PetLibrary::useInstalled(const QString &id) {
    if (busy()) return;
    updateInstalled();
    if (!installed_.contains(id)) { status_ = "该模型尚未下载"; emit changed(); return; }
    folder_ = installed_.value(id); currentId_ = id;
    QSettings().setValue("arkpets/currentFolder", folder_);
    status_ = "已切换本地模型"; updateInstalled(); emit changed();
}
QUrl PetLibrary::previewAtlas(const QString &id) const {
    const QUrl folder=installed_.value(id);
    if (folder.isLocalFile()) {
        const QString path=folder.toLocalFile()+"/idle.png";
        return QFileInfo::exists(path)?QUrl::fromLocalFile(path):QUrl();
    }
    if (id==QStringLiteral("002_amiya")) return QUrl(QStringLiteral("qrc:/arkpets/amiya/idle.png"));
    return {};
}
