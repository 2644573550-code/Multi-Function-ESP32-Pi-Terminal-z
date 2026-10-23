# 网络电视播放内核：Kodi 取经笔记 + 实现说明

参考对象：`xbmc/xbmc`（master 与 18.9-Leia）与 `xbmc/inputstream.adaptive`。
抓取时间 2026-10，源码证据见各节末尾的 raw URL。

## 1. Kodi 到底怎么处理 m3u8

- 裸 `.m3u8` 不走 inputstream，而是 `CDVDInputStreamFFmpeg` → `avformat_open_input()`，
  由 FFmpeg 的 `libavformat/hls.c` 全权处理。
- Kodi 几乎不设 HLS 选项：`GetFFMpegOptionsFromInput()` 只透传
  `seekable/reconnect*/icy*`，所以 `live_start_index`、`max_reload`、`http_persistent`、
  `seg_max_retry` 全是 FFmpeg 默认值（`live_start_index=-3`、`seg_max_retry=0`）。
- 唯一 HLS 专属优化是重试 open 时的 `load_all_variants=0`（新 FFmpeg 已无此选项）。
- inputstream.adaptive 才是自研实现：每流 1 个下载线程、分片缓冲硬编码 4 片、
  直播重试 6 次 × 1s、ABR 取最近 10 次测速、视频预算占带宽 90%、
  `live_delay` 默认/最小 16s、刷新间隔取最小 `EXT-X-TARGETDURATION`，不支持 LL-HLS。

结论：Kodi 的 HLS“优化”不在解析，而在**缓冲有限、失败重试、码率钉死**这三件事。

## 2. 帧节奏与丢帧（Kodi 真正的平滑来源）

`VideoPlayerVideo.cpp` / `RenderManager.cpp`：

- 渲染端先跳帧：显示最新到期帧，中间帧进 discard，`lateframes > 10` 才让解码器丢。
- 包迟到 > **500ms** → `m_skipToKeyFrame`，直接跳到下一个关键帧。
- `WaitForBuffer` 超时 `clamp(显示时间+500ms, 50ms, 500ms)`；缓冲 < 2 才 emergency。
- `NUM_BUFFERS=6`，队列最小 2；Pi 上 MMAL 用 26 个 opaque 句柄，像素不进 ARM 内存。
- 丢帧要“记账”：每丢一帧记 `{pts, frames}`，等渲染 pts 追过再扣回，避免连环丢。

## 3. 本次实现（pi_terminal/src/ui/GstVideoItem.*）

管线：

```
souphttpsrc is-live=true timeout=10 retries=5 keep-alive=true
  ! hlsdemux [connection-speed=N]
  ! queue max-size-bytes=6MB
  ! tsdemux ! h264parse ! v4l2h264dec
  ! videoconvert ! video/x-raw,format=NV12
  ! appsink sync=false max-buffers=4 drop=true
```

取自 Kodi 的三点：

1. **帧队列代替双缓冲 memcpy**：解码线程只把 `GstSample` 挂进 4 槽环（Kodi `NUM_BUFFERS=6`），
   渲染线程直接从 `GstVideoFrame` 行指针上传纹理（`GL_UNPACK_ROW_LENGTH`），
   省掉每帧一次 1080p 全帧拷贝。
2. **按 pts 实时打点**：`due = anchorPts + (now - anchorWall)`，只有到期的帧才 `update()`；
   渲染队列满则丢最旧，渲染端拿到的最新到期帧之外的帧直接 discard（Kodi `PrepareNextRender`）。
3. **迟到 > 500ms 重新锚定**：整段延迟一次清掉，绝不让延迟累积；直播换段 pts 前跳同样重锚定。

其他：`TERMINAL_GST_CONNECTION_SPEED`（kbps）对应 Kodi 的 `bandwidth.max`，
`TERMINAL_GST_QUEUE_BYTES` 调整抖动缓冲。

