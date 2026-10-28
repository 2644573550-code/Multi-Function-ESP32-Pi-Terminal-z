#include "core/ModuleWorkerClient.h"

#include "core/WorkerIpc.h"

#include <QJsonDocument>
#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QtEndian>

namespace security {

ModuleWorkerClient::ModuleWorkerClient(QObject *parent) : QObject(parent) {}

ModuleWorkerClient::~ModuleWorkerClient() { stop({}); }

bool ModuleWorkerClient::start(const QString &session, const QString &libraryPath,
                               const QJsonObject &transport) {
    stop(session);
    errorCode_.clear();
    lastError_.clear();
    degraded_ = false;
    if (session.isEmpty() || libraryPath.isEmpty()) {
        errorCode_ = QStringLiteral("INVALID_MESSAGE");
        lastError_ = QStringLiteral("session and library path are required");
        return false;
    }

    // 先探测 systemd 是否真的能管这个 unit：只有它能提供 DynamicUser 等隔离。
    // 不用 `systemctl start --wait` 试探 —— 那会在没有 unit 时挂满 90 秒。
    const QString unit = QStringLiteral("orange-pi-worker@%1.service").arg(
        QString(session).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")),
                                 QStringLiteral("_")));
    QProcess probe;
    probe.start(QStringLiteral("systemctl"),
                {QStringLiteral("cat"), unit});
    const bool systemdManaged = probe.waitForFinished(1500) && probe.exitCode() == 0;
    if (systemdManaged) {
        // unit 存在：交给 systemd 起进程，它负责 UID 与网络隔离。
        if (QProcess::startDetached(QStringLiteral("systemctl"),
                                    {QStringLiteral("start"), unit})) {
            errorCode_.clear();
            degraded_ = false;
            return true;
        }
    }

    // 降级：本机或未安装 unit 时直接起 worker 二进制。
    // 明确标记 degraded —— 这种情形不构成隔离证据（§0.4）。
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    // 查找顺序：显式环境变量 → 安装路径 → 与宿主同目录（测试/开发构建）。
    // 不把安装路径写死：本机与测试环境没有 /usr/libexec 部署。
    QString workerBinary = qEnvironmentVariable("ORANGE_PI_MODULE_WORKER");
    if (workerBinary.isEmpty()) {
        const QString installed = QStringLiteral("/usr/libexec/orange-pi-terminal/module_worker");
        const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/module_worker");
        workerBinary = QFileInfo::exists(installed) ? installed : sibling;
    }
    process_->start(workerBinary, {});
    if (!process_->waitForStarted(1500)) {
        lastError_ = process_->errorString();
        errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
        delete process_;
        process_ = nullptr;
        return false;
    }
    degraded_ = true;
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        emit workerExited(code);
    });

    // 第一条请求就带着库路径与 transport，由 worker 自己加载。
    QJsonObject response;
    const QJsonObject body{{"library", libraryPath}, {"transport", transport}};
    const bool loaded = request(QStringLiteral("publishSample"), body, &response)
        && response.value("code").toString() == QLatin1String("OK");
    if (!loaded) {
        // 加载失败必须收掉 worker，否则留下孤儿进程。
        if (response.contains("code"))
            errorCode_ = response.value("code").toString();
        if (errorCode_.isEmpty()) errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
        stop({});
        return false;
    }
    return true;
}

void ModuleWorkerClient::stop(const QString &session) {
    if (!process_) return;
    QJsonObject response;
    request(QStringLiteral("stop"), {}, &response);
    process_->closeWriteChannel();
    if (!process_->waitForFinished(1000)) process_->kill();
    delete process_;
    process_ = nullptr;
    buffer_.clear();
    Q_UNUSED(session)
}

bool ModuleWorkerClient::isRunning() const {
    return process_ && process_->state() == QProcess::Running;
}

bool ModuleWorkerClient::sendData(const QByteArray &payload) {
    if (!isRunning()) return false;
    // 512 上限在这里再兜一层；调用方 LaneGate 已判过。
    if (payload.size() > 512) {
        errorCode_ = QStringLiteral("INVALID_MESSAGE");
        return false;
    }
    QJsonObject response;
    const QJsonObject body{{"payload", QString::fromLatin1(payload.toBase64())}};
    return request(QStringLiteral("readOwnRecords"), body, &response)
        && response.value("code").toString() == QLatin1String("OK");
}

bool ModuleWorkerClient::request(const QString &op, const QJsonObject &body, QJsonObject *response) {
    if (!isRunning()) {
        errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
        return false;
    }
    const quint64 id = nextRequestId_++;
    const QByteArray frame = WorkerIpc::encode(WorkerIpc::request(id, op, body));
    process_->write(frame);
    if (!process_->waitForBytesWritten(WorkerIpc::kTimeoutMs)) {
        errorCode_ = QStringLiteral("TIMEOUT");
        return false;
    }
    // 同步等响应：P0 的请求都是短操作，不需要并发。
    // 循环读直到拿到匹配的 response。
    // worker 可能先发若干 reportState 事件（模块主动 emit），
    // 只等一次 waitForReadyRead 会把它们当成超时。
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < WorkerIpc::kTimeoutMs) {
        while (process_->bytesAvailable() > 0) buffer_.append(process_->readAllStandardOutput());
        while (buffer_.size() >= 4) {
            const quint32 declared = quint32(quint8(buffer_[0])) | (quint32(quint8(buffer_[1])) << 8)
                | (quint32(quint8(buffer_[2])) << 16) | (quint32(quint8(buffer_[3])) << 24);
            if (declared > quint32(WorkerIpc::kMaxMessageBytes)) {
                // 超长立即断开，按 §5 的要求。
                errorCode_ = QStringLiteral("INVALID_MESSAGE");
                stop({});
                return false;
            }
            if (quint32(buffer_.size()) < declared + 4) break;
            const QByteArray frameBytes = buffer_.left(int(declared) + 4);
            buffer_.remove(0, int(declared) + 4);
            const QJsonObject message = WorkerIpc::decode(frameBytes);
            if (message.isEmpty()) continue;
            if (message.value("op").toString() == QLatin1String("reportState")) {
                const QJsonObject payload = message.value("body").toObject();
                emit eventReceived(payload.value("type").toString(), payload.value("json").toString());
                continue;
            }
            if (static_cast<quint64>(message.value("request_id").toDouble()) == id) {
                if (response) *response = message;
                return true;
            }
        }
        if (process_->waitForReadyRead(qMax<qint64>(50, WorkerIpc::kTimeoutMs - timer.elapsed())))
            continue;
        if (process_->state() != QProcess::Running) break;
    }
    errorCode_ = QStringLiteral("TIMEOUT");
    return false;
}

}  // namespace security
