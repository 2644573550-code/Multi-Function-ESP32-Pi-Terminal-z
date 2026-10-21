# 构建、部署与验证

先进入实际项目根目录；下列路径相对仓库。临时目录名只是建议，不代表其中已经有最新程序。

## 本机 Qt 构建

```sh
source pi_terminal/scripts/env.sh
cmake -S pi_terminal -B /tmp/opencode/terminal-dev \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DTERMINAL_REPAIR_AUDIT=ON
cmake --build /tmp/opencode/terminal-dev --parallel 4
ctest --test-dir /tmp/opencode/terminal-dev --output-on-failure
```

正式版使用 `-DBUILD_TESTING=OFF -DTERMINAL_REPAIR_AUDIT=OFF`，不能把测量/GUI 审计和 Qt Test 当作产品必需依赖。
`scripts/env.sh` 默认指向本机 Qt 6.5.3 SDK；CMake 最低 Qt 6.4。不要把 x86 Qt/Node/插件复制到 ARM。
模糊合成 shader 通过 Qt ShaderTools/qsb 构建并嵌入 UI 库，需匹配目标 Qt 的主机工具与开发包（Debian `qt6-shadertools-dev`），但目标运行不需要开发工具。新增 QML/shader 必须进入 CMake 资源。

## 动态库、安装与同步

- 主程序负责启动/连接/调度；三个内置共享库是系统、音乐、界面，不是独立热卸载插件。
- 构建布局：`orange_pi_terminal` + `lib/orange-pi-terminal/libterminal_{system,music,ui}.so*`，保留 soname 符号链接。
- 安装用 `cmake --install <build> --prefix <prefix>`。库在 GNUInstallDirs 的 libdir 下，可能是多架构目录；相对 `$ORIGIN` 寻址，不依赖原源码路径加载界面资源。
- `terminal_ui` 含 QML/字体/图标/QSB，`terminal_system` 含词库，`initializeTerminalUi()` 保留强引用并注册原生类型。不要把资源再重复编进主程序或遗漏 CMake QML/shader 列表。
- 主程序变小不代表总安装体积、启动耗时或内存同步减少；按相同编译选项测量，不能引用固定字节数作为永久保证。
- `shared_runtime_install.py` 验证安装后搬移、三个库实际解析位置、字体/QML、临时构建树复制、拒绝覆盖异版目标；不是要求保留产品旧版备份。
- `tools/runtime_bundle.py` 的 `copy_runtime()`、`runtime_digest()` 仅处理构建树完整复制、SONAME 链接与组合哈希，不停止服务、不删除目录、不自动生成检查点名称。安装树复制整个前缀，不能把构建树 helper 当成通用安装部署器。
- 运行前选取本次成功构建的二进制。不要习惯性启动旧临时预览目录。主程序和库作为同版整体更新，先停止授权范围内的服务，再更新。
- 当前同步/安装约定见项目 `docs/deployment-20261018.md`。旧的日期硬编码部署入口已移除，不重建或引用它；先现场确认服务入口，资料同步不等于启用新版本。

## ARM64 与真实设备

保留项目部署约定：ARM64 在 PC 使用匹配 Pi Qt 6.4 的 target sysroot 和 aarch64 工具链交叉编译；Pi 只实测，不运行 cmake --build，除非用户明确改变约定。当前 Debian 12/Qt 6.4.2 可复用 SDK 位于 `$HOME/esp32_workspace/toolchains/pi-debian12-qt642-cross/`，先 source `env.sh` 再传入其中 `toolchain.cmake`；Pi 更新 Qt/OS 后现场刷新 sysroot，不复用旧 ABI。此目录是长期构建依赖，不纳入临时测试清理。
工具链路径必须现场查证，skill 不假造一个文件。同步前用 `file`/`readelf -h` 核 ELF 架构；同步完整安装/构建运行布局，rsync 用明确路径且不带 --delete。
只有当前任务授权才操作 Pi 服务；事先核 ActiveState/MainPID/NRestarts/ExecStart、真实 exe、组合哈希和 session ready。用户要求不保留旧版资料：先验证候选完整运行包，再按授权更新，不默认另建旧版本目录。清理/skill 维护不得顺带切换或重启服务。

## 测试矩阵

