#include "TerminalUi.h"
#include "BlurItem.h"
#include "TextFormatter.h"
#include "PetSpriteItem.h"
#include "PetLibrary.h"
#include "GstVideoItem.h"
#include <QtQml/qqml.h>
#include <mutex>

void initializeTerminalUi() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        qmlRegisterType<BlurItem>("Terminal.Native", 1, 0, "BlurItem");
        qmlRegisterType<TextFormatter>("Terminal.Native", 1, 0, "TextFormatter");
        qmlRegisterType<PetSpriteItem>("Terminal.Native", 1, 0, "PetSpriteItem");
        qmlRegisterType<PetLibrary>("Terminal.Native", 1, 0, "PetLibrary");
        qmlRegisterType<GstVideoItem>("Terminal.Native", 1, 0, "GstVideoItem");
    });
}