## 4. 实测（Pi 3B，CCTV-2 1080p25 网络源）

| 指标 | 改造前 | 改造后 |
|---|---|---|
| 解码出帧 | ~22.8 帧/s | ~23.6 帧/s |
| 渲染 | ~20–22 帧/s | ~23–25 帧/s |
| 整机 CPU | ~149% | ~159% |
| 延迟累积 | 无机制 | 500ms 上限，超出即重锚定 |

局限：该源码率约 9 Mbps（10s 分片 ≈ 11MB），Pi 3B 硬解 1080p 的天花板约 25 帧/s；
`vcgencmd get_throttled` 报 `0x50005`（欠压 + 曾降频），换电源前不要指望更高帧率。

## 5. 源码出处

- https://raw.githubusercontent.com/xbmc/xbmc/master/xbmc/cores/VideoPlayer/VideoPlayerVideo.cpp
- https://raw.githubusercontent.com/xbmc/xbmc/master/xbmc/cores/VideoPlayer/VideoRenderers/RenderManager.cpp
- https://raw.githubusercontent.com/xbmc/xbmc/master/xbmc/cores/VideoPlayer/DVDDemuxers/DVDDemuxFFmpeg.cpp
- https://raw.githubusercontent.com/xbmc/inputstream.adaptive/master/src/common/AdaptiveStream.cpp
- https://raw.githubusercontent.com/xbmc/inputstream.adaptive/master/src/parser/HLSTree.cpp
- https://raw.githubusercontent.com/xbmc/xbmc/18.9-Leia/xbmc/cores/VideoPlayer/VideoRenderers/HwDecRender/MMALRenderer.cpp

## 6. 线路切换（720P / 1080P）

底部控制栏在「正在播放」右侧、音量左侧有一个线路按钮（显示当前清晰度 + 下拉箭头），
点击弹出选项框，逐条列出该频道所有线路的**分辨率和码率**（`1280×720 · 1.4 Mbps`），
当前线路高亮；选中后走 `switchSource()` → 先 `release()` 再换源
（Pi 上只能有一个硬编解码实例），并自动关闭弹窗。

码率/分辨率来自频道数据的 `width` / `bitrate`（kbps）字段，只给实测过的源标注；
没标注的源只显示清晰度。同一清晰度的多个镜像只占一条选项，且排序改成
「按 height 升序、同 height 保持数据顺序」（Qt 的 `Array.sort` 不稳定，
不这样处理首选线路会被镜像挤掉）。

起播频道可用 `TERMINAL_TV_CHANNEL=浙江卫视` 指定，便于演示。

### 为什么要换到浙江卫视

| 源 | 分辨率/帧率 | 码率 | 派上实测下载 |
|---|---|---|---|
| CCTV-1（120.40.39.246 hls/1） | 1920x1080@25 | ~9.2 Mbps | ~1.0 MB/s（刚好追不上） |
| CCTV-1 720p 镜像（74.91 → 63.141.239.226） | 1280x720@25 | ~1.47 Mbps | 76–161 KB/s（需 184 KB/s，不可用） |
| 浙江卫视 720P（阿里 CDN） | 1280x720@25 | ~1.38 Mbps | 3.3 MB/s |
| 浙江卫视 1080P（阿里 CDN） | 1920x1080@25 | ~4.2 Mbps | 4.4 MB/s |

CCTV-1 没有可达的 720p 源，所以把「720P（流畅）/ 1080P（高清）」这一对放在浙江卫视上：
两条线路都远低于 4 Mbps、带宽余量 5–10 倍。

### 有界队列回压

`kSlots=6`（Kodi NUM_BUFFERS），解码线程发现队列满了会 `QWaitCondition` 最多等 400ms，
而不是丢最旧的帧——丢最旧会让渲染端长时间等不到「到期帧」。400ms 仍无空位才丢弃当前帧。

## 7. 换台卡顿与失效源清理

