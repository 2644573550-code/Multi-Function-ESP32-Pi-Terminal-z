# 主程序与内置动态库

本次只调整构建边界、资源归属和原生类型注册，不重写界面、路由或业务接口。

| 产物 | 职责 |
| --- | --- |
| `orange_pi_terminal` | 启动、配置、连接/会话管理、应用生命周期和运行调度 |
| `libterminal_system.so.1` | 系统设置、文件/USB、输入法、语音、桌面集成、拼音词库与 Viper 浏览器服务适配 |
| `libterminal_music.so.1` | 音乐 API、请求隔离和播放事务服务 |
| `libterminal_ui.so.1` | 原生模糊、编辑器格式处理、QML 界面、图标和字体资源 |

这些是启动时由系统动态链接器加载的内置共享库，**不是按页面懒加载或可热卸载插件**。
现有外接模块使用 `QLibrary` + `pi_module_api.h` 的 C ABI，机制保持不变，不是 `QPluginLoader`。内置库接口属于项目内部接口，
升级时应停止程序，将主程序和三个库作为同一版本整体更新，不要在线覆盖正在使用的库。

同为 Release + 测试/截图审计开启的主机构建，主可执行文件由 16,354,200 字节降到
699,880 字节（约减少 95.7%）。资源和功能移到共享库中，并未删除；
这不代表总安装体积或内存占用也减少同样比例。
关闭测试和截图审计后的正式 Release 主程序为 592,280 字节，未链接 Qt Test。

## 构建与运行

```sh
source pi_terminal/scripts/env.sh
cmake -S pi_terminal -B /tmp/opencode/terminal-release \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTERMINAL_REPAIR_AUDIT=OFF
cmake --build /tmp/opencode/terminal-release --parallel 4
/tmp/opencode/terminal-release/orange_pi_terminal --window-size 1024x600
```

构建目录内的库位于 `lib/orange-pi-terminal/`。主程序使用相对 `$ORIGIN` 查找这些库，
不要只复制可执行文件。正式构建关闭测试后不会链接 Qt Test 或编入截图审计逻辑。

## 安装

```sh
cmake --install /tmp/opencode/terminal-release --prefix /path/to/prefix
/path/to/prefix/bin/orange_pi_terminal --window-size 1024x600
```

库安装到 `${CMAKE_INSTALL_LIBDIR}/orange-pi-terminal/`（某些系统为多架构 lib 目录），
相邻库使用 `$ORIGIN` 查找彼此。QML、字体、图标和词库已嵌入相应动态库，不依赖源码目录。
原有音乐 API 的 `libexec` 安装规则继续保留。Qt、Node.js、ffmpeg 和 udisks2 等系统依赖不变。

P0 安全链路（`pbr-secure-v1`）在 Pi 侧需要 mbedtls 开发头文件，用于 ECDSA P-256、X25519、HKDF-SHA256 与 ChaCha20-Poly1305。这些**不是现有依赖**，构建时按需供应：

```sh
apt download libmbedtls-dev          # 免 sudo，解包到仓库外路径
dpkg-deb -x libmbedtls-dev_*.deb /path/to/mbedtls-sdk
cmake ... -DMBEDTLS_ROOT=/path/to/mbedtls-sdk/usr
```

不要把 mbedtls 头文件 vendored 进 `third_party/`：Pi 目标的 ARM64 sysroot 需要同一版本，vendored 会产生两套漂移。系统若已装 `libmbedtls-dev` 可直接用，无需解包。
搬移安装目录时必须搬移整个前缀，不能只搬移 `bin/`。

`tools/runtime_bundle.py` 仅提供构建树依赖闭包复制与组合哈希，不执行服务切换、删除目录或自动归档。
`copy_runtime()` 保留库链接且拒绝覆盖异版目标；`runtime_digest()` 同时覆盖主程序与实际库文件，不能仅比较主程序哈希。
安装树仍应复制整个前缀并保留实际 GNU libdir。用户要求不保留旧版资料，工具的存在不代表默认创建版本备份；测试副本由临时目录管理。

## 回归

- 原生测试直接链接实际运行的共享库，不再重复编译另一份服务源码。
- `shared_runtime_install` 验证安装后搬移目录、共享库解析、QML/字体资源和部署副本。
- `tests/screenshot_regression.py` 继续用于文件管理、编辑器、输入法、媒体和快捷面板。
- 当前验收见 `docs/deployment-20261018.md` 与 `docs/blur-performance.md`：Pi Qt 6.4.2 / VC4 硬件 OpenGL 已有分场景验证。
  真实 USB 挂载、实体触摸与在线 seek 仍需单独验收；最大化整体 FPS 未证明稳定提升。
- `ui_responsive_touch` 和 `ui_highdpi_touch` 曾出现字号/透明度基线问题及隐藏音乐布局循环超时；当前 Arkpets 代码仍需按实际日志核对布局/字体警告。保留完整测试，不以历史结果永久排除或宣称通过。
