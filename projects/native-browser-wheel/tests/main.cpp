#include <QGuiApplication>
#include <QtQuickTest/quicktest.h>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

int main(int argc, char **argv) {
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    return quick_test_main(argc, argv, "NativeWheel", QUICK_TEST_SOURCE_DIR);
}
