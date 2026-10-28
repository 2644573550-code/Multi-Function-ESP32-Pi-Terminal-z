#include "TerminalOptions.h"

#include <QCommandLineParser>

namespace TerminalOptions {

void configure(QCommandLineParser &parser) {
    parser.addHelpOption();
    parser.addOptions({{{"c", "config"}, "Product configuration JSON", "path"},
                       {"demo", "Attach simulator module 80"},
                       {"quit-after", "Quit after milliseconds", "ms"},
                       {"screenshot", "Save a window screenshot before quitting", "path"},
                       {"metrics", "Write frame-delivery and process-memory metrics on exit", "path"},
                       {"render-benchmark", "Continuously animate the actual home carousel for rendering measurements"},
                       {"notification-test", "Show a diagnostic notification and test Chinese speech"},
                       {"quick-panel-test", "Open Android-style quick settings panel for screenshot verification"},
                       {"paint-test", "Open the drawing app and report canvas geometry/state"},
                        {"app-drawer-test", "Open the application drawer and report its responsive geometry"},
                        {"keyboard-test", "Open the floating keyboard and verify local Chinese candidates"},
                        {"window-animation-test", "Audit app-card paging and window size animations"},
                       {"notification-layout-test", "Show example app-icon notification cards in the quick panel"},
                       {"notification-banner-test", "Show one app-style notification banner for screenshot verification"},
                       {"window-size", "Initial logical window size, e.g. 800x480", "size"},
                       {"preview-page", "Open a built-in page for target-screen layout testing", "page"},
                       {"open-module", "Open this ready module session in the foreground", "instance"},
                       {"invoke-action", "Invoke an available action after opening the module", "action"},
                       {"ui-audit", "Run responsive layout and injected touch tests", "directory"},
                       {"ui-test", "Exercise dynamic page/actions/disconnect automatically"},
                       // 首次管理员初始化：本地受控、无默认 PIN。注册表非空时拒绝。
                       {"init-admin", "Create the first administrator (local initialisation only); "
                                      "asks for PIN on stdin unless --init-pin-stdin is given", "display-name"},
                       {"init-pin-stdin", "Read the initial administrator PIN from stdin instead of a TTY prompt"}});
}

} // namespace TerminalOptions
