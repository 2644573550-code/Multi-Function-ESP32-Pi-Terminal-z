# VLC 视频后端与浏览器摇杆

对应根目录路线图的两个条目。保留原播放器布局，不改 `main.cpp`；
第三方集成放在 `third_party/vlc`，屏幕摇杆及上下文滚动逻辑分别封装为独立组件。

## 验收范围

- VLC 3.x：本地视频、播放/暂停/停止、定位、时长、静音/音量、错误、切换媒体与销毁。
- 视频图像在 Qt Quick 场景内显示，保持比例并允许全屏控制层覆盖。
- 摇杆：上下左右连续滚动、死区、松手归中、隐藏/切标签停止。
- 上下文：优先最近操作/聚焦的可滚动区域，滚到边界向祖先传递，最后滚动整页。
- 测试分为功能、界面、性能；使用本地合成素材/HTML，不依赖互联网。
- Linux、Windows、macOS 使用目标平台的 VLC SDK；交叉编译使用目标 sysroot，不能链接主机 VLC。

## 合并门槛

根路线图要求：交叉编译、树莓派实机验证与代码审查完成后才能合并。
自动化主机测试不能替代触屏、音频、硬件解码和树莓派性能验证。
在这些检查完成前，根路线图的复选框保留未完成。

> **当前状态（2026-10-27 核对）**：VLC 后端**已并入 `main`**（PR #21），`VideoPlayer.qml` 已使用 `VlcVideoItem`，构建时缺 VLC 3.x SDK 会直接失败。但本文第 75 行的树莓派实机检查**仍未执行**，根路线图对应条目也仍是未勾选状态。也就是说：「已合并」不等于「已验收」，两者不要混读。

## 构建与运行测试

依赖安装及跨平台 SDK 布局见 `third_party/vlc/README.md`。
独立测试要求 CMake 3.21+、C++17、Python 3、Qt 6.2+ 的 Quick、QuickControls2、
Test、QuickTest、WebEngineQuick，以及 VLC 3.x SDK/运行时。完整终端仍要求 Qt 6.4+。

```sh
python3 pi_terminal/tests/run_media_browser_tests.py --configure
# 非标准 SDK/Qt 路径通过 CMake 参数传入：
python3 pi_terminal/tests/run_media_browser_tests.py --configure \
  --cmake-arg=-DVLC_ROOT=/path/to/target/vlc \
  --cmake-arg=-DCMAKE_PREFIX_PATH=/path/to/qt
# 选择一个类别；默认运行全部。
python3 pi_terminal/tests/run_media_browser_tests.py --label performance
```

脚本自动生成无外部编解码器依赖的 320×180/24fps/8 秒 AVI 素材。
这是冒烟性能测试，不能代表高码率/高分辨率素材的解码能力。
独立测试包含实际 VLC 解码、原视频页面的三个窗口尺寸、QML 摇杆鼠标操作、
真实 WebEngine 的本地嵌套滚动网页。
仅当明确传 `--backend-only` 时不构建 WebEngine 集成测试；报告会记录此选项。
完整终端构建还提供 `media_video_player_interface`，验证原视频页面的三个尺寸、
VLC 实例、静音和播报衰减绑定：

```sh
cmake -S pi_terminal -B build/terminal -DBUILD_TESTING=ON
cmake --build build/terminal --parallel 2
python3 pi_terminal/tests/run_media_browser_tests.py --build-dir build/terminal
```

结果位于构建目录的 `artifacts/`：`report.json`、`junit.xml`、`test.log`、
`vlc-letterbox.png`。窗口截图仅在平台支持 `grabWindow()` 时生成 `video-ui-*.png`；
不支持时明确写入日志，页面布局与属性断言仍会执行。基础性能在 Qt Quick 窗口中
记录首帧时间、解码帧率、事件循环心跳最长间隔和进程 CPU；
阈值可以通过脚本的 `--max-startup-ms`、`--min-fps`、`--max-heartbeat-ms`、
`--max-cpu-percent` 调整。不要以放宽阈值代替排查性能问题。

默认离屏测试不改变 Chromium 沙箱设置。受限容器若无法创建 Chromium 沙箱，
应在专用测试容器中处理权限/显示服务，不要在生产启动脚本中禁用沙箱。

## 摇杆约定及限制

默认增加的是屏幕虚拟摇杆，不新增 Linux joystick/USB 手柄协议。
拖动方向对应内容滚动方向；偏移越大速度越快，中心有死区，松手/切标签/
页面隐藏/窗口失焦/加载新页面时停止。每次最多一个 JS 请求，超时归中。
按网页缩放比例校正速度，保留网页本身的 `overscroll-behavior` 边界约束。

上下文选择优先最近指针经过/点击/聚焦的元素，再沿可滚动祖先向外传递剩余位移。
支持 textarea、普通嵌套滚动区、同源 iframe 和开放 shadow DOM 的祖先链。
跨域或沙箱 iframe 受到浏览器同源安全限制，不能通过注入脚本直接控制内部滚动；
不绕过该限制。只有点击摇杆时页面没有上下文，则使用页面中心/整页作为回退。

## 树莓派实机检查（待执行）

- 使用目标 ARM Qt/VLC sysroot 交叉编译；检查 ELF 架构及动态依赖，不能混入主机库。
- 实机播放产品实际使用的 MP4/MKV/网络流，检查声音、暂停/定位/结束/反复切换。
- 触屏验证全屏控制层、摇杆松手和取消、隐藏页面、窗口失焦、横竖滚动及页面缩放。
- 用实际 720p 素材测首帧、CPU、温度、内存与长时间播放，确认 CPU-copy 路径满足预算。
- 运行全部 `media_` 测试，保留报告，再进行代码审查。未经这些步骤，不标记路线图完成。

## 平台验证状态

- 已验证：Linux x86_64，GCC 11、Qt 6.2.4、VLC 3.0.16 的独立后端及浏览器测试构建。
- 未验证：完整终端 Qt 6.4+ 构建、Windows/macOS SDK 构建、ARM 交叉编译与树莓派实机。
- 当前离屏 Qt 6.2 页面测试会记录视频页自适应布局的 binding-loop 警告；
  属性/几何断言通过并不表示视觉回归验收完成，需要在产品 Qt 版本与真实显示器复核。
