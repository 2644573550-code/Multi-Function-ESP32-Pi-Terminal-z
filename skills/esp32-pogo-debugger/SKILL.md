---
name: ESP32 Pogo Debugger
description: 维护 Multi-Function-ESP32-Debugger 的 ESP32-S3 固件、PBR1/BLE/LAN 协议、原生 Qt/QML 终端、启动与 UI 审计、桌宠彩蛋和模型转换、模糊预缓存、系统音量、HTTP 音乐跳转、媒体、USB、输入法、项目清理、回归测试和安全部署时使用。
---

# ESP32 Pogo Debugger：项目维护工作流

更新：2026-10-22。以现场源码和设备状态为准，不保存凭据、逐轮日志、临时截图或旧版资料副本。

## 任务开始

1. 核对仓库、Git 状态、用户改动与涉及路径；不把历史 untracked 状态或旧构建目录当成当前事实。
2. 先读 [源码地图](references/source-map.md)，再按任务读取参考：
   - [固件、协议与插件 ABI](references/firmware.md)
   - [原生界面、模糊、媒体、文件与输入法](references/native-ui.md)
   - [构建、验证与部署](references/build-test.md)
   - [清理与知识维护](references/cleanup.md)
   - [桌宠彩蛋与模型转换](references/desktop-pets.md)
   - [设备候选索引](references/devices.md)（不代表在线）
3. 涉及模糊必须同时阅读项目 `docs/blur-performance.md` 和实际 `BlurCache.qml`、`CachedBlur.qml`、`BlurItem.cpp`，不能照搬旧实时采样方案。
4. 涉及启动、语言切换或测试超时，核 `StartupSplash.qml`、`TerminalRuntime.cpp`、`DiagnosticRunner.cpp`、`screenshot_regression.py` 与 `docs/pi-ui-regression.md`；先区分冷启动、实际操作、审计和图像解码耗时。

## 操作边界

- 未获当前任务授权，不刷 ESP、不停止/重启 Pi 服务、不切换版本。清理资料不构成这些操作的授权。
- 写 Flash 前核 USB by-id、芯片、容量、分区及备份。私钥、token、Wi-Fi 密码、私有 sdkconfig、Flash dump 不写入 skill/公共报告。
- 操作 Pi 前现场核 systemd ActiveState/MainPID/NRestarts/ExecStart、真实 `/proc/<PID>/exe`、动态库映射和相关 ready 日志；旧会话状态不延续。
- 同步使用明确路径、依赖闭包与 SHA256，不用宽泛 `rsync --delete`，不覆盖私有配置。
- 用户要求不留旧版资料：确认过时且脱离依赖的文档、一次性脚本、源码备份、截图和重复交接资料直接清理，不再另建归档。保留当前源码、有效固件、许可证、密钥、配置、运行数据及服务实际使用的文件。
- 清理截图先分类，不按扩展名批量删；保留原始设计图、`docs/figures`、第三方随包静态图片及长期回归素材。保留 `screenshot_regression.py`、`music_seek_test.cpp` 等长期测试源码，只清理可确认的临时截图、`.pyc` 和任务暂存物。
- 不凭文件名、`build` 目录或 untracked 判定垃圾；不批量 git add/commit，不回退/重置用户改动。长期测试及必要素材不是一次性测试产物。

## 当前架构与产品约束

