# 原生终端功能维护

所有源码路径相对项目根。先读 `docs/native-runtime-modules.md`、`docs/native-music.md`、`docs/pi-ui-regression.md`，再看实际实现，文档和代码冲突时报告并修正。

## 最小修改与设计

- 保留现有 `MusicPlayer.qml`、`Main.qml` 路由及原生 Qt 架构，不重写成另一个页面，不因媒体不使用 WebView 而删除独立浏览器的 Qt WebEngine。
- 重点检查 1024×600，触控热区主要控件至少 48px；`Theme.hit` 是默认尺寸，不是所有紧凑控件的硬下限。
- 文案用 `I18n.t()`，补齐中英文；用户歌曲、文件、便笺、网页、插件和拼音候选原文不翻译。
- 窄屏热区、地址栏宽度和任务栏内容以当前源码实测；浏览器工具菜单当前使用单一自定义触发项弹出 Material 菜单，是否运行审计遵循用户当轮要求，不引用其他副本的菜单状态。
- 当前 CarText 默认 pointSize；调用方覆盖 pixelSize 可能同时保留两种字体 resolve 位并出现 Qt 警告。SettingsGroup 的 implicitHeight/anchors.fill 依赖需按当前源码和运行日志审查，不能套用其他副本的单向布局修正。
- 审计运行时应显式隔离 XDG 和设备变量；先检查当前 TerminalRuntime、GuiAudit 与截图脚本的等待和警告处理实现。产品对象初始化前隔离、初始化警告捕获等不能从另一副本的结论推断已实现；日志须区分 Qt 布局/字体、宿主 ICC 和 VAAPI 提示。
- 已失败的写法要从根因修正：内容决定 implicitHeight 却 anchors.fill 自身可能形成高度循环；继承 pt 后再设 px 会同时保留两种字体 resolve 位；只改审计等待/屏蔽日志不能修复布局/字体错误。若当前代码树仍采用 SettingsGroup ColumnLayout 双向高度依赖或 CarText pointSize 后由调用方覆写 pixelSize，应据实际日志记录警告，不能声称已改为单向布局或单一字体单位。
- Pi Qt 6.4 的 QML children 列表遍历使用带空值检查的索引循环，避免 `for...of` 包装转换报 TypeError；不要只用 PC Qt 6.5 通过来判断兼容。`Dialog`/`Popup` 不是 Item，`Keys` 事件应绑定到有焦点的 contentItem。
- 图标优先使用 Google 官方 Material SVG；启动状态使用 `check_circle` / `pending`，禁止 Unicode 勾号/圆点替代状态图标。24dp 固定图标槽与文字布局框垂直中心对齐，常规状态行 56dp、短屏 48dp；不能只对齐文本基线。新增图标更新 `MaterialIcon` 白名单、黑白资源、SOURCES.json 的原始来源/SHA256 和许可证，确认 CMake 打包。确需自绘时才保留一致的黑白 SVG，不随意改写官方 glyph。

## 启动层与性能设置

- `StartupSplash.qml` 在 Main 的 `Overlay.overlay` 中，通过 Loader 在完成后卸载；启动时阻挡底层输入并获得焦点，不改成独立 Window/网页。它从 QML 窗口阶段开始，不覆盖动态链接器/Qt 引擎建立前的系统启动。
- 真实阶段为主页/偏好初始化、壁纸解析（出错说明默认背景）、首帧、窗口/面板双缓存。完成比例由阶段计算，进度条与百分比同步，不按秒伪造任务完成；不提前启动 Node API 或等模块 session_ready。
- 首帧后最短展示 1.1 秒，6 秒缓存仍未就绪说明兼容渲染、允许已有局部回退；不把 fallback 说成缓存就绪。退出后无持续动画/全屏模糊/多余 FBO。生产日志 `STARTUP_READY` 记录实际缓存/回退状态。
- 最短展示时间不是总启动耗时承诺；现场服务检查可能超过固定 40 秒窗口，需核同一 PID 的新日志与真实状态，不能只凭超时断言服务挂死，也不能把旧日志的 ready 当新启动成功。
- Material 3 线性进度为 4dp 轨道/圆角、间隔及终点标记；卡片 24dp 内边距、图文 16dp 间距，按 320×320、1024×538、1024×600 验证中英文；短屏隐藏完整步骤列表但保留当前具体内容。
- 设置仅更名为“性能 / Performance”，副标题“节能渲染 / Energy-saving rendering”；不是删除节能开关或输入法功能。

## 模糊与快捷面板

