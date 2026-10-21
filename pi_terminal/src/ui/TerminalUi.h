#pragma once

// Strong reference to the UI library also retains its QML/font/icon resources
// when the executable is linked with --as-needed.
void initializeTerminalUi();
