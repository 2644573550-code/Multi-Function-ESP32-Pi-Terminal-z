# 模糊预缓存与实机验证

## 当前渲染路径

`BlurItem` 仍是唯一模糊算法。`BlurCache.qml` 分别预渲染窗口（strength=2）与快捷面板（strength=3.2）的完整背景，每种缓存使用两个有界 GPU 槽。
首个桌面帧完成后才允许预缓存。候选槽先等待源 `scheduledUpdateCompleted`，再绘制对应 revision 的模糊 FBO 并发送 `frameReady(revision)`；只有最新 generation、匹配的候选槽能发布。生成替换纹理期间继续使用上一张有效纹理，不先清空正在显示的缓存。生成过程中再次失效会合并为下一次请求，不中途复用仍在捕获的槽。

`CachedBlur.qml` 使用烘焙后的 `cached-blur.frag.qsb` 对共享纹理做局部裁切和圆角透明遮罩，不再为每个面板卡片或每一帧窗口尺寸变化创建一个模糊 FBO。缓存未就绪时才通过 Loader 使用原生模糊回退，就绪后卸载回退对象。
缓存生成节点保留纹理提供者，但不向屏幕绘制额外的全屏四边形；OpenGL shader program 按渲染线程/context 复用，资源由渲染器生命周期管理。
源采样节点需参与场景图初始化：原生消费者不会像 ShaderEffect 一样替隐藏源维护 effect 引用，因此使用桌面后方的微小采样节点，不将源设为 `visible:false`。输入捕获未完成不能仅凭“FBO 已分配”发布纹理。静态槽 revision 未改变时不重复执行金字塔，场景图失效后重新预热。

应用窗口、主页卡片与快捷面板始终按实际位置裁切背景，不把整张壁纸缩放进每张卡片。快捷面板展开时使用预测的最终落点，纹理随面板移动；元素增减、布局/滚动变化只更新 UV，不重新执行模糊。没有定时位置轮询或逐帧背景捕获。

## 失效与画质

- 壁纸加载/恢复、背景尺寸变化和能耗模式变化触发新的缓存生成；旧槽保留到替换槽就绪。
- 节能仍为 2 级、0.25 分辨率，普通仍为 4 级、0.5 分辨率；圆角、tint、文字和布局保留。
- 新卡片在首个有效布局帧使用正确的裁切矩形，不先把临时 1×1 采样拉伸成纯色。
- 窗口最大化、缩放变换和主页卡片翻页使用明确的坐标/变换依赖，避免 `mapToItem()` 绑定未失效而残留旧坐标。
- 超出背景有效边界的区域透明处理，不用边缘像素拉成长条。
- `desktopBackground` 必须覆盖完整窗口（`height: win.height`），不能只 `anchors.fill` 扣除 footer 的内容区。快捷面板位于全窗口 Overlay，若源止于任务栏上沿，底部 UV 会越界变透明；修复源范围，不以边缘像素拉伸掩盖缺口。普通/节能 `quick-glass-*` 同时检查面板采样矩形完全落在 `blurSourceSize` 内。
- 展开直接使用现有设备状态缓存，不在首帧同步广播全量状态刷新；原有 5 秒轮询和设备/开关事件继续维护状态。
- `QQuickFramebufferObject` 默认会把实际 FBO 与未缩小的控件尺寸比较。本项目的输出有意缩小，故关闭默认尺寸跟随，在 `synchronize()` 中只对实际尺寸/DPR/分辨率变化进行失效处理。新 FBO 初始化透明；已有有效 FBO 在源纹理暂不可用时保留内容。

## 构建依赖

构建需 Qt ShaderTools（Debian 开发包 `qt6-shadertools-dev`）及与目标 Qt 匹配的主机 `qsb`。CMake 生成并嵌入 shader，覆盖 GLSL 100 ES/120/150；终端运行时不需要外部 shader 文件或开发工具。
QML、shader 资源与渲染器均属于 `terminal_ui` 动态库，发布仍需完整运行包。

## 可重复测试

继续使用唯一截图入口，不另建一次性回归脚本：

