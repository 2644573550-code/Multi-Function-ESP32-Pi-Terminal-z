# 原生音乐播放器（非 WebView）

保留原有 `MusicPlayer.qml` 的布局、列表、分页与底部控制栏，使用 Qt Multimedia 解码；`MusicService` 使用 Qt Network 请求第三方网易云 API。音乐页不运行 Electron、Chromium 或官方 CLI。需要本机 API 时会按需启动独立 Node 服务。

设计借鉴 YesPlayMusic 的 Player.js：独立队列快照、异步结果校验、切歌先停止旧音频、音源解析与解码分层、引擎播放事件确认。当前界面已移除随机、顺序/循环模式按钮，不应重新添加；代码中仍有兼容状态和队列测试，不代表这些按钮仍是产品需求。没有复制 Vue/Howler 代码到终端。

## API 服务

默认本机集成，无需填写 API 地址或手动启动：

```bash
bash pi_terminal/scripts/install-music-api.sh
```

安装 Node.js 18+ / npm 后执行一次上述脚本，依赖集中在 `pi_terminal/music_api`，版本由 package-lock.json 固定。CMake install 将该库和依赖打包进 `libexec/orange-pi-terminal/music-api`。发布包需先执行依赖安装脚本；Node.js 是目标机器的系统运行依赖（不能将开发机 x86 Node 二进制复制给 ARM）。进入在线功能首次请求时自动启动，监听本机 127.0.0.1:3000，终端退出时停止自己启动的服务；不会停止外部已有服务。日志和匿名 token 位于应用数据目录 music-api，离线播放不启动服务。

这里的“自动启动”是按需拉起，不是独立的开机自启 systemd 服务：`MusicService` 构造只读取已保存会话，首次在线请求才探测本机端口，未就绪时启动自己的 Node 子进程。配置远端 API 时不拉起本机服务；已存在的本机服务仅复用、不接管。不能为此重复添加开机服务，或把“终端已启动”当作音乐 API 已就绪。

也可以在电脑/服务器运行，不必占用树莓派资源：

```bash
cd pi_terminal/music_api
npm install --ignore-scripts
npm start
```

默认仅监听 `127.0.0.1:3000`。跨设备部署需自行设置 HOST、防火墙和 HTTPS 反向代理；不要把带登录权限的 API 裸露公网。终端音乐页的 API 按钮配置服务地址。只向自己信任的服务登录；扫码产生的会话凭据会发送给该服务，不复用浏览器或 CLI 凭据。

原有音乐页的扫码登录、歌曲搜索、个人创建/收藏歌单、每日推荐、歌单每页 50 首、原生在线/本地播放、上下首、音量/进度已对接。每页重新编号；队列保留选歌时那一页，不暗中加载完整歌单。运行时拒播会报错，不无限跳歌。不引入版权解锁。

在线 seek 触发当前整曲磁盘预缓存：写入 `QStandardPaths::CacheLocation/online-audio`，缓存下载期间原远程流继续播放，完整音源经 `QSaveFile` 原子提交后才切到本地文件定位。缓存命中后播放/seek 均走本地文件。LRU 总容量 512 MiB；尚未接入无条件下一首预取或通用离线下载，不能声称完整复刻 YesPlayMusic。ListView 按可见项创建控件。

本地文件元数据由 `ffprobe` 读取 title/artist/album/genre/year/duration，嵌入专辑封面由 `ffmpeg` 提取到应用缓存目录后显示；本地和在线播放分别写入当前曲目的封面字段，避免两种来源互相沿用。

## 当前界面与控制约束

- 在线和离线音频均由 QML `MediaPlayer` 播放；Qt 6 跳转使用毫秒单位的 `position` 属性，不调用不存在的 QML `seek()` 方法。拖动期间只更新预览，停留超过 100ms 也不发实际 seek，松开只提交一次；歌词点击和键盘调节以 100ms 合并最新目标。
- 进度条和歌词共用 `commitSeek()`。首次在线 seek 不对远程流发 seek：先下载完整当前音源，期间原流继续播放；`QSaveFile` 完整提交后切到本地文件，零增益预热解码器，在本地缓存中定位到最新目标，再恢复原有播放/暂停状态。已缓存曲目也走本地定位流程；离线本地文件仍原位 seek。拖动只预览、松手只提交，歌词/键盘重复输入合并；切歌取消未完成下载，缓存失败则停留在原播放位置并提示。
- `MusicText.qml` 固定主文字白色、次要文字灰色。音乐页及队列/歌词弹窗使用局部 Material Dark 和不透明深色背景，不根据桌面壁纸改变文字颜色。普通、节能模式均需检查。
- 音乐和视频的 `hasMedia`、`mediaPlaying` 等状态由原生播放器驱动；快捷面板可控制后台媒体。开始播放一路会暂停另一路，不允许同时出声。
- `MusicService`、兼容的 `MusicPlayback` 位于 `terminal_music` 共享库；页面、配色和资源位于 `terminal_ui`。`MusicPlayback` 的 CLI 事务测试仅覆盖兼容代码，不应据此把当前页面切回 CLI/mpv。
- 长期验证使用 `music_service_test.cpp`、`music_queue_test.cjs`、`music_seek_test.cpp` 和 `screenshot_regression.py` 的音乐场景；不要新建一个临时脚本来替代已有回归入口。`music_http_seek_clock` 使用生产 QML、真实 HTTP MP3 与独立线程服务器，验证拖动预览、歌词跳转、缓存下载期间原流保持、完整缓存后本地切换/定位、缓存复用及约 1× 时钟。host 默认 AudioOutput 增益为零；Pi 可设置 `TERMINAL_MUSIC_TEST_OUTPUT_GAIN` 低增益音测试，并验证本地预热时输出增益归零、定位后恢复；不写系统主音量或真实账号。运行前检查活动 sink-input，不能打断用户正在听的媒体；检测到音频设备也不能替代真实歌曲的人耳验收。Pi 可用 `TERMINAL_MUSIC_TEST_FIXTURE` 指定 PC 预生成 MP3，避免在低内存设备启动编码器。

