#include "core/ModuleWorkerClient.h"
#include "core/WorkerIpc.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

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

    session_ = session;
    const QString unit = WorkerIpc::unitName(session);
    const QString socketPath = WorkerIpc::socketPath(session);

    // 先探测 systemd 能否管这个 unit：只有它能提供 DynamicUser 等隔离。
    QProcess probe;
    probe.start(QStringLiteral("systemctl"), {QStringLiteral("cat"), unit});
    const bool systemdManaged = probe.waitForFinished(1500) && probe.exitCode() == 0;

    if (systemdManaged) {
        // unit 存在：先清掉上一次运行留下的 socket，再交给 systemd 起进程。
        // socket 由 worker 自己创建，宿主不代为创建（否则属主是宿主，越权）。
        QFile::remove(socketPath);
        QProcess starter;
        starter.start(QStringLiteral("systemctl"), {QStringLiteral("start"), unit});
        if (!starter.waitForFinished(WorkerIpc::kTimeoutMs) || starter.exitCode() != 0) {
            errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
            lastError_ = QString::fromUtf8(starter.readAllStandardError()).trimmed();
            return false;
        }
        degraded_ = false;
    } else {
        // 降级：本机无 systemd unit 时直接起 worker。明确标记 degraded，
        // 这种情形不构成隔离证据（§0.4）。
        workerProcess_ = new QProcess(this);
        workerProcess_->setProcessChannelMode(QProcess::SeparateChannels);
        QString workerBinary = qEnvironmentVariable("ORANGE_PI_MODULE_WORKER");
        if (workerBinary.isEmpty()) {
            const QString installed = QStringLiteral("/usr/libexec/orange-pi-terminal/module_worker");
            const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/module_worker");
            workerBinary = QFileInfo::exists(installed) ? installed : sibling;
        }
        // 降级模式下 worker 也走 socket；用降级 socket 名避免和 systemd 单元抢路径。
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("ORANGE_PI_WORKER_SOCKET"), socketPath);
        workerProcess_->setProcessEnvironment(env);
        workerProcess_->start(workerBinary, {});
        if (!workerProcess_->waitForStarted(1500)) {
            lastError_ = workerProcess_->errorString();
            errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
            delete workerProcess_;
            workerProcess_ = nullptr;
            return false;
        }
        degraded_ = true;
        connect(workerProcess_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            emit workerExited(code);
        });
    }

    // 连 socket。worker 启动到 listen 之间有窗口期，重试到超时。
    socket_ = new QLocalSocket(this);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < WorkerIpc::kTimeoutMs) {
        socket_->connectToServer(socketPath);
        if (socket_->waitForConnected(200)) break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    if (socket_->state() != QLocalSocket::ConnectedState) {
        errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
        lastError_ = socket_->errorString();
        delete socket_;
        socket_ = nullptr;
        return false;
    }
    connect(socket_, &QLocalSocket::readyRead, this, [this] {
        buffer_.append(socket_->readAll());
        // 只缓冲；真正的解析在 request() 的同步循环里做。
    });

    // 第一条请求带库路径与 transport，由 worker 自己加载并 attach。
    QJsonObject body{{"library", libraryPath}, {"transport", transport}};
    QJsonObject response;
    if (!request(QStringLiteral("publishSample"), body, &response)) {
        if (errorCode_.isEmpty()) errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
        // 启动失败必须把连接与进程一起收掉：否则 isRunning() 仍然为真，
        // 调用方会以为 worker 可用，后续发送只会拿到 TIMEOUT。
        stop(session);
        return false;
    }
    const QString code = response.value("code").toString();
    if (code != QLatin1String("OK")) {
        // worker 侧把库加载失败与 attach 拒绝分成了不同错误码，原样上抛，
        // 便于界面区分"部署问题"与"模块拒绝"。
        errorCode_ = code;
        lastError_ = QStringLiteral("worker refused: ") + code;
        stop(session);
        return false;
    }
    return true;
}

void ModuleWorkerClient::stop(const QString &session) {
    if (socket_) {
        QJsonObject response;
        request(QStringLiteral("stop"), {}, &response);
        socket_->disconnectFromServer();
        if (socket_->state() != QLocalSocket::UnconnectedState) socket_->waitForDisconnected(500);
        delete socket_;
        socket_ = nullptr;
    }
    if (workerProcess_) {
        workerProcess_->closeWriteChannel();
        if (!workerProcess_->waitForFinished(1000)) workerProcess_->kill();
        delete workerProcess_;
        workerProcess_ = nullptr;
    }
    if (!session_.isEmpty()) {
        QProcess stopper;
        stopper.start(QStringLiteral("systemctl"),
                      {QStringLiteral("stop"), WorkerIpc::unitName(session_)});
        stopper.waitForFinished(WorkerIpc::kTimeoutMs);
    }
    buffer_.clear();
    session_.clear();
    Q_UNUSED(session)
}

bool ModuleWorkerClient::isRunning() const {
    return socket_ && socket_->state() == QLocalSocket::ConnectedState;
}

bool ModuleWorkerClient::sendData(const QByteArray &payload) {
    if (!isRunning()) return false;
    if (payload.size() > LaneGate::kMaxBusinessPayload) {
        errorCode_ = QStringLiteral("INVALID_MESSAGE");
        return false;
    }
    // 业务数据的 op 是 publishSample（把数据交给模块），不是 readOwnRecords。
    const QJsonObject body{{"payload", QString::fromLatin1(payload.toBase64())}};
    QJsonObject response;
    return request(QStringLiteral("publishSample"), body, &response)
        && response.value("code").toString() == QLatin1String("OK");
}

bool ModuleWorkerClient::request(const QString &op, const QJsonObject &body, QJsonObject *response) {
    if (!isRunning() || response == nullptr) {
        errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
        return false;
    }
    const quint64 id = nextRequestId_++;
    const QByteArray frame = WorkerIpc::encode(WorkerIpc::request(id, op, body));
    socket_->write(frame);
    if (!socket_->waitForBytesWritten(WorkerIpc::kTimeoutMs)) {
        errorCode_ = QStringLiteral("TIMEOUT");
        return false;
    }

    // 同步等响应：P0 的请求都是短操作，不需要并发。
    // worker 可能先发若干 reportState 事件，只等一次会被当成超时。
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < WorkerIpc::kTimeoutMs) {
        while (socket_->bytesAvailable() > 0) buffer_.append(socket_->readAll());
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
            if (static_cast<quint64>(message.value("request_id").toDouble()) != id) continue;
            *response = message;
            return true;
        }
        if (!socket_->waitForReadyRead(qMax<qint64>(50, WorkerIpc::kTimeoutMs - timer.elapsed()))) {
            if (socket_->state() != QLocalSocket::ConnectedState) {
                errorCode_ = QStringLiteral("WORKER_UNAVAILABLE");
                return false;
            }
        }
    }
    errorCode_ = QStringLiteral("TIMEOUT");
    return false;
}

}  // namespace security