- 唯一算法 `BlurItem`：FBO 多级降/上采样 shader；不用 `QGraphicsBlurEffect` 或每帧 CPU 图像处理。
- `SystemTools.energySaving` 为唯一状态：开启 2 级/0.25 分辨率，关闭 4 级/0.5；不能以关闭模糊替代节能。
- 统一 `desktopBackground` 采样源，每个表面使用映射后的局部坐标，不能缩整张壁纸进卡片或从别的模糊卡片二次取样。
- 背景源必须覆盖完整窗口（`height: win.height`），不能改回只填扣除 footer 的 contentItem。Overlay 快捷面板比内容区高，源范围不足会使任务栏附近采样越界透明；保留透明越界语义，修范围而非夹紧 UV。`quick-glass-*` 已断言整个面板采样在源范围内，Pi 实际窗口可能为 1024×538，不硬编码 600px。
- 应用/主页卡片复用 strength=2 的窗口缓存，快捷面板复用 strength=3.2 的面板缓存。首个桌面帧后先等源 `scheduledUpdateCompleted`，再等匹配 revision 的有效 `frameReady` 才发布；双槽、generation 防串帧，生成中再次失效合并到下一次请求。不能将“已有 FBO/texture id”当成输入捕获已完成。
- 原生消费者不会替隐藏的 ShaderEffectSource 维护 effect 引用；源用桌面后方微小节点参与场景图初始化，不能随意改成 `visible:false`。生成节点自身不绘制冗余全屏 quad；同一静态 revision 不重复模糊，场景图失效后重新预热。默认壁纸须用原生 alpha 和缓存图检查，不能把底下 UI 透出当成模糊有效。
- `CachedBlur` 使用内嵌 QSB 只做局部裁切/圆角/透明度合成，不再为每张卡片创建 FBO；未就绪才启用原生模糊 Loader 回退，不能让不可见回退对象继续随最大化分配纹理。
- 面板 `GlassSurface.staticBlur=true`，源缓存保持冻结。开始打开即可按最终落点裁切；元素变化在布局同帧更新 UV，不能先把临时 1×1 区域铺满卡片。不能等展开完成再延时，不加位置轮询或每帧捕获。
- `BlurItem` 缩小 FBO 尺寸时关闭 Qt 默认 textureFollowsItemSize，自己在 synchronize 中按实际尺寸/DPR/分辨率失效；新 FBO 清透明，已有有效帧在源未就绪时保留。不能恢复按未缩小尺寸反复分配的路径。
- 壁纸加载/恢复、背景尺寸和能耗变化更新共享缓存，不重建页面；普通状态刷新不能无条件重建主页模型。详细实测与限制见 `docs/blur-performance.md`，不把零 FBO 分配说成稳定 60 FPS。
- 展开直接使用已有设备状态缓存，避免在动画首帧同步广播全量 SystemTools 刷新；保留现有 5 秒轮询、设备与开关事件。性能计时不得启用诊断用缓存 alpha GPU 回读。
- 热点/蓝牙/电源/音量用 `QuickPanelTile`，保持一致大小和间距；控制与下方状态网格都固定两列，避免 Pi 缩放时下方自动三列破坏等宽。音量总在系统电源卡片右侧，媒体出现时控制卡片排媒体右侧，不能回到圆形开关。音量点击展开系统主音量滑块与静音；新增卡片继续复用面板缓存，不另建模糊算法。
- GlassSurface 实现随代码树而异；当前 Arkpets 源码通过 host.findControl(..., "desktopBackground") 查找采样源。若出现绑定/递归警告，检查节点重挂与查找依赖，不要假定已应用显式稳定引用修正。

## 音乐与视频

