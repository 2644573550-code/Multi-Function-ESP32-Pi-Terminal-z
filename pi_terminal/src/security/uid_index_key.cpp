#include "security/uid_index_key.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

namespace security {


namespace {

constexpr int kKeyBytes = 32;

QString keyPath() {
    // AppConfigLocation，不是 AppLocalDataLocation（后者是模块可读目录）。
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/uid_index.key");
}

bool readKey(const QString &path, QByteArray *out) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray raw = file.read(kKeyBytes + 1);
    file.close();
    // 长度必须精确；截断或超长都视为损坏，不猜测修复。
    if (raw.size() != kKeyBytes) return false;
    *out = raw;
    return true;
}

bool writeKey(const QString &path, const QByteArray &key) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return false;
    if (file.write(key) != key.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) return false;
    // 先落盘再收紧权限：commit 在某些文件系统上会重置权限。
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return QFileInfo(path).size() == kKeyBytes;
}

bool newKey(QByteArray *out) {
    out->resize(kKeyBytes);
    for (int i = 0; i < kKeyBytes; ++i) {
        (*out)[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return true;
}

}  // namespace

bool uidIndexKey(QByteArray *out32) {
    if (!out32) return false;
    // 缺失时失败关闭：调用方据此返回空，不得退回明文 UID 比较。
    return readKey(keyPath(), out32);
}

bool ensureUidIndexKey() {
    QByteArray existing;
    if (readKey(keyPath(), &existing)) return true;
    QByteArray key;
    if (!newKey(&key)) return false;
    return writeKey(keyPath(), key);
}

bool rotateUidIndexKey() {
    QByteArray key;
    if (!newKey(&key)) return false;
    return writeKey(keyPath(), key);
}

}  // namespace security