| 入口 | 覆盖范围 |
| --- | --- |
| `protocol_test.cpp` | PBR1 帧与校验 |
| `runtime_test.cpp` | 状态/文件/USB/热点策略等运行时边界 |
| `session_test.cpp` + `run_integration.py` | 签名包、会话、包通道、插件 ABI、生命周期 |
| `music_service_test.cpp` | 原生 API 请求及过期结果隔离 |
| `music_seek_test.cpp` | 生产 QML、HTTP MP3 全曲预缓存、原流保留、QSaveFile 提交后切本地、muted prime/定位/恢复、缓存复用与播放时钟 |
| `music_playback_test.cpp` | 旧 CLI 兼容事务，不是真实当前在线路径 |
| `music_queue_test.cjs` | QML 队列算法，需 Node；未自动纳入 CTest |
| `text_formatter_test.cpp` | 字符格式和序列化回读 |
| `shared_runtime_install.py` | 共享库安装/搬移/依赖闭包复制边界 |
| `startup_splash_test.cpp` | 启动阶段/真实超时、中英文与小屏布局、官方图标中心对齐 |
| `screenshot_regression.py` | 长期通用 UI 分场景、配色、输入法、USB、媒体、模糊 |

主机场景示例：

```sh
python3 pi_terminal/tests/screenshot_regression.py \
  /tmp/opencode/terminal-dev/orange_pi_terminal /tmp/opencode/terminal-screens \
  --host-test --only ime-editor-input --only usb-zh-sidebar \
  --only music-colors-False-lyrics --only quick-glass-True \
  --settle-ms 6500 --quit-after-ms 11000
```

- 真实场景名先从脚本查，不猜测；该脚本在 host 模式去掉产品串口/热点变量并隔离 XDG 状态。
- `music_http_seek_clock` host 默认 AudioOutput 增益为零，Pi 默认也保持零；仅在确认没有用户媒体播放并获同意时可用 `TERMINAL_MUSIC_TEST_OUTPUT_GAIN`（上限 0.25）发测试音。测试不写系统 master volume。运行前核现场状态，隔离 HOME/XDG。HTTP fixture 必须独立线程，Qt 6.5 FFmpeg 可能在 setSource 同步开流，同线程服务器会造成假超时。Pi 可用 `TERMINAL_MUSIC_TEST_FIXTURE` 传 PC 生成 MP3，不在 Pi 编码或编译。测试需断言缓存完成前 source 仍为远程且播放状态连续、完整文件缓存完成后 source 是本地文件、muted prime 后目标位置已到达且输出按原播放态恢复；设备枚举不等于用户真实歌曲听感。
- 部署验证可通过明确 `LD_LIBRARY_PATH` 让该 runner 加载待安装正式库，核其 `/proc/<testPID>/maps` 与 payload SHA；runner 自带 Qt Test 不代表正式包要带测试依赖。测试通过再整体更新正式主程序/三库，检查生产映射无 Qt Test 或临时路径；库内 QML 改动可能仅改变 UI 库哈希，不能只比较主程序。
- 慢速 Pi 可用 `--quit-after-ms 20000 --settle-ms 7000` 延长每场景生存时间及审计前等待（默认 6500/1400，不能降低）；不删除断言或把缺失 REPAIR_AUDIT 当作成功。区分正常退出过早与进程超时，首次 GStreamer 扫描可能增加启动时间。此等待只影响审计，不是恢复产品模糊捕获延迟。
- `--host-test` 为 offscreen/software，只证明布局/逻辑，不证明 FBO/GPU 或真实触摸。
- `startup-{zh,en}-{True,False}` 检查首帧焦点/启动图及真实完成、卸载；Pi 不得走缓存超时回退。启动图只用异步 grabToImage，不从 frameSwapped 同步 grabWindow 重入渲染；已排队回调用单次标记防重入。host 原生缓存不可用需等待 6 秒回退，不能以覆盖层仍在的截图通过目标页面断言。
- 本机执行安装搬移测试前必须 source scripts/env.sh；安装后的 Qt SDK 库不在系统搜索路径，缺少 LD_LIBRARY_PATH 造成 not found 不代表产品漏装内置库。
- `startup-settings-{zh,en}` 检查预选语言启动后的性能分组；启动 fixture 不再于 800ms 反复切语言。`language-en-4` 单独检查运行中切换，后续 Pi 已通过全部原断言，不能用预选英文结果代替动态切换结论。
- Pi 同步 Qt/QML 初始化曾超过 50 秒，`--quit-after` 不是从进程创建开始计时。截图脚本给冷启动独立 120 秒预算，再加场景运行时间；超时保存日志后仍失败。用 `REPAIR_PHASE setup/snapshot/image` 区分阻塞位置，不能把冷启动慢或 Python PNG 解码时间误判为语言切换死锁。整个 SSH 命令还需覆盖图像校验耗时。
- `--ui-test` 在 `startupComplete` 后才触摸主页入口；输入遮罩未卸载前的点击应被拦截，不能关闭启动层或删除触控/会话断言来修集成测试。
- PC 本机硬件 OpenGL 可另做诊断，但必须查实际 renderer，排除 llvmpipe/softpipe；仍不等于 Pi 验收。
- Pi 正式截图回归不带 --host-test，脚本要求 ARM、真实 Xorg :0 和硬件 GL。它会按原服务状态停/恢复服务，未授权不得运行；inactive 保持 inactive，状态模糊就拒绝。
- 完整 UI 必须测普通和节能，明暗壁纸、列表/队列/歌词、IME 边界、媒体后台状态。USB 模拟与真实热插拔、API 假服务与真实音源要分开报告。
- 模糊性能与闪烁走同一截图入口的 `blur-performance`、`blur-cache-max-*`、`blur-cache-layout-*`、`blur-cache-transition-*`；命令与测量窗口解释见 `docs/blur-performance.md`。过渡帧回读与性能计时分开，不把截图采集开销算作运行帧率，也不把长批次超时算通过。
- `blur-cache-default-True/False` 验证默认壁纸的原生缓存 alpha 和单独缓存图；`PI_REPAIR_PROBE_CACHE=1` 只供此诊断，不能全局导出到服务或性能测量。frameSwapped 的 GUI 观测含线程阻塞/投递时间，不是纯 GPU 计时；保留低帧率样本，最大化整体 FPS 尚未证明稳定提升。
- `quick-glass-True/False` 必须核完整面板采样矩形落在 `blurSourceSize` 内，尤其 footer 上沿及底部。实际窗口高度受 Pi 桌面影响，不硬编码为 600；不能只验证坐标相等而忽略源尺寸不足。
- 音量/播报场景沿用同一入口：`volume-{zh,en}`、`sound-settings-{zh,en}`、`speech-duck-{True,False}`、`speech-duck-video`。host 假 pactl/espeak 只验证布局/逻辑；Pi 用真实默认输出与播报引擎，要求实际 AudioOutput 降音/恢复，用户期间暂停、改音量和视频静音均不被覆盖。
- Pi 桌面缩放可能使 `48*uiScale` 实际仅 44px；新增音量滑块、静音及收起按钮需保证最终至少 48px，不能降低断言。控制与状态网格保持两列，`quick-glass-*` 的卡片/状态等宽断言必须保留，不能在新增音量后把下方仍留作自动三列。慢场景媒体样本长度要覆盖 `--quit-after-ms` 加余量，不能等到播报开始时样本已结束。
- 实际系统主音量写入测试须记录原默认输出及各声道原始数值/静音，结束或失败均还原；只读音量卡片验收不等于真实写入验收。真实输出增益与进程结束可自动验证，扬声器听感/语音清晰度仍需人耳确认，不能混称。

