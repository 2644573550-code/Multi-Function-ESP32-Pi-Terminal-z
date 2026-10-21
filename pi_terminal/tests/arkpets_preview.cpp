#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include "ui/TerminalUi.h"

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Material");
    initializeTerminalUi();
    QQmlApplicationEngine engine;
    engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