- 主程序负责启动、连接/会话及调度；`terminal_system`、`terminal_music`、`terminal_ui` 为启动时动态链接的内置库，不是懒加载/热卸载插件。主程序和三个库同版发布，保留相对 RPATH、SONAME 链接与嵌入资源。
- 外接模块由 `QLibrary` 加载 `pi_module_api.h` 的 C ABI；`IModulePlugin` 仅为 C++ 辅助接口。不要改成 QPluginLoader，也不要混同内置库。
- 模块上报 `module-transport-v1`，固定 BLE-only 或 LAN-only；PBR1/LinkIt 仅控制面，业务走认证 BLE/TCP，不跨模式回退。BLE 下载可临时租用热点，业务仍留 BLE。
- 身份、token、协议、序号和 `session_ready` 都通过才算 ready；串口/TCP 打开不等于成功。关闭先停业务、释放 QML/插件，再释放传输/热点；最小化保留会话。
- 保留现有原生 Qt/QML 布局，重点适配 1024×600、Material Design 和 Pi 性能。主要触控热区至少 48px，不能机械套用全部 64dp。工具菜单使用唯一 submenu 入口、主题容器色与统一圆角，避免手动触发项和 Qt 自动 submenu 项重复；官方图标登记原始 URL/SHA256，新增 glyph 需更新 manifest、双主题资源与 CMake GLOB。系统文案走 I18n，用户内容不翻译。
- 当前维护的 Pi 终端源码应现场核对工作目录；桌宠彩蛋默认关闭，五击“显示与个性化”标题弹出启动/关闭与管理两项。关闭时 Loader 卸载人物，管理不自动启动。桌宠管理筛选按“角色已下载/未下载”（角色只要一个模型已安装即为已下载），不要把模型列表筛选混称角色筛选；模型列表保留分类/搜索与下载状态，已下载条目可看待机图集预览。鼠标链路须以完整设置页验证，不用独立控件结果替代。
- 桌宠失败方案、转换限制及未解决项见 [桌宠参考](references/desktop-pets.md)。管理器含已下载/未下载筛选与已下载模型待机图集预览；预览不是实时模型渲染。质量 v4、可见轮廓归一化、逐帧接地以当前 `pi_terminal` 实现为准；诊断固定等待可能早于弹窗完成打开，不当作功能结论。
- Viper 浏览器移植范围见项目 `docs/viper-browser-port.md`：当前产品目标为基本触屏浏览；默认主页 Baidu；用户主页保留。终端明确不提供自定义 User-Agent、书签 Netscape HTML 导入导出、用户脚本管理、Viper WebChannel、DOM 源码查看、网页查找或凭据管理；运行 profile 中也强制关闭自定义 UA/用户脚本，不安装 Viper 全局网页脚本。保留基础导航、标签、书签/历史、下载、PDF、过滤订阅、Cookie 与权限提示。按项目文档描述部分能力及验证边界，不把部分适配写作完整 Viper 功能。桌面 QWidget 窗口不嵌入终端，未经授权不部署 Pi。
- 启动层为 Main.qml Overlay 内可卸载的 StartupSplash，不另建 WebView/Window。进度只反映偏好、壁纸、首帧及真实双缓存就绪；首帧后最短展示 1.1 秒，6 秒缓存超时明确提示兼容渲染，不伪报模块/API 就绪。设置分组为“性能”，副标题只保留“节能渲染”。
- 状态图标使用 Google 官方 Material SVG（check_circle/pending），不再使用 Unicode 勾号/圆点；24dp 图标槽与文字垂直中心对齐，保留来源 URL、原始 SHA256、许可证及黑白资源。MD 规范需对照官方资料和实际截图，不只凭暗色/圆角自称符合。
- 音乐为 MusicService/Qt Network/第三方 Node API + Qt Multimedia；不恢复 CLI/mpv、Electron/WebView 为当前播放路径。MusicText 保持局部暗色白/灰文字，Qt 6 QML seek 写毫秒 `position`，不恢复已移除的播放模式按钮。
- 音乐跳转统一 preview/request/commit/cancel：拖动只预览、松手提交一次；歌词/键盘以 100ms 合并。YesPlayMusic 实际是整首音源进 IndexedDB、播放改 Blob，并预取下一整首；它不是按目标时间做 range-segment cache，seek 本身交给 Howler。Qt 在线首次 seek 应先把当前完整音源原子缓存到本地，等待期间保持原远程流，不发 HTTP seek；缓存完成切本地、零增益预热、暂停本地解码器定位后恢复原播放状态。命中本地缓存后也走本地定位；离线文件仍原位 seek。LRU 有上限，缓存失败留在原流原位置，切歌取消未完成缓存。不得把 pause/play 用在远程源上或循环改 playbackRate。
- 音乐 API 不是独立开机服务：首次在线请求按需探测/拉起 Node；本地播放不需要，远端地址不拉本机，退出只停止自己启动的子进程。终端自启不等于 API 已就绪，不重复添加 systemd 音乐服务。
- 快捷面板的控制和下方状态网格均固定两列、保持卡片等宽：热点、蓝牙在上，系统电源、音量在下，音量在电源右侧。点击音量展开系统主音量/静音；新增滑块与按钮在实际桌面缩放后仍至少 48px，不能只用 48×uiScale 当最小值。保留 Google 官方音量 SVG、现有 Material 与共享模糊缓存。
- 系统主音量由异步 pactl 控制默认输出（PulseAudio / PipeWire-Pulse）；“播报时降低媒体音量”默认开启、持久保存。真实 Qt TTS / espeak 生命周期只让音乐/视频输出乘 0.2，结束/取消/失败恢复；不改变播报本身、系统音量、用户暂停/静音或期间更新的媒体音量。只协调终端自身播报，不拦截其他应用音效。
- USB 经 RemovableStorage 和受限命名空间访问，按需 udisksctl、不用 sudo；图片/媒体内部打开。输入法为宿主 Overlay 内 Popup，保持拖动边界，不能另建 Window。编辑器区分纯文本/HTML，覆盖需确认。

## 渲染维护要点