YesPlayMusic 源码核对结论：`cacheTrackSource()` 将整首音源下载为 ArrayBuffer 写入 IndexedDB，读取后生成 Blob URL；`_cacheNextTrack()` 预取下一整首。它没有任意时长的片段缓存，`seek()` 本身仍交给 Howler。当前实现参考其整曲缓存粒度，只在首次 seek 时按需下载。Pi Qt 6.4.2/GStreamer 候选正式库通过拖动、真实歌词委托、缓存落盘后本地目标播放、持久缓存复用和 1× 时钟测试，随后已部署。测试增益 8%，系统主音量/静音不变；人耳确认真实线上歌曲仍需现场完成。参考文件：`third_party/YesPlayMusic/src/utils/db.js`、`src/utils/Player.js`、`src/views/lyrics.vue`。

## 系统音量与播报优先

- 快捷面板“系统”卡片右侧新增“音量”，点击展开系统主音量滑块（0–100%）和静音按钮；沿用现有卡片及 Google 官方 `volume_up` / `volume_off` SVG，不引入网页或新模糊算法。布局遵循 [Material slider](https://m3.material.io/components/sliders/overview) 的连续调节、数值标签和触控语义，并参考 [Google Material Web slider 文档](https://github.com/material-components/material-web/blob/main/docs/components/slider.md)，保留至少 48px 的交互高度。
- `SystemVolume` 属于 `terminal_system`，通过用户会话的 `pactl` 控制 `@DEFAULT_SINK@`，兼容 PulseAudio 和 PipeWire-Pulse；不使用 sudo，不持久化一份虚假的系统音量。命令异步、2.5 秒超时，拖动写入按 80ms 合并；打开面板/卡片和面板可见时每 5 秒读取真实状态。缺少 pactl、音频服务或默认输出时明确显示不可用，禁用调节，后续打开可重试。目标机实际验证和启用指纹读 `deployment-20261018.md` 及当前结构化报告，自动验证不等于人耳听感验收。
- 设置 → 声音与语音 → “播报时降低媒体音量”，默认开启并写入 QSettings 的 `duckMediaForSpeech`。开启时，现有 Qt TTS / espeak-ng 播报期间，本终端音乐与视频输出为当前用户媒体音量 × 0.2，以 120ms 短渐变切换；播报结束、失败、超时或被关闭时恢复。关闭则不压低媒体。不会降低播报自己的输出或改变系统主音量。
- 播报不调用媒体 pause/play，也不保存并回写旧音量，因此用户期间暂停、静音、切曲或改音量后不会被自动恢复逻辑覆盖。连续 espeak 播报保持压低直到待播内容结束。本功能仅协调本终端两个播放器与现有语音播报，不截获其他应用/桌面的提示音，也不新增暂停策略或提示音播放器。
- 本机回归以 `runtime_test.cpp` 的系统音量边界/超时和播报生命周期，以及 `screenshot_regression.py` 的 `volume-{zh,en}`、`sound-settings-{zh,en}`、`speech-duck-{True,False}`、`speech-duck-video` 为入口。host 使用假 pactl/espeak，不改开发机主音量；同时读取真实 QML AudioOutput 增益，并验证播报结束后的新音量、暂停和静音不被覆盖。这些不等于实机音频听感通过。
- Pi 实际缩放可能使 `48*uiScale` 只有 44px；新音量滑块和按钮以最终至少 48px 为下限。慢场景的测试音视频长度随请求运行时间增加，避免媒体先播放结束再验证播报优先。真实主音量写入验证之后还原默认输出各声道的原始数值与静音，不把四舍五入后的百分比当作精确恢复。

源码参考目录 `third_party/YesPlayMusic` 保留 MIT LICENSE，**不参与终端构建，也不部署到树莓派**。此前尝试安装的 Web/Electron 依赖仅在开发机，不是终端依赖。