- 当前路径：`MusicService` Qt Network → `music_api/server.cjs` / 第三方服务 → Qt Multimedia；不依赖官方 CLI/mpv，不搬用浏览器 Cookie。
- 本机默认 127.0.0.1:3000，不设独立开机自启服务。构造只读会话，首次在线请求探测端口、按需启动 Node 子进程；本地播放不拉起，远端 endpoint 不启动本机，已有本机服务只复用。退出只停止自己启动的服务。Node 18+，`npm ci` 由锁文件安装，目标机 Node 架构正确。终端开机自启不等于音乐 API 已启动/就绪。
- 本地 metadata 用 ffprobe，内嵌封面用 ffmpeg 缓存；在线/本地封面不得串用。
- 当前 `localMusicMetadata()` 仍有同步 QProcess 等待；Image 的 asynchronous 不代表元数据扫描完全异步。大量本地歌曲/USB 慢盘性能需实测，不宣称已解决所有阻塞。
- 队列是选歌时当前页快照，每页 50；浏览/登录/音源请求互相隔离，过期结果不能覆盖新曲目。
- 随机和顺序/循环模式按钮已移除；仍有兼容状态/测试不等于应恢复 UI。
- `MediaPlayer.position` 为毫秒；拖动只预览，停留也不 seek，松手仅提交一次；歌词/键盘 100ms 合并。YesPlayMusic 的 Electron 缓存粒度是整首 ArrayBuffer/Blob 和下一首预取，不是片段 Range。Qt 在线 seek 首次按需把当前完整音源写入限额 LRU 缓存，QSaveFile 提交完成前继续旧远程流；之后从本地完整文件零增益预热、定位并按原播放态恢复。远程源不 pause/play、不发 position seek、不改 rate；已命中缓存时做本地定位。QML MediaPlayer 没有 `seek()` 方法。Pi 回归验证真实 HTTP 下载、cacheReady 后切成本地 source、缓存复用、播放时钟和非空音频设备；人耳确认仍单列。
- 音乐页固定不透明暗色背景和局部 Material Dark；`MusicText` 主白次灰，显式绿色高亮，搜索提示、队列、歌词不能被壁纸对比算法改黑。
- 媒体状态卡绑定真实播放器，后台视频不因 inactive 自动暂停；开始播放一路应暂停另一路。
- 视频保持文件库/播放页、原生 VideoOutput、进度/静音/音量、真实全屏；进入全屏隐藏终端装饰，3.5 秒自动隐藏控件，退出恢复窗口状态。
- 系统主音量由 `SystemVolume` 异步调用用户会话 pactl/default sink（PulseAudio / PipeWire-Pulse）；无 sudo，写入合并、2.5 秒超时，面板可见才每 5 秒读回；不可用明确禁用调节，不以 QSettings 冒充系统实际音量。
- “声音与语音”新增 `duckMediaForSpeech` 持久开关，默认开启。现有 Qt TTS / espeak-ng 播报期间，音乐/视频 AudioOutput 为当前用户媒体音量 × 0.2，120ms 渐变；结束、取消或失败释放。绝不改系统主音量、播报增益或媒体播放/静音状态，不回写旧音量；连续播报到队列结束才释放。只覆盖终端自身播报，不拦截别的应用音效。
- 音量/播报优先的启用指纹与当前实测读 `docs/deployment-20261018.md` 及 `audio-validation.json`；不固化“尚未部署”等中间状态，也不能沿用此前启动发布的硬件通过结论冒充本功能复测。Pi 默认输出/增益/播报进程自动验证和人耳听感分别报告。
- `MusicPlayback.cpp` 和相关 CLI 测试是兼容路径，未检查引用不要删除，也不能据其通过宣布当前在线音源验证成功。

## 文件、USB、图片、文本

- 文件入口调用 `SystemTools` 的安全路径 API；图片、音频、视频优先内置打开，不能直接用外部 APP 替代已支持功能。
- `PictureViewer` 缩放/旋转/拖动/缩略图条；`FileThumbnail` 异步图片和专辑封面。保留合理 sourceSize，避免原图解码拖慢 Pi。
- `RemovableStorage` 约 1.5 秒扫描 sysfs + QStorageInfo。既有设备启动只列出，新插入弹提示；未挂载仅在用户点击时异步调用 udisksctl，不使用 sudo。
- USB 命名空间 `@usb/<id>/...` 只映射当前有效挂载根，拒绝 `..`、绝对路径/符号链接逃逸、系统根/boot 卷和根目录删除/重命名；拔出撤销访问，文件管理器返回主目录。
- 模拟挂载测试覆盖扫描/权限边界，不代表真实 USB、文件系统、Polkit 挂载通过。目标系统需 udisks2 或系统已完成挂载。
- 文本用 `EditorToolbar` + `TextFormatter`：撤销/重做、剪贴板、查找替换、字体/字号/对齐/项目符号。纯文本序列化为 UTF-8，HTML 保留格式，另存覆盖确认；保留现有 64 KiB 上限。

## 窗口内 IME

- 文件名虽叫 `FloatingKeyboard.qml`，实现必须是宿主 Overlay 内的 Popup，不能创建独立顶层 Window。
- 1024×600 的默认尺寸 680×268；拼音与候选合并一行，主要按键高 48，候选只显示文字、chevron 翻页。
- 左上拼音区拖动，`clampPosition()` 限制坐标，终端窗口移动/缩放后依然内部；不挤出状态坞。
- 保留正确输入目标和组合提交；检查全拼→候选提交→退格→英文输入，而不仅是截图存在。
- 检查 100/125/150% 界面比例、较小宿主窗口、窗口边界、文字截断；offscreen 合成测试不替代真机触摸。
