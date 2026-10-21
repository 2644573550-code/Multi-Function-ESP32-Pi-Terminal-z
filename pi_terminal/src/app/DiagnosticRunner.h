#pragma once

class QCommandLineParser;
class QObject;
class QQuickWindow;
class SystemTools;

namespace DiagnosticRunner {
void schedule(const QCommandLineParser &options, QObject *root, QQuickWindow *window,
              SystemTools &systemTools);
}
