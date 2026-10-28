#include "app/AdminInitializer.h"

#include "security/UserService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

namespace AdminInitializer {

int run(const QCommandLineParser &options) {
    if (!options.isSet("init-admin")) return -1;   // 未请求，调用方继续正常启动

    QTextStream out(stdout);
    QTextStream in(stdin);

    UserService users;
    // 已有任何用户就必须拒绝，且不覆盖 —— 否则删文件即可重建管理员。
    if (users.hasAnyUser()) {
        QTextStream(stderr) << "init-admin: user registry already exists at " << users.path()
                            << "; refusing to create another administrator.\n";
        return 2;
    }

    const QString displayName = options.value("init-admin").trimmed();
    if (displayName.isEmpty()) {
        QTextStream(stderr) << "init-admin: display name required.\n";
        return 2;
    }

    QString pin;
    if (options.isSet("init-pin-stdin")) {
        pin = in.readLine();
    } else {
        // 交互输入不回落显；TTY 不可用时拒绝，不读明文参数（ps 可见）。
        QTextStream(stderr) << "First administrator PIN (at least 6 ASCII digits): " << Qt::flush;
        pin = in.readLine();
        QTextStream(stderr) << "\n";
    }
    pin = pin.trimmed();

    const QString userId = users.createInitialAdmin(displayName, pin);
    if (userId.isEmpty()) {
        QTextStream(stderr) << "init-admin: failed (PIN must be at least 6 ASCII digits).\n";
        return 3;
    }
    out << "created administrator " << displayName << " (" << userId << ")\n";
    return 0;
}

}  // namespace AdminInitializer
