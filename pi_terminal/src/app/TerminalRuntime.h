#pragma once

class QApplication;
class QCommandLineParser;

class TerminalRuntime final {
public:
    TerminalRuntime(QApplication &application, const QCommandLineParser &options);
    int run();

private:
    QApplication &application_;
    const QCommandLineParser &options_;
};