### 换台不再卡 GUI

管线创建/销毁从 GUI 线程搬到独立线程 `GstPlayerWorker`（`QThread` + 队列连接）：

- 实测：原来在 GUI 线程建管线 **243 ms**，现在工作线程 **23–30 ms**，界面不再停顿；
- 串行化 start/stop，保证旧管线彻底 `NULL` 后再建新管线，
  不会再出现抢 `/dev/video10` 导致的“起不来→重试”；
- 起播 20 s 超时（`startTimer_`），源死了会尽快报错换下一个源。

### 自动测试并删除不可用的电视台

`tools/probe_tv_sources.py`（在派上跑，判断标准贴近播放器）：

1. 拉 m3u8（跟随 302），必须 `#EXTM3U`；
2. 主列表先取一路变体；
3. 取最后一个分片（失败再试第一个），`Range` 只拉 32 KB，
   必须是 200/206 且不是 HTML/JSON 错误页；
4. 用 `Content-Length` / `Content-Range` 与 `#EXT-X-TARGETDURATION` 估算码率。

`tools/apply_tv_probe.py` 按报告删源、删台、回写 `bitrate`：

```bash
scp tools/probe_tv_sources.py pi_terminal/assets/tv/china.json rpi:/tmp/
ssh rpi 'cd /tmp && python3 probe_tv_sources.py --input china.json --output report.json'
scp rpi:/tmp/report.json /tmp/
python3 tools/apply_tv_probe.py --channels pi_terminal/assets/tv/china.json --report /tmp/report.json
```

判据分两级（`--shallow` 只做第一级）：

1. 可达性：播放列表 + 一个分片能取到；
2. **吞吐**：把整段分片下完（限时 12s / 24MB），耗时必须 **< 0.8 × #EXT-X-TARGETDURATION**，
   否则直播追不上实时，判为不可用。

本次结果：

- 第一级：119 个源里 116 可达 → 删除 **CCTV-6**（Connection refused）、
  **CCTV-4 中文国际**（403，`auth_key` 过期）、**CCTV-4K**（Connection refused）；
- 第二级：116 个源里 **78 个跑得动** → 又删掉 **CCTV-9 / CCTV-10 / CCTV-17 / CCTV-9 纪录 /
  四川卫视 / 湖北卫视 / 辽宁卫视 / 重庆卫视**（全部镜像都太慢，例如 212 KB/s、11 KB/s、
  `6.0s > 5.6s`）。

最终 **40 个台 / 78 个源**，实测码率写回数据（线路弹窗因此能显示 `1280×720 · 1.6 Mbps`）。
临时变慢的源也会被删，需要时重跑上面的两条命令即可重新探测。

## 8. 低码率默认线路 + 高码率确认

### 默认最低码率

`playChannel()` 不再固定 `sourceIndex = 0`，而是 `defaultSourceIndex()`：
在源列表里挑**实测码率最低**的一条（没有码率信息时退回清晰度最低的第一条）。
换台、点「重新连接」都回到这个默认线路。

### 切到更高码率要确认

`requestSource()` 比较当前源与目标源的 `bitrate`：

- 目标更高 → 弹「切换到高码率线路？」对话框（`tvSwitchWarning`），
  正文带上目标线路的分辨率/码率，按钮为 **取消** / **确认切换**；
- 取消：只关框，不动播放；确认：走原来的 `switchSource()`（先 release 再换源）；
- 目标更低或相同：直接切，不打扰。

对话框是 `modal + dim` 的居中弹窗，打开时暂停控制条自动隐藏，关闭后恢复。

### 低码率示例：河北卫视

浙江卫视没有可用的 480p/576p（阿里 CDN 只提供 720/1080，txiptv 镜像实测 7.5 Mbps 且慢），
而河北卫视有一对合适的线路：

