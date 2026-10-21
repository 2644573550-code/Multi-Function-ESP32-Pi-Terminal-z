#include "RemovableStorage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QSet>

static QString readStorageText(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString{};
}

RemovableStorage::RemovableStorage(QObject *parent, const QString &sysRoot,
                                 std::function<QVariantList()> volumes)
    : QObject(parent), sysRoot_(sysRoot), volumes_(std::move(volumes)) {
    refresh(); // Existing devices belong in the sidebar, without startup popups.
    timer_.setInterval(1500);
    connect(&timer_, &QTimer::timeout, this, &RemovableStorage::refresh);
    timer_.start();
}

void RemovableStorage::refresh() {
    QVariantList mounts;
    if (volumes_) mounts = volumes_();
    else for (const auto &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady()) continue;
        mounts.append(QVariantMap{{"device", QString::fromLocal8Bit(volume.device())},
                                 {"path", volume.rootPath()}, {"name", volume.displayName()}});
    }
    QVariantList next;
    const QDir sys(sysRoot_);
    const auto names = sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &id : names) {
        const auto node = sys.filePath(id);
        const bool partition = QFileInfo::exists(node + "/partition");
        const auto physical = partition ? QFileInfo(QFileInfo(node).canonicalFilePath()).dir().dirName() : id;
        const auto physicalNode = sys.filePath(physical);
        if (!QFileInfo(node).canonicalFilePath().contains("/usb") &&
            readStorageText(physicalNode + "/removable") != "1") continue;
        if (!partition) {
            bool hasPartitions = false;
            for (const auto &other : names)
                if (QFileInfo(QFileInfo(sys.filePath(other)).canonicalFilePath()).dir().dirName() == id &&
                    QFileInfo::exists(sys.filePath(other) + "/partition")) { hasPartitions = true; break; }
            if (hasPartitions) continue;
        }
        const QString device = "/dev/" + id;
        QString path, label;
        bool systemVolume = false;
        for (const auto &mount : mounts) {
            const auto map = mount.toMap();
            if (map.value("device").toString() != device &&
                (QFileInfo(device).canonicalFilePath().isEmpty() ||
                 QFileInfo(map.value("device").toString()).canonicalFilePath() != QFileInfo(device).canonicalFilePath())) continue;
            const auto root = QFileInfo(map.value("path").toString()).canonicalFilePath();
            if (root == "/" || root == "/boot" || root == "/boot/firmware") {systemVolume = true; break;}
            if (root.isEmpty()) continue;
            path = root; label = map.value("name").toString(); break;
        }
        if (systemVolume) continue;
        if (label.isEmpty() || label.startsWith('/')) label = readStorageText(physicalNode + "/device/model");
        if (label.isEmpty()) label = "USB " + id;
        next.append(QVariantMap{{"id", id}, {"physical", physical}, {"device", device}, {"name", label},
                                {"path", path}, {"mounted", !path.isEmpty()}, {"location", "@usb/" + id}});
    }
    QSet<QString> oldPhysical;
    for (const auto &entry : devices_) oldPhysical.insert(entry.toMap().value("physical").toString());
    const bool changedList = next != devices_;
    devices_ = next;
    if (changedList) emit changed();
    if (initialized_) for (const auto &entry : next) {
        const auto map = entry.toMap(); const auto physical = map.value("physical").toString();
        if (!oldPhysical.contains(physical)) {oldPhysical.insert(physical); emit inserted(map.value("name").toString());}
    }
    initialized_ = true;
}

QString RemovableStorage::resolve(const QString &location, bool creating, bool allowRoot) const {
    const auto parts = location.split('/');
    if (parts.size() < 2 || parts[0] != "@usb" || parts.contains("..")) return {};
    for (const auto &entry : devices_) {
        const auto map = entry.toMap();
        if (map.value("id").toString() != parts[1] || !map.value("mounted").toBool()) continue;
        const auto root = map.value("path").toString();
        if (QFileInfo(root).canonicalFilePath() != root) return {};
        if (!volumes_) { // Do not fall back to an empty mount directory after unplugging.
            QStorageInfo volume(root);
            if (!volume.isValid() || !volume.isReady() || volume.rootPath() != root ||
                QFileInfo(QString::fromLocal8Bit(volume.device())).canonicalFilePath() !=
                QFileInfo(map.value("device").toString()).canonicalFilePath()) return {};
        }
        const QFileInfo info(root + '/' + parts.mid(2).join('/'));
        if (info.isSymLink()) return {};
        const auto path = creating && !info.exists()
            ? QFileInfo(info.absolutePath()).canonicalFilePath() + '/' + info.fileName() : info.canonicalFilePath();
        return (allowRoot && path == root) || path.startsWith(root + '/') ? path : QString{};
    }
    return {};
}

void RemovableStorage::open(const QString &id) {
    refresh();
    for (const auto &entry : devices_) {
        const auto map = entry.toMap();
        if (map.value("id").toString() != id) continue;
        if (map.value("mounted").toBool()) {emit ready(map.value("location").toString()); return;}
        if (mounting_) return;
        const auto program = QStandardPaths::findExecutable("udisksctl");
        if (program.isEmpty()) {emit error("USB 设备尚未挂载，请安装 udisks2 或先通过系统挂载。"); return;}
        mounting_ = true;
        auto *process = new QProcess(this);
        auto *timeout = new QTimer(process); timeout->setSingleShot(true); timeout->setInterval(15000);
        connect(timeout, &QTimer::timeout, process, &QProcess::kill);
        const auto location = map.value("location").toString();
        connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process, location](int code, QProcess::ExitStatus status) {
            mounting_ = false; refresh();
            if (status == QProcess::NormalExit && code == 0 && !resolve(location, false, true).isEmpty()) emit ready(location);
            else emit error("USB 挂载失败，请检查设备、文件系统或系统挂载权限。");
            process->deleteLater();
        });
        connect(process, &QProcess::errorOccurred, this, [this,process](QProcess::ProcessError errorCode) {
            if (errorCode == QProcess::FailedToStart) {mounting_=false;emit error("无法启动 USB 挂载服务。");process->deleteLater();}
        });
        process->start(program, {"mount", "--no-user-interaction", "--block-device", map.value("device").toString()});
        timeout->start(); return;
    }
    emit error("USB 设备已移除。");
}