## 结果边界与已知限制

- 最近历史设备发布记录见 `docs/deployment-20261018.md` 与对应 checkpoints；实际启用版本、服务和构建指纹须现场复核。主机源码可能已领先该发布，不从旧报告推断当前设备状态，也不为更新文档擅自连接设备。skill 不固化逐轮日志、PID、短暂在线状态或安装 SHA。
- 默认运行 `ctest --test-dir <host-build> --output-on-failure`，包括桌宠/管理/工作进程、文本、协议、音乐事务/API/HTTP seek、运行模型、启动层、插件、安装搬移、签名会话与普通/高 DPI UI 审计。选定子集时明确列出排除项，不能再把旧全局 UI 失败视为永久豁免。

当前 Arkpets 代码的 `TerminalRuntime.cpp`、`GuiAudit.cpp` 与其他副本不同；运行前必须核对审计等待、状态隔离、警告捕获和断言的实际实现，不假定已具备其他分支的审计修正。音乐页是否延迟布局也以当前 `MusicPlayer.qml` 为准；隐藏页布局警告不能按旧修复结论忽略。回归同时查报告和启动日志，不全局屏蔽警告或删除测试来“全绿”。
Pi 硬件 OpenGL 的部分场景已有验收记录，见 `docs/deployment-20261018.md`；真实 USB 热插拔/挂载、在线媒体 seek、实体触摸与持续性能仍需单独验证。不能把历史设备在线信息或旧测试成功当成本次结果。
