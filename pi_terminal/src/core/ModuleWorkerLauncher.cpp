#include "core/ModuleWorkerLauncher.h"

#include "core/WorkerIpc.h"

#include <QProcess>

ModuleWorkerLauncher::ModuleWorkerLauncher(QObject *parent) : QObject(parent) {}

QString ModuleWorkerLauncher::unitName(const QString &session) {
    // session 是内部标识，仍需过滤成 systemd 允许的单元名字符集。
    QString safe;
    for (const QChar c : session) {
        safe.append(c.isLetterOrNumber() ? c : QLatin1Char('_'));
    }
    return QStringLiteral("orange-pi-worker@%1.service").arg(safe);
}

QStringList ModuleWorkerLauncher::startCommand(const QString &session) {
    // 必须走 systemctl：直接 QProcess 起二进制会绕过 unit 的全部沙箱声明。
    return {QStringLiteral("systemctl"), QStringLiteral("start"), unitName(session)};
}

bool ModuleWorkerLauncher::start(const QString &session, const QString &deviceId) {
    if (session.isEmpty() || deviceId.isEmpty()) {
        lastError_ = QStringLiteral("session and deviceId are required");
        return false;
    }
    QProcess process;
    const QStringList command = startCommand(session);
    process.start(command.first(), command.mid(1));
    if (!process.waitForStarted(WorkerIpc::kTimeoutMs)) {
        lastError_ = QStringLiteral("systemctl not available (worker isolation requires systemd)");
        return false;
    }
    if (!process.waitForFinished(WorkerIpc::kTimeoutMs)) {
        process.kill();
        lastError_ = QStringLiteral("systemctl start timed out");
        return false;
    }
    if (process.exitCode() != 0) {
        lastError_ = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

void ModuleWorkerLauncher::stop(const QString &session) {
    QProcess process;
    process.start(QStringLiteral("systemctl"),
                  {QStringLiteral("stop"), unitName(session)});
    process.waitForFinished(WorkerIpc::kTimeoutMs);
}

bool ModuleWorkerLauncher::isRunning(const QString &session) const {
    QProcess process;
    process.start(QStringLiteral("systemctl"),
                  {QStringLiteral("is-active"), unitName(session)});
    if (!process.waitForFinished(WorkerIpc::kTimeoutMs)) return false;
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed() == QLatin1String("active");
}
