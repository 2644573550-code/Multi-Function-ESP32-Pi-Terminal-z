# 源码阅读顺序

项目全景图与逐文件完整索引见项目内 `docs/project-code-map.md`，可用 `python3 tools/generate_project_code_map.py` 刷新。

先确认实际 checkout，不能把临时构建目录当成源码。路径不相对 skill 目录：`src/`、`qml/`、`tests/`、`tools/`、`modules/` 和 `music_api/` 均位于项目 `pi_terminal/` 下；`esp/`、`build_scripts/`、`docs/`、`third_party/` 相对项目根目录。

## 终端生命周期和构建

- `pi_terminal/CMakeLists.txt`：Qt 6.4+ 组件、内置共享库、QML/资源归属、测试和安装。
- `src/main.cpp`：OpenGL、Qt WebEngine/Material 初始化、`initializeTerminalUi()`、字体、命令行。
- `src/app/TerminalApplication.{h,cpp}`：系统/连接/音乐/桌面对象及 QML context；销毁顺序保护页面、会话和热点租约。
- `src/app/TerminalRuntime.cpp`、`TerminalOptions.cpp`、`DiagnosticRunner.cpp`：窗口、调度、诊断；`--ui-test` 等待 startupComplete 后操作，REPAIR_PHASE 标记 setup/审计/采集。测试模式不能误触串口/热点。
- `terminal_core`、`terminal_transports`、`terminal_protocol` 目前仍为静态库；不要把所有 `.so` 都理解为插件。
- `src/ui/TerminalUi.cpp`：强引用保留 UI 库，原生类型只注册一次；QML/fonts/icons/QSB 在 UI 库，拼音词库在系统库。

## 通信和插件

- `src/core/ConnectionManager.cpp`：配置、attach、认证、包传输、信任、加载、恢复、关闭、热点租约。
- `src/core/TransportPolicy.h`：模块配置对 transport/package 字段的约束；profile 中其他身份/凭据不被覆盖。
- `src/core/HostEventDetector.h`、`src/core/BridgeConnectionMonitor.h`：物理控制板事件与状态。
- `src/core/SessionStateMachine.cpp`：合法状态迁移；同目录 ModuleCardModel 和 PageProvider 给界面发布状态。
- `src/core/ModuleManager.cpp`、`src/modules/api/pi_module_api.h`：`QLibrary`、C ABI、manifest 一致性与同步停止契约。
- `src/core/PackageReceiver.cpp`、`tools/package_tool.py`：有界数据包传输、Ed25519、哈希、解压、缓存和 ELF 架构验证。
- `src/transport/`：PBR1 串口、BLE、TCP；`src/protocol/`：帧、校验和。
- `modules/example/`、`modules/test_devices/`：主机插件示例；其 `.so` 不在 ESP 上执行。

## 终端功能

- `qml/Main.qml`：应用路由、文件侧栏、便笺、快捷面板、媒体互斥、弹窗和截图审计入口。
- `qml/StartupSplash.qml`：真实初始化阶段、Material 3 启动进度、官方状态图标、超时回退与 Loader 退出；`tests/startup_splash_test.cpp` 覆盖生命周期和图文中心对齐。
- `qml/BlurCache.qml`：双槽、dirty/generation 合并、源捕获完成与有效模糊帧的两阶段发布。
- `qml/CachedBlur.qml` / `qml/shaders/cached-blur.frag`：局部 UV、圆角、透明度合成，无逐卡片模糊 FBO；未就绪使用 Loader 回退。
- `qml/GlassSurface.qml` / `src/ui/BlurItem.{h,cpp}`：局部坐标、原生模糊、尺寸失效、有效帧保留、context shader 复用、仅诊断的 alpha 回读。
- `qml/FloatingKeyboard.qml` / `qml/KeyboardKey.qml`、`src/ui/SystemTools.cpp`：窗口内 IME / 词频与输入提交。
- `src/ui/RemovableStorage.cpp`：sysfs/QStorageInfo、按需挂载、安全 USB 根；`src/ui/SystemTools.cpp`：统一文件操作。
- `src/ui/SystemVolume.{h,cpp}`：用户会话系统主音量/静音、异步 pactl、命令合并与超时；`SystemTools` 维护真实播报生命周期和持久降音开关，两播放器只乘媒体增益、不暂停或恢复旧状态。此功能需区分开发源码和已安装版本。
- `qml/MusicPlayer.qml` / `qml/MusicText.qml`、`src/ui/MusicService.cpp`、`music_api/`：当前音乐路径。
- `tests/music_seek_test.cpp`：生产 QML 的 HTTP MP3 整曲原子缓存/缓存复用/本地 seek 回归（Qt 6.4 GStreamer / host FFmpeg）；覆盖拖动、真实歌词委托、缓存时远程播放连续、零增益本地 decoder prime、目标落点和暂停保持。Pi 默认测试增益为零且不写系统主音量；检测到音频设备不等于人耳确认真实线上歌曲。
- `qml/VideoPlayer.qml`、`qml/PictureViewer.qml`、`qml/FileThumbnail.qml`：视频、图片、预览。
- `qml/EditorToolbar.qml`、`src/ui/TextFormatter.h`：格式、查找替换、剪贴板、序列化。
- `qml/Theme.qml`、`I18n.qml`、`MaterialIcon.qml`：共享设计、文案、图标注册；新增图标需进资源列表。
- `qml/DesktopPet.qml`、`PetManager.qml`、`src/ui/PetSpriteItem.*`、`PetLibrary.*` 与 `tools/arkpets/`：默认关闭的桌宠彩蛋、角色目录/本机转换、角色级已下载/未下载筛选、已下载模型待机图集预览、单节点图集、质量/大小与逐帧接地；细则见 [桌宠参考](desktop-pets.md)。
- `src/ui/ViperBrowserController.*` 与 `third_party/Viper-Browser/`：终端基本触屏浏览器服务；主页默认百度。能力边界及有意移除项见 `docs/viper-browser-port.md`，Material 菜单和图标规则见 `docs/pi-ui-regression.md`；不得将 Qt Widgets 主窗口当作终端内浏览器页。
- `tests/screenshot_regression.py` / `src/app/DiagnosticRunner.cpp`：唯一 UI 场景入口、硬件模糊性能观测与独立过渡帧捕获；脚本区分冷启动预算和场景运行时限，超时保留日志并判失败。不是所有测试脚本都可作为临时文件删除。
- `tests/shared_runtime_install.py` / `tools/runtime_bundle.py`：安装搬移、资源、依赖闭包与复制边界；helper 无服务控制副作用。

## ESP

- `esp/S3_PiBridge_LinkIt_Host/main/`：控制桥 PBR1 UART0、LinkIt、模块配置转发。
- `esp/S3_Pi_Module/main/`：模块编译模式、嵌入包、传感器发布和断开请求。
- `esp/shared/module_data_service/`：BLE/TCP 业务会话、包 API、可选 HTTP/Wi-Fi。
- `esp/shared/pi_sensor_device/`：BME/BMP280 与 BH1750，valid 标记和总线恢复。
- `build_scripts/`：构建包装器、私有配置、BLE/LAN 隔离产物。

`third_party/YesPlayMusic` 仅是算法/交互参考，不构建、不部署。`docs/figures`、原始设计图和许可证是素材，不是测试垃圾。