```sh
python3 pi_terminal/tests/screenshot_regression.py BINARY OUTPUT \
  --only blur-performance --quit-after-ms 100000

python3 pi_terminal/tests/screenshot_regression.py BINARY OUTPUT \
  --only blur-cache-max-True --only blur-cache-max-False \
  --only blur-cache-layout-True --only blur-cache-layout-False \
  --quit-after-ms 23000 --settle-ms 14000

python3 pi_terminal/tests/screenshot_regression.py BINARY OUTPUT \
  --only blur-cache-transition-True --only blur-cache-transition-False \
  --quit-after-ms 23000 --settle-ms 14000
```

目标机不带 `--host-test`，使用真实 Xorg/硬件 OpenGL，自动按测试前状态停/恢复服务，并隔离配置与产品串口。慢速 Pi 按小批次运行，避免长批次总超时；超时不算通过。
性能场景对两种能耗模式分别执行两轮最大化、恢复、展开、通知变化和关闭，用相同 16ms 帧请求、500ms 采样窗口记录实际呈现间隔、首帧延迟、FBO 分配和金字塔次数。这个 FPS 是受控交互窗口的数据，不是整机长期/空闲 FPS；零呈现窗口也会保留在报告中。
这里观察的是 GUI 线程收到 `frameSwapped` 的事件间隔和等待，包含 GUI 阻塞及信号投递延迟，不是纯 GPU 计时器结果；各次运行的周期状态刷新、浏览器后台负载也可能引入波动。
过渡场景分别捕获最大化、展开和通知变化的 4 帧，合计每种模式 12 帧，用于人工检查空白、临时纯色及错位。截图采集与性能计时分开，避免 GPU 回读污染性能数据。
另用 `blur-cache-default-True/False` 检查默认壁纸：诊断环境才对原生缓存读取 4 个点的 alpha，并检查单独裁切缓存图的 alpha，要求有效背景不透明；防止把透明缓存、底层 UI 透出误认为有效模糊。该 GPU 回读不用于性能场景，也不在正常运行时启用。

## 2026-10-18 实机结果

设备为 Qt 6.4.2、Broadcom VC4 V3D 2.1、约 1 GiB RAM、真实桌面 Xorg。两种模式各动作仅两次采样，不能据此声称稳定 60 FPS。

以下性能数据属于预缓存优化阶段（完整窗口覆盖修复前），不是此次覆盖修复后重新跑出的性能结果。

| 指标 | 优化前 | 预缓存优化复测 |
| --- | --- | --- |
| 最大化期间模糊 FBO 新建次数 | 每次 24–43 | 0 |
| 展开面板期间模糊 FBO 新建次数 | 每次 49–134 | 0 |
| 通知变化期间模糊 FBO 新建次数 | 每次 4–8 | 0 |
| 节能模式展开首帧 | 1078–1159ms | 323–401ms |
| 普通模式展开首帧 | 1592–1757ms | 362–383ms |
| 展开动作采样 FPS（两种模式） | 0–0.9 | 9.1–11.7 |
| 普通模式最大化采样 FPS | 17.8–18.8 | 9.3–19.6 |
| 普通模式恢复采样 FPS | 13.0–18.8 | 18.5–20.1 |

节能模式最大化 FPS 仍有波动（本次 13.9–19.1），普通模式最大化也有低帧率样本，不能据两次采样声称最大化 FPS 已稳定提高。取消纹理重建不等于解决全部 UI/CPU/驱动瓶颈，不隐去这些样本。
PC 8 项选定原生/安装/会话测试通过，旧完整 UI 审计未声称通过。
Pi 已分别验证两种能耗模式的最大化、动态面板、过渡帧，及更换/恢复壁纸、局部模糊坐标、背景音乐与纹理冻结。默认壁纸两种模式的原生缓存采样 alpha 均为 255，缓存图不透明度检查通过。最新发布候选再次通过壁纸更换/恢复及两种快捷面板场景。
完整窗口覆盖修复已部署；Pi 普通/节能模式 `quick-glass-*` 均通过硬件 OpenGL 检查，实际 1024×538 窗口的源与面板范围一致（不是固定假定 600px）。正式包另通过硬件渲染启动检查。`GlassSurface.backdrop` 的既有绑定循环警告仍存在，不声称日志全无警告；此项未在覆盖修复中扩大处理。