- BlurItem 是唯一模糊算法；BlurCache 双槽预渲染，CachedBlur/QSB 只做局部裁切与圆角合成，不逐卡片建立 FBO。
- 桌面首帧后，先等源 `scheduledUpdateCompleted`，再等匹配 revision 的 `frameReady`；生成期间保留有效旧槽，合并失效请求。不能凭“已有 texture id”发布。
- 每个控件裁切自身位置，不能缩整张壁纸进卡片。显式跟踪布局/Scale/滚动依赖，避免 mapToItem 绑定残留旧坐标或临时 1×1 采样闪变。
- desktopBackground 覆盖完整窗口而非扣除 footer 的内容区（height: win.height）；Overlay 面板需完整缓存。底部缺口修源范围，不用 UV clamp 拉条遮掩；普通/节能 quick-glass 同时核采样矩形在源内，按实际窗口尺寸检查。
- 快捷面板静态缓存，展开按最终落点更新 UV；不加位置轮询、每帧捕获或打开后延迟模糊。壁纸、尺寸、能耗或场景图失效才重建缓存，不重建页面/主页模型。
- 缩小 FBO 时由 BlurItem 自行管理尺寸失效，不能恢复 Qt 默认尺寸比较导致的反复分配。默认壁纸需验证真实缓存 alpha，不能把透明缓存透出 UI 当成模糊有效。
- 节能 2 级/0.25、普通 4 级/0.5 均保留模糊。零 FBO 重建不等于整体 FPS 稳定提升；测量边界见性能文档。

## 验证与知识更新

- ARM64 在 PC 使用匹配 Pi Qt 的 sysroot/工具链交叉编译；Pi 只实测。正式 Release 关闭 BUILD_TESTING 与 TERMINAL_REPAIR_AUDIT；QSB 需匹配的主机 ShaderTools，运行不需要开发工具。
- UI 截图/场景长期入口仅 `pi_terminal/tests/screenshot_regression.py`；音频跳转/时钟用 `music_http_seek_clock` 原生测试，优先扩展长期入口，不为每次 Bug 累积新脚本。测试隔离 HOME/XDG、产品串口/热点及诊断环境变量。
- seek/cache 发布可让 Qt Test runner 加载待安装的正式三个库，通过 HTTP Range fixture、实际 QML、完整音频文件缓存、QSaveFile 完成后 source 切换、本地解码器预热/目标 seek/恢复播放，并检查缓存复用、播放态和约 1× 时钟；Pi 输出增益设零避免打断当前用户音乐时仍可检查非空 QAudioDevice。测试通过后先检查活动 sink-input，服务正在播音时不可未经用户许可重启；停服后整体安装正式主程序和三库。候选库哈希须与测试 runner `/proc/<PID>/maps` 一致。缓存/时钟自动测试不是实际扬声器听感确认。
- Pi 验证使用真实 Xorg/硬件 OpenGL；offscreen/host-test 只验证布局逻辑。回归按原服务 active/inactive 状态恢复；本次只清理时不得运行会控服务的 Pi 回归。
- 首帧启动图使用异步 grabToImage；不得在 frameSwapped 回调同步 grabWindow 重入渲染。队列回调加单次保护，disconnect 不会撤销已经排队的调用。启动回归必须同时检查首帧显示/焦点与最终卸载，不把采集阻塞误报为产品启动失败。
- `--ui-test` 必须等待 `startupComplete` 再点击主页；启动遮罩拦截早期输入是正确行为，不能禁用遮罩或删除触控/会话断言来修测试。
- `--quit-after` 在同步 Qt/QML 初始化后才设定，不等于进程总生存时间。当前截图脚本单独给予冷启动 120 秒预算，再加场景运行时间；超时仍失败并保存 stdout/stderr。用 `REPAIR_PHASE` 定位 setup、snapshot、image，不能凭总超时就断言语言切换死锁，也不能把 1.1 秒最短动画时间当成总启动承诺。
- 运行中英文切换 `language-en-4` 与预选英文启动是不同验收，不能互相替代。以当前结构化报告及对应构建为准；已闭环的旧超时不反复列为当前未通过项，也不把旧成功延伸为未来构建全绿或物理触摸验收。
- 保留完整库安装/搬移测试。`tools/runtime_bundle.py` 只是无服务副作用的构建树复制/哈希工具，不是自动部署器，也不默认创建旧版备份。
- 当前已有 Pi 分场景、模糊过渡及受控 HTTP seek 时钟验证；最大化整体 FPS 仍波动。真实 USB 挂载、物理触摸耐久、特定线上音源听感与模块握手需单独现场验收。
- 每次核对 GlassSurface、SettingsGroup、Flow 和 CarText 的实际源码再判断模糊源、隐式高度与字体单位；旧修正不能自动沿用到替代代码树。当前 Arkpets 版本中 GlassSurface 经 `findControl()` 查找背景，SettingsGroup 用 ColumnLayout anchors.fill 同时依赖 implicitHeight，CarText 默认 pointSize；运行日志曾出现布局循环与 point/pixel 冲突警告，不能写成已经消除。
- UI 验证需隔离 XDG/控制设备并等待启动完成；先阅读当前 TerminalRuntime 和 GuiAudit，不能将隔离、初始化警告捕获或审计等待当作本版本已实现的功能。保留热区、裁切、触摸和模糊测试，不删失败测试或屏蔽警告；文档描述验证入口和已知限制，不沿用另一版本的通过结论。
- 修改后核路径、引用和 skill 链接；只在当轮授权范围内同步。主项目源码整理不构成连接 Pi 或同步远端 skill 的授权，不保留过时规则、临时产物或未复核设备状态。
