# Pi UI 修复与无人值守截图回归

## 当前设计

- `StartupSplash.qml` 为窗口内 Material 深色启动层：圆角容器、线性进度、分阶段说明和短时缩放/淡入淡出。进度对应偏好/主页初始化、壁纸解析（错误时说明默认背景）、首帧、双模糊缓存就绪，不伪造模块连接或音乐 API 加载。首帧后至少展示 1.1 秒，缓存 6 秒仍未完成则明确显示兼容渲染；完成后 Loader 卸载启动层，释放输入。它覆盖 QML 窗口初始化阶段，不是进程动态链接或 Qt 引擎建立前的系统开机画面。
- 设置分组使用“性能 / Performance”，副标题“节能渲染 / Energy-saving rendering”，不再出现“键盘和输入方案”；保留原有开关与输入法功能。
- 启动状态使用 Google 官方 Material Icons Round `check_circle` / `pending` SVG，不以 Unicode 勾号或圆点代替图标。24dp 图标槽与 20dp 行高文字均垂直居中；常规状态行 56dp，短屏紧凑为 48dp，卡片内边距 24dp、图文间距 16dp。线性进度使用 4dp 圆角指示条、轨道间隔和终点标记，数值与百分比对应同一真实阶段。小于 440px 高度只显示当前启动内容，避免挤出窗口。

