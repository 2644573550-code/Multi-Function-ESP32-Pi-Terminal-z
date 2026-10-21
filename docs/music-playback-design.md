# 音乐播放调度：当前实现与兼容代码

当前播放路径以 `docs/native-music.md` 和实际源码为准，不再通过官方 CLI/mpv 播放。

## 当前路径

1. `MusicService` 使用 Qt Network 请求第三方 API，区分浏览、登录和音源解析请求，取消或切歌后拒绝旧结果。
2. `MusicPlayer.qml` 用独立队列快照播放当前页；搜索、翻页不修改正在播放的队列。切歌先停止旧播放器，再解析并设置新音源。
3. 在线、离线均交给 Qt Multimedia。播放状态、时长和进度来自真实 `MediaPlayer`，不可把请求返回成功当成起播成功。
4. Qt 6 QML 拖动预览与实际跳转分离，释放后写入毫秒单位的 `position`；切歌取消旧跳转，不调用不存在的 QML `seek()`。
5. 运行时拒播或超时显示错误，不无限递归跳歌；未实现音频磁盘缓存和下一首预取。
6. 保留上下首和队列；用户已移除随机、顺序/循环按钮，不应因残留内部状态或算法参考而恢复这些按钮。

参考思想来自 `third_party/YesPlayMusic/src/utils/Player.js`，只借鉴队列/事务隔离等思路；参考项目不参与终端构建或部署。

## 兼容路径不是当前页面实现

`MusicPlayback.cpp`、`SystemTools` 中仍有早期 CLI 兼容代码，编入 `terminal_music`/`terminal_system`。
其 generation、停止确认、状态查询隔离由 `music_playback_test.cpp` 覆盖。
不得将这些兼容测试、可选 `NCM_LIVE_TRACKS` 测试或类名当成产品仍依赖 CLI 的依据；
也不要在未检查调用者和测试前删除兼容代码。

## 验证入口

- `music_service_test.cpp`：API 请求隔离及过期音源抑制。
- `music_queue_test.cjs`：提取 QML 队列算法测试。
- `screenshot_regression.py`：列表、队列、歌词、普通/节能配色与后台媒体控制。
- `music_playback_test.cpp`：兼容 CLI 事务测试，使用假进程，不需要真实账号。

账号、Cookie 和会话凭据只保存在应用私有配置中，不写入文档、截图说明或 skill。