| 线路 | 分辨率 | 码率 | 说明 |
|---|---|---|---|
| **540P（默认）** | 960×540@25 | ~1.5 Mbps | 天翼 CDN，实测 2.2 MB/s |
| 1080P | 1920×1080@25 | ~6.4 Mbps | 切上去前弹警告 |

实测：启动即 `GST_FIRST_FRAME 960x540`；点 1080P 弹框，点「取消」保持 540P，
点「确认切换」后 `GST_FIRST_FRAME 1920x1080`；540P 下 `samples` ≈125/5s、`UI_FPS` ≈42。

## 9. UI 改版（按项目原有 Material 风格）

### 侧栏：文件管理器式逐级进入

- 一级：分类（央视 / 地方省市）；
- 二级：点分类后整块面板换成节目列表，顶部「← 分类名」返回；
- 搜索框输入时直接进「搜索结果」二级页，返回即清空搜索；
- 节目行带台标（数据里的 `logo` URL，异步加载，失败回退 `tv` 图标），
  行内留 12 px 左内边距，侧栏加宽到 236 px，图标不会再被裁。

### 搜索 + 键盘

- 顶部：键盘按钮 + `CarField` 搜索框 + 清除按钮；键盘按钮再点一次收起；
- 键盘直接复用项目自带 `FloatingKeyboard`（未改任何逻辑），
  通过 `signal keyboardRequested()` + `virtualKeyboard` 走 `StatusDock` 同样的接线方式。

### 底部控制栏

- 去掉「正在播放 / 已暂停」状态文字；
- 码率选择器用 `anchors.horizontalCenter` 真正居中，宽度按内容计算（修掉 1080P 超出圆角）；
- 播放/重连 56 px、全屏 56 px（原来 48），音量条 `fillWidth` 拉长，窄屏隐藏百分比；
- 左右两组各占「栏宽 − 中间槽」的一半，互不挤压。

### 声音

管线加了音频分支：

```
tsdemux name=demux
  demux. ! queue ! h264parse ! v4l2h264dec ! videoconvert ! video/x-raw,format=NV12 ! appsink
  demux. ! queue leaky=downstream ! decodebin ! audioconvert ! audioresample ! volume name=vol ! autoaudiosink
```

`leaky=downstream` 很关键：重建管线时音频设备偶尔不能立刻打开，普通 queue 会把
`tsdemux` 堵死、视频跟着停；leaky 队列丢几帧音频但保住视频。音量/静音走 `volume` 元素。
`TERMINAL_GST_AUDIO=0` 可回退纯视频（排查音频设备用）。

### 暂停/继续

`togglePlay()` 不再重建管线：暂停 3 秒内直接 `PAUSED → PLAYING`（清空帧队列、重新锚定），
超过 3 秒才重新起播（避免长时间暂停后落后于直播）。

### 其他

- 删除「其他」分类（凤凰香港、金鹰卡通两个源都已失效）；
- `keyboard.svg` 缺 `fill` 属性，`monochromeIcon` 无法着色 → 深色界面上是黑的，
  补上 `fill="#f4f8ff"`（与其它图标一致）；
- `Main.qml` 里之前为了性能隐藏壁纸/模糊层的改动**已全部回滚**，
  现在只保留电视功能本身的增补（应用入口、互斥暂停、页面实例），与原项目渲染完全一致。

## 10. CCTV 低码率线路 + 布局调整

### CCTV 为什么比地方台卡

地方台基本都是 576p / 2 Mbps 左右；央视的主源是 1080p **50fps**、7–9 Mbps
（`120.40.39.246 hls/N` 与 txiptv `000N_1`），Pi 3B 的 1080p 上传就要 35–40 ms/帧，
解码也只有 23–25 fps，所以体感明显更卡。**码率本身由源决定，播放器只能换更低的源。**

本次补测后加入的线路（默认选最低码率，所以自动生效）：