设计参考：[Material 3 进度指示器](https://m3.material.io/components/progress-indicators/overview)、[列表规格](https://m3.material.io/components/lists/specs)。图标原始 URL 与 SHA256 在 `pi_terminal/qml/icons/material/SOURCES.json`，许可证随安装包发布；glyph 路径不改写，不在运行时联网取图标。
- Qt Quick/OpenGL；`BlurItem` 用独立 FBO 做逐级减半的 mipmap 金字塔和上采样，禁止 `QGraphicsBlurEffect`。
- 节能开关以 `SystemTools.energySaving` 持久状态为唯一数据源，Switch 使用显式 `Binding` 与 `onClicked` 反转 setter 状态。开启使用两级、0.25 分辨率；关闭使用四级、0.5 分辨率，两种模式都保持模糊。应用/主页卡片复用窗口模糊缓存，快捷面板及其控制、媒体、通知卡片复用独立的静态模糊缓存，不进行持续采样或定时刷新，详见 `docs/blur-performance.md`。
- 应用窗口、抽屉和对话框使用背景模糊与独立 tint，不透明度仅影响背景；文字和控件不随背景透明度变淡。
- 快捷面板日期根据终端语言固定格式化（中文完整显示年月日与星期）；顶部状态坞依据 sysfs 电源供应分类显示电池百分比/充电图标、仅外接电源图标或未知供电图标。状态栏背景使用实时 FBO 模糊和 tint。
- 系统 UI 使用 `I18n.qml` 的中英文资源；第三方模块功能页、文件名、用户文档、网页和拼音候选保持原始内容。新增文案显式使用 `I18n.t()` 并补入词表；`pi_terminal/tools/localize_ui.py` 是批量迁移工具，不应每次改文案都直接运行并改写无关页面。
- 壁纸选择器复用文件管理器的 `SystemTools.files` 目录枚举、`fileIcon` 图标分类和 `setFileWallpaper` 路径设置/校验逻辑；子路径通过当前目录+文件名组成（目录项没有 `relative` 字段）。
- 文件管理器搜索是工具栏独立按钮，按钮显示输入框并聚焦；搜索不再藏在文件操作菜单，过滤随输入即时更新。
- 浏览器地址栏不显示“输入网址或域名”占位提示。浏览器页由 Qt Quick/WebEngineView 显示并使用 Viper 管理的持久 profile、过滤、书签和历史服务；保留触控 shell，不嵌入 Viper QWidget 主窗。触控回归核真触屏滑动页面，细则见 `docs/viper-browser-port.md`。文件预览的系统默认应用操作使用本项目绘制的外部打开图标。
- 浏览器工具菜单遵循 [Material 3 菜单](https://m3.material.io/components/menus/guidelines) 的单入口子菜单语义：不要在自动生成的 submenu entry 前再重复放同名 MenuItem；菜单表面取主题 surface-container、圆角使用主题形状，普通项目图标为 24dp、主要触控行不小于 48dp。小屏将低频操作收进单一子菜单；基础工具保留快捷圆形操作列，菜单中不得重复后退/前进/书签/关闭标签/刷新。菜单使用的 Material Icons Round 本地资源及原始 URL/SHA256 在 `qml/icons/material/SOURCES.json`；官方图标缺失时才设计配套 SVG，不用运行时联网图标。
- `BlurCache` 双槽预渲染背景，完成有效纹理后再发布；`CachedBlur` 仅裁切/圆角合成，不另建逐卡片 FBO。应用窗口及主页卡片依赖真实位置与变换更新局部 sourceRect，不能将整张壁纸缩放进卡片。快捷面板开始展开即可使用最终落点对应的缓存区域，布局变化同帧更新 UV，不能等 `onOpened` 再延时或恢复 33/80ms 轮询。
- 背景源覆盖完整窗口（`desktopBackground.height: win.height`），不只填扣除 footer 的内容区；Overlay 快捷面板底部不得超出缓存。`quick-glass-True/False` 同时核对 `blurSourceSize` 与完整面板采样矩形，不能用 UV 边缘拉伸掩盖缺口。
- 壁纸加载、更换/恢复、背景尺寸和能耗变化使缓存失效；保留上一张有效纹理直到替换槽就绪，不重建页面、主页模型或卡片对象。缓存尚未就绪时才走原生局部采样回退；`windowBackdrop.live` 为回退属性，正常缓存路径已解除其 sourceItem，不代表仍在逐帧捕获。
- IME 为宿主 `Overlay.overlay` 内的 `Popup`，不是独立 `Window`。1024×600 下为 680×268，主要按键高 48px；合并拼音/候选栏，候选只显示文本，chevron 用于翻页。拖动左上角拼音区域，通过 `clampPosition()` 限制在终端内；宿主移动、缩放后不得超界。
- IME 只保留全拼。词典先完整读取再按频率筛选；词组使用 MIT Jieba 词频，全拼通过词组/单字 lattice 分词与 log-frequency 打分，候选选择记录为持久化使用频率。
- 计算器默认普通窗口；浏览器默认最大化。
- 画图使用 `DragHandler` 指针抓取，鼠标/触摸共用采样与提交，覆盖拖动移动、grab takeover 和结束提交，减少触摸过程只留点/丢线。
- 音乐/视频均使用原生 Qt Multimedia；音乐在线 API 为 `MusicService` + Qt Network + 按需启动的本机第三方 Node API，不使用官方 CLI/mpv 作为当前播放路径，也不嵌入 WebView。歌曲列表、播放队列及旧请求隔离分别管理，歌单每页 50 首；参考 `docs/native-music.md`。
- 1024×600 下缩紧非必要留白，主要操作保持至少 48px 热区，默认组件可使用更大的 `Theme.hit`，不能机械要求每个紧凑控件均为 64dp。音乐页用 `MusicText` 固定白/灰文字与 Material Dark，不随壁纸变黑；QML `MediaPlayer.position` 以毫秒提交进度。音乐/视频后台播放由快捷面板控制，播放一路时暂停另一路。
- 快捷面板热点、蓝牙、电源、音量为 `QuickPanelTile` 卡片；控制和下方状态网格均固定两列、统一大小和间距，使音量总在系统电源卡片右侧。有媒体时四张卡片在媒体卡片右侧，不能改回圆形开关。音量展开系统主音量滑块和静音按钮；详见 `docs/native-music.md` 的音量与播报优先说明。
- 图片、歌曲、视频由文件管理器交给内置应用。`PictureViewer` 提供缩放/旋转/缩略图条；`FileThumbnail` 提供图片/专辑封面。文本编辑器使用 `EditorToolbar` + `TextFormatter`，纯文本和 HTML 分别保存，富文本另存需确认覆盖。
- USB 存储由 `RemovableStorage` 检测，侧栏显示，插入弹出提示，按需通过 `udisksctl` 挂载。`@usb/<id>/...` 不是任意绝对路径授权；拒绝越界、符号链接逃逸、系统根卷，拔出后撤销访问并回主目录。模拟挂载测试不能冒充真实 U 盘验收。
- 视频播放器分为文件库和独立播放页，选择文件后才进入播放；播放页采用宽松深色媒体布局，含进度、音量/静音及全屏按钮；全屏时隐藏宿主窗口装饰，可按 Escape 退出。无底部 codec 提示文案。
- 视频库不显示右侧文件数；播放界面只保留一个全屏切换按钮，图标/文案在全屏和退出全屏间切换。
- 视频独占全屏通过 `QQuickWindow::showFullScreen()` 覆盖整个屏幕，隐藏终端状态栏和窗口标题栏；播放控件作为视频画面底部半透明浮层，提供 seek、音量、暂停和退出全屏。`Esc` 或全屏按钮退出。
- 全屏工具栏初始显示 3.5 秒后隐藏；鼠标移动/触摸视频画面时重新显示并重新计时。退出全屏时恢复进入前的最大化/普通窗口状态。文件库条目只显示单行名称，副标题不挤占触控热区。
- 播放/暂停使用真实 pause/play Material glyph；文件库与播放页通过覆盖滑动动画过渡。文件行至少 `Theme.hit` 高度，媒体库卡片采用紧凑边距释放浏览面积，正文字号维持主题规范。
- 媒体库标题、列表卡片及其内容统一 Theme.gap 对齐基线；切换动画为媒体信息卡+动态进度线的双阶段滑动过渡。

## 唯一修复截图脚本

`startup_splash_lifecycle` CTest 验证启动阶段顺序、中英文、320×320/1024×538/1024×600 布局、24dp 官方图标与文字中心对齐、就绪退出及真实 6 秒缓存超时。截图回归的 `startup-{zh,en}-{True,False}` 同时验证首帧启动层、焦点、显示范围和最终卸载；Pi 要求原生缓存就绪且未走超时回退。首帧只请求 `grabToImage()` 异步采集，不能从 `frameSwapped` 回调同步 `grabWindow()` 重入渲染（Qt 6.4/VC4 可能阻塞），排队的 frameSwapped 回调必须加单次标记，不能只依赖 disconnect。

软件/offscreen 渲染没有原生模糊缓存，使用 `--settle-ms 6500 --quit-after-ms 11000` 等待兼容渲染退出；慢速 Pi 可增至 12000/20000，审计等待不缩短或绕过产品启动流程。所有页面截图均断言启动层已卸载，避免把启动图当作目标页面通过。

`startup-settings-{zh,en}` 在选定语言启动后打开设置，验证性能分组文案与启动层卸载。启动场景保持启动前选定的 locale，不在 800ms 再人为 zh→en 切换；`language-en-4` 单独验证运行中英文切换，后续已在 Pi 通过全部原断言，不以预选英文场景替代该结论。

音频新增场景：`volume-{zh,en}` 检查展开和电源右侧几何、滑块可用状态/48px 热区；`sound-settings-{zh,en}` 检查声音搜索下的设置开关文案；`speech-duck-{True,False}` / `speech-duck-video` 检查播报期间两个真实 AudioOutput 的增益和结束恢复，期间主动暂停媒体、修改音量及视频静音，断言恢复不覆盖这些操作。host 假语音持续 4 秒，建议 `--settle-ms 6500 --quit-after-ms 15000`；Pi 的实际播报可能更长，应延长到 35000 或以上，缺少恢复日志仍失败。`quick-glass-*` 的表面集合现包含 `quickVolumeBlur`。

当前音量发布的本机、Pi 硬件场景和正式启用结果保存在两端 `~/esp32_workspace/checkpoints/deploy-20261018/audio-validation.json` 与 `deployment.json`，入口见 `deployment-20261018.md`。明确区分 host 假后端、Pi 真实音量读写/播报/输出增益及人耳听感，不宣称排除两项旧全局审计后就是完整 CTest 全绿。实际缩放下音量交互至少 48px，慢场景媒体样本持续时间覆盖请求运行时间加余量。

诊断日志 `REPAIR_PHASE` 区分 setup、审计和图像采集；子进程超时也保存已有 stdout/stderr 后继续判失败。Pi 曾观测同步 Qt/QML 初始化超过 50 秒，而 `--quit-after` 到事件循环前才设定，因此脚本给冷启动独立的 120 秒预算，再加场景运行时间；总 SSH 时间还应包含 Python 图像解码。集成测试必须等 `startupComplete` 后才触摸图标，不禁用启动层或放松触控/会话断言。

`pi_terminal/tests/screenshot_regression.py` 替换旧 UI 冒烟、设计契约和对比脚本；核心协议与签名会话测试保留。

独立修复优先扩展已有回归场景或原生测试，不要求每次另建临时脚本。只清理自动生成的图片、日志、字节码和一次性验证产物；保留通用脚本、长期测试、必要测试素材和报告中仍需追踪的问题。

ARM64 产物按部署约定先在 PC 上使用 aarch64 工具链和 Pi Qt 6.4 target sysroot 交叉编译，并确认 ELF 架构；Pi 只运行目标机回归。主程序现依赖 `terminal_system`、`terminal_music`、`terminal_ui` 三个动态库，必须同步完整运行目录而非单个可执行文件，参见 `docs/native-runtime-modules.md`。然后执行：

```sh
python3 pi_terminal/tests/screenshot_regression.py \
  /path/to/synced/aarch64/orange_pi_terminal \
  /tmp/opencode/ui-regression
```

脚本记录测试前的服务状态：若原先 active 才停止并在 `finally` 恢复；若原先 inactive 则保持 inactive。遇到 activating/failed 等不明确状态会拒绝测试。测试实例使用隔离 XDG，不提供 profile、USB 控制设备或 sudo 热点变量。Pi使用真实 Xorg `:0`、硬件 OpenGL；每个场景保存 PNG、日志、结构化 JSON。

本机开发可显式添加 `--host-test`：使用 Qt offscreen/software 跑场景，跳过树莓派服务管理、硬件 renderer/FBO 像素对比。结果仅用于发现 QML/布局/输入逻辑错误，绝不可作为 Pi 部署验收。测试可用重复的 `--only <scenario>` 限定场景；截图输出目录是临时产物，回归结束后删除。

验收包含自选壁纸锐图/模糊图像素边缘对比；默认背景原始/模糊截图差值对比；打开应用和快捷设置卡片各自“模糊开/关”基线的像素差值；自选与默认壁纸主页模糊恢复、主页卡片背景、全局状态坞 FBO、快捷设置卡片 FBO/采样源；FBO 实际 draw 日志；电池/外接电源/未知供电图标映射；语言覆盖、窗口前景 opacity=1、IME 边界、词频候选、壁纸选择器和绘图连续线。offscreen 仅检查逻辑与可见状态，所有像素模糊结论只取 Pi 硬件 OpenGL 回归结果。

## 部署与清理

获授权且候选测试通过才更新服务运行包；安装入口不变时不必改 ExecStart。核服务 active、NRestarts、真实 exe/库映射、完整清单 SHA 和字体/硬件渲染；BLE session ready 需控制板现场验证。清理确认脱离依赖的一次性产物和旧资料，保留源码、许可证、有效固件、密钥、私有配置及当前运行依赖；按用户约定不另保留旧版本回滚副本。在线状态不从历史报告推断，单纯资料清理不启停服务。

截图测试验证真实渲染与合成输入，但不能等同物理触摸控制器的长时硬件验收。

## 当前审计规则与限制

当前源码保留原有主题字号/自动缩放，基础热区下限为 48px。显式像素字号通过 `CarText/CarButton.fontPixelSize` 转为对应逻辑 DPI 的 point size，避免同时设置两种字体单位。自适应高度的设置、对话框、音量与媒体内容不再让布局同时决定父容器高度并填满父容器；`GlassSurface/StatusDock` 通过宿主 `blurBackdropItem` 获取背景，不递归遍历视觉树。

电脑端 Qt 6.5.3 当前修复版通过普通及 1.5× DPI 的完整审计，各 61 个场景（含 1024×600），结构化报告均为零错误；16 项选定截图场景以及桌宠、管理、启动层和 HTTP seek 时钟共 4 项 CTest 通过。审计按实际主题缩放/字体单位检查，分别验证背景 tint 与始终清晰的前景文字；旋转按现有 Y 轴翻卡设计检查。画图注入坐标从 JSON 小数取整，仅完全没有输入事件时才启用 host 采样回退，不重复追加笔划。

这不是全部 CTest 或 Pi 验收：Xvfb 渲染器为 llvmpipe，host 截图不验证 GPU 模糊像素。启动日志仍可见两条无 QML 位置的 Qt 内部 `Detected recursive rearrange` 警告（审计开始前），不能宣称日志完全无警告；外部网页脚本和宿主多媒体/托盘环境警告也须单独区分。此次未部署或重启 Pi，实机及物理触摸仍需另验。
