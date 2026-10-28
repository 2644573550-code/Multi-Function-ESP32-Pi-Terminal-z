#pragma once

class QCommandLineParser;

namespace AdminInitializer {
// 处理 --init-admin。未请求时返回 -1，调用方继续正常启动。
// 已初始化过则拒绝（返回 2），不覆盖现有注册表。
int run(const QCommandLineParser &options);
}  // namespace AdminInitializer