| 频道 | 新增线路 | 实测 | 说明 |
|---|---|---|---|
| CCTV-1 | `74.91.26.218/cctv1hd` | 720p / ~3.5 Mbps | 原来只有 1080p50 / 9.2 Mbps |
| CCTV-2 | `74.91.26.218/cctv2hd` | 720p / ~1.7 Mbps | 原来只有 1080p / 9.1 Mbps |
| CCTV-5 | `207.56.13.146/cdnlive/cctv5` | 720p30 / ~0.26 Mbps | 原来只有 1080p |

实测 CCTV-1：`samples` ≈118–125/5s（跟满 25 fps），`GST_RENDER` ≈20 fps。
注意 CCTV-1 的 720p 镜像来自会轮换主机的美国 CDN，偶尔会短暂断流（几秒后自愈）；
要绝对稳定仍然只有那条 1080p50 主源。

同一清晰度但码率相差 ≥15% 时（例如 1080P 9.2M 与 1080P 3.5M）会各占一条线路；
相差 <15% 视为同档镜像只保留一条。

### 码率选择器文字不再溢出

按钮宽度改为「文字宽 + 箭头宽 + 30 px 留白」，内容用固定内边距（左 15 px、右 11 px）
而不是居中布局，`1080P` 也有足够空间。

### 布局

搜索栏收窄到左列（搜索栏 + 侧栏）宽度，右列视频区从顶部到底部占满整高，
显示面积比原来大（多了原来搜索栏那一行的高度）；全屏时左列整体隐藏。

## 11. 画面比例自适应 + 码率选择器改成下拉框

- 删除 **CCTV-5+**（唯一源实测卡到不可用）。
- `GstVideoItem` 新增 `videoSize` 属性（首帧 caps 的宽高，切源时更新并通知）；
  `TvPlayer` 的视频容器按「源宽高比」在可用区域里等比取最大，居中放置，
  视频刚好铺满容器——**不再出现上下/左右黑边**，多余区域露出页面背景。
  全屏时同样按比例居中。
- 码率选择器改成项目记事本「段落对齐」那种 Material `ComboBox` 风格：
  收起时只显示 `720P`，点开向上弹出（控件在底部，避免被窗口裁掉），
  每行显示「720P / 1280×720 · 3.5 Mbps」两行信息；控件宽度按内容计算，数字不会出界。

## 12. 构建与依赖（免安装）

新增依赖只有 **GStreamer**（Qt Multimedia 在目标机上本来就依赖它的运行时）：

- 运行时（目标机已有）：`libgstreamer1.0-0`、`libgstreamer-plugins-base1.0-0`、
  `gstreamer1.0-plugins-good/bad`（`v4l2h264dec`、`hlsdemux`、`souphttpsrc`、`alsasink` 等）。
- 开发头文件：已随仓库放在 `third_party/gst/<arch>/`（`amd64` 已提交；`arm64` 可在目标机执行
  `tools/fetch_gst_headers.sh` 生成，只 `apt-get download` + 解压，不需要 root、不安装系统包）。
  `third_party/gst/<arch>/lib` 里只有指向系统运行时 `.so.0` 的符号链接——**不要**复制实体
  `libgstreamer` 进来，否则 GStreamer 会按自身位置重定位插件目录，所有 element 都会 “no element”。
- 其它：Qt 6.5.3（项目原有要求）、树莓派交叉编译用 `CMAKE_SYSROOT`。

本机编译：

```bash
cmake -S pi_terminal -B pi_terminal/build-qt653 -DCMAKE_PREFIX_PATH=$HOME/.local/share/orange-pi-terminal-sdk/qt/6.5.3/gcc_64
cmake --build pi_terminal/build-qt653 -j16 --target orange_pi_terminal
```

树莓派交叉编译：`cmake -S pi_terminal -B build-pi642 -DCMAKE_TOOLCHAIN_FILE=~/pi-cross/toolchain-pi642.cmake`
（工具链与 sysroot 属于本机环境，不在仓库内）。
