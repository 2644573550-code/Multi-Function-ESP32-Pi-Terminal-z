# 2026-10-18 当前 Pi 部署与维护

## 运行目录与边界

- PC 源码：`/home/axe665/桌面/Multi-Function-ESP32-Debugger-master`；Pi 镜像源码：`/home/raspberrypi/Multi-Function-ESP32-Debugger-master`。
- 正式安装：`/home/raspberrypi/.local/opt/orange-pi-terminal/20261018`；入口 `bin/orange_pi_terminal`，三个内置库在 `lib/orange-pi-terminal/`。
- 原 `pi-module-terminal.service`、ExecStart 覆盖、桌面入口及私有产品配置保留，不更改 token、网络、串口身份或 ESP 固件。
- 音乐 API 和 ARM64 Node 依赖仍在 `libexec/orange-pi-terminal/music-api/`；按需启动，不增加开机常驻 API 服务，不覆盖现有依赖或用户登录数据。

## 构建和更新

ARM64 在 PC 使用匹配 Pi Qt 6.4.2 的 sysroot/主机工具交叉构建，Pi 只实测。当前隔离交叉 SDK 保存在 `/home/axe665/esp32_workspace/toolchains/pi-debian12-qt642-cross/`，入口 `env.sh` 和 `toolchain.cmake`；sysroot 按现场 Pi Qt 6.4.2 建立，ShaderTools 仅解包进 PC sysroot、未安装到 Pi。Pi OS/Qt 升级后需刷新 sysroot。正式 Release 关闭 `BUILD_TESTING` 和 `TERMINAL_REPAIR_AUDIT`；界面审计运行包与正式包分开。

使用 `cmake --install <production-build> --prefix <staging> --component TerminalRuntime` 生成主程序和三个同版库，保留相对 RPATH 与 SONAME 链接。先验证候选、架构、依赖和哈希，再停服务整体更新；不在线覆盖正在映射的库，不仅复制主程序，也不另建旧版归档。

本次只更新运行组件，已有模块、SDK、许可证、音乐 API 和 Node 依赖保持。安装目录 `SHA256SUMS` 继续覆盖 41 个非 Node 依赖文件；更新后逐项核验，并确认真实 `/proc/<PID>/exe`、三个库映射和无临时路径/Qt Test。原私有配置、服务及 drop-in 逐项核哈希不变。

## 当前功能

- 原生 `StartupSplash.qml`、Google 官方状态图标、真实初始化阶段、完成后卸载；设置分组“性能 / 节能渲染”不删除输入法功能。
- 双槽背景模糊缓存、局部裁切和完整窗口采样源保持。快捷面板普通/节能均保留模糊，不恢复逐卡片或每帧捕获。
- 新增“音量”卡片，固定在快捷面板系统电源卡片右侧，展开系统主音量滑块和静音。通过异步 pactl 控制默认输出；滑块、静音和收起按钮在实际桌面缩放后仍至少 48px。
- 声音设置新增默认开启、持久保存的“播报时降低媒体音量”。终端真实语音播报期间，音乐/视频输出乘 0.2，结束后恢复；不改系统主音量、播报输出，也不覆盖用户期间暂停、改音量或静音。只覆盖本终端自己的播报，不截获其他应用提示音。
- 音乐进度条与歌词采用 YesPlayMusic 同粒度的整曲缓存：首次 seek 下载当前完整音源；完成原子缓存前保持旧远程流，之后切到本地文件，零增益预热解码器并定位目标，再按原播放状态恢复。后续缓存命中直接本地定位；LRU 容量 512 MiB。远程流不 pause/play、不写 playbackRate。
- 设计、实现及长期回归入口见 `native-music.md`、`pi-ui-regression.md`、`blur-performance.md`。

## 验收记录与限制

整曲缓存版已部署：Pi Qt 6.4.2/GStreamer 的候选正式库通过 5 项 HTTP MP3 回归，覆盖拖动、真实歌词委托、缓存完成前保持远程流、完整文件原子提交后切本地定位、缓存跨 MusicService 重建复用及约 1× 时钟；测试输出设备为 HDMI，测试媒体增益 8%。正式程序已启用，服务 active、重启数 0，硬件渲染 VC4 V3D、`cacheReady=true`、`usingFallback=false`，41 项安装清单校验通过。主机 10 项选定 CTest 和 Node 队列/缓存逻辑通过。系统主音量、静音、配置和 service drop-ins 未更改。测试 tone/设备枚举不等于真实在线歌曲的人耳听感，仍需现场确认。

音量/播报版本的 9 个旧专项场景和音量往返结果在 `audio-validation.json` 中明确标注验证构建，不冒充本次复跑。本轮 seek 测试不写系统主音量；清理不覆盖用户后续调节的音量或静音。

两端 `~/esp32_workspace/checkpoints/deploy-20261018/` 仅保留当前紧凑记录：

- `deployment.json`：正式安装指纹、服务现场状态、此次启动日志和配置保护结果。
- `audio-validation.json`：本机/实机音量与播报测试、原始声道音量/静音恢复、对应审计构建指纹及验证范围。
- `seek-validation.json`：host 10 项选定 CTest、Node 队列/缓存逻辑、Pi 候选正式库预缓存与本地定位测试及听感限制。
- `verification.json`：最终清单/映射校验、已知限制和清理统计。
- `SHA256SUMS`：当前正式安装清单，不是审计构建指纹。

报告按实际构建和场景区分，不把过去启动四组合、语言切换或模糊性能测量自动算作新版本复测。本机软件渲染不是 Pi 硬件 OpenGL；实际 AudioOutput 与真实播报进程的自动验证也不等于人耳听感验收。

仍保留既有 `GlassSurface.backdrop` 绑定循环警告和两项旧全局 UI 审计失败/超时的限制，不宣称完整测试全绿或 FPS 稳定提高。真实 U 盘挂载、实体触摸耐久、特定线上音源的听感和模块 `session_ready` 需另验；串口路径出现或缺失都不能冒充握手结果。

## 清理与同步

本轮清理已检查仓库图片、测试入口、文档引用和运行目录：项目内没有本轮生成的回归截图；保留 `docs/figures`、根目录原始设计图及 YesPlayMusic 随包静态截图。仅删除 `pi_terminal/tools/__pycache__` 的 4 个 `.pyc`（20,582 bytes），不删除长期测试源码或 `node_modules` 静态资源。先前缓存功能部署留下的 PC/Pi 暂存构建、runner 与测试音频已清理；可复用 Qt 6.4.2 交叉 SDK 保留。

保留当前源码、可复用工具链/sysroot/构建树、长期测试与只读音频诊断脚本、原始设计图、官方图标/字体/许可证、有效固件、私有配置及运行数据。用明确路径和校验和同步源码、文档、全局 skill 与紧凑记录，不用宽泛 `rsync --delete` 或按 untracked 状态清理。
