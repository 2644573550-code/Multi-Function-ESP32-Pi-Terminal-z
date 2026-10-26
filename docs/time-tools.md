# 时间（时钟 · 计时器 · 秒表 · 闹钟）

内置应用页 `section 12`，入口在首页/应用抽屉的"时间"卡片。实现是两个新文件：
`pi_terminal/qml/TimeTools.qml`（页面）与 `pi_terminal/qml/TimeMath.js`（纯函数），
另加 `SystemTools` 上的一对设置读写接口。

## 范围

| 模块 | 行为 |
| :--- | :--- |
| 时钟 | 本机时间（系统时钟）、日期、星期、`UTC±HH:MM`；可在**数字 / 指针（表盘）**两种样式间切换，选择持久化 |
| 世界时钟 | 20 个城市目录，可勾选/取消，最多 8 个；未校时用内置标准时区表；点"网络校时"才请求公开时间接口；每行可语音播报 |
| 计时器 | 1/3/5/10/15/30 分钟预设或时/分/秒步进；**运行状态归 AlarmEngine**，页面关掉也到点弹通知并语音播报 |
| 秒表 | 开始/暂停/继续、计次（最多保留 20 条）、复位；主数字精确到 0.1 s |
| 闹钟 | 最多 8 个；每天/仅一次；可设名称、铃声、响铃时长与再响间隔；到点弹通知横幅 + 全局响铃弹窗（停止/再响） |

## 闹钟引擎在页面之外

`pi_terminal/qml/AlarmEngine.qml` 在 `Main.qml` 里实例化一次，**时间页关掉也照常计时**：
页面只是它的视图。引擎负责闹钟数据、1 秒 tick、响铃、再响与状态坞图标的状态。

- 响铃 = `notify()` 通知横幅 + `announce()` 语音播报 + 全局弹窗（停止 / 再响）；
  弹窗由引擎持有，因此不在时间页也能操作。
- 铃声用 QML `MediaPlayer` + `loops: Infinite` 循环播放，时长到点自动停。
  这是终端里**第三个音源**（音乐、视频之外），播报降音只作用于音乐/视频，不会压低闹钟。
- 默认铃声是随包内置的 15 秒节选（`qrc:/ringtones/default-ringtone.mp3`），新建闹钟默认用它；
  UI 显示为「默认铃声（15 秒）」或所选文件的文件名。素材来源与授权注意见
  `pi_terminal/assets/ringtones/NOTICE.md`：原曲版权不属于本仓库，公开发布前应替换。
- 铃声来自 `SystemTools.files()` 的目录枚举——复用文件管理器的沙箱，不复制也不修改它的代码；
  只列出 mp3 / wav / m4a / ogg 四种常见格式。受沙箱限制，选择范围就是文件管理能看到的路径。
- 状态坞在键盘按钮左侧显示闹钟图标（`alarmActive`），全部闹钟关闭后消失；点击跳到闹钟页。
- 闹钟名用项目自带 `CarField` + `FloatingKeyboard`：点一下触发 `keyboardRequested()`，
  由 `Main.qml` 调 `screenKeyboard.show()`，输入法代码未改动。
- 时间数字（计时器时/分/秒、闹钟时/分、时长、再响）除了 ± 按钮，**数字本身可点**：
  聚焦 `TextInput` 后同样弹出内置键盘，使用 `ImhDigitsOnly` 与 `IntValidator` 限制范围。
  键盘只在“点数字/点输入框”时请求，**不跟随焦点变化**——否则键盘关闭、焦点回弹会
  再次触发，表现为键盘关了又开、一直循环。
- 响铃时长与再响间隔各自与自己的数字框同一行（标签 + 框 + 单位），不再出现“共用选择器”的观感。

## 持久化

以下状态写入 `QSettings`，键名统一在 `time/` 前缀下，重启终端后恢复：

| 键 | 内容 |
| :--- | :--- |
| `time/alarms` | 闹钟数组的 JSON（时间、每天/仅一次、启用、名称、铃声、响铃时长、再响间隔） |
| `time/clock-style` | `digital` 或 `analog` |
| `time/repeat` | 新建闹钟时"每天"开关的默认值 |
| `time/timer-preset` | 计时器上次的时/分/秒（400 ms 合并写入） |
| `time/api-mode` | `auto` 或 `custom` |
| `time/api-host` | 自定义校时服务器地址 |
| `time/cities` | 已选城市（IANA 时区 id 数组） |

计时器运行状态、秒表读数、计次列表**不持久化**（重启即清空），这是刻意的：
恢复一个"看起来还在跑"的计时器只会误导。

## 布局约定

- **标签页懒加载**：四个标签页由 `Loader` 按需实例化，`Flickable.contentHeight` 等于当前页高度加标签行。
  早期用 `StackLayout` 时它的隐式高度会残留，滚到底部会出现一整屏空白；回归用例把
  "contentHeight − 当前页高度 < 200"写成了断言。
- **数字进容器**：计时器的时/分/秒、倒计时、秒表读数与闹钟时间都放在
  `Theme.surfaceContainerHigh` 圆角容器里（Material 3 的 contained 数字输入），
  `+/-` 是 48px 图标按钮，位于同一容器内。

`SystemTools::settingsRead/settingsWrite` 只接受 `time/` 前缀、键名 ≤96 字符、值 ≤16 KiB。
`trusted/*`（插件信任记录）与宿主偏好同在一个 QSettings 文件里，页面脚本必须够不到它们，
这条前缀白名单就是那条边界；`runtime_test` 的 `pageSettingsArePrefixScoped` 覆盖了拒绝路径。

## 世界时钟的网络行为

默认顺序（自动模式）：`worldtimeapi.org` → 失败整轮改用 `timeapi.io`。
接口名后面跟一个状态点：灰=本次未测、绿=可用、红=不可用。红点与弹窗状态**只在一次同步内有效**，
重新点"网络校时"会清空重测——避免一次断网把接口永久标红。

失败时：页面内弹出提示（不新建 Window/Popup 层级），写明是哪个接口、什么原因、是否已切换；
若全部失败则保留内置时区并显式报错，不写入猜测值。

**自定义服务器**（自定义模式）需要兼容 worldtimeapi.org 的返回格式：
`GET /api/timezone/<区域>` → 至少包含 `utc_offset`（"+08:00"）与 `unixtime`。
自定义模式**严格只用自定义地址**，失败不会静默改用公共接口（与项目"传输模式不回退"的既有原则一致）；
弹窗会提示可切回自动后重试。

- 触发条件：只在你点"网络校时"时发生，页面加载与定时刷新都不联网。
- 请求规模：自动模式 = 已选城市数（≤8）+ 可能的一轮备用接口；自定义模式 = 已选城市数。每次 8 秒看门狗。
- 不携带 cookie、token、设备标识；不读取返回里的客户端 IP 字段。
- 上游可用性、限流与许可由服务方决定，本项目不作保证。

## 与既有功能的边界

- **不改核心链路**：主题、模糊渲染（`BlurItem`/`BlurCache`/`CachedBlur`）、连接与会话、
  媒体管线、浏览器 profile 一行未动。
- **不新建全局 UI 层**：计时结束/闹钟复用 `Main.qml` 已有的 `notify()` 通知横幅与 `announce()`
  语音播报；唯有校时失败提示是页面内覆盖层（离开页面就不可见，且只在用户主动点校时后出现）。
- **`Main.qml` 只改三处**：路由表、首页卡片顺序、一个页面实例 + 两行信号接线。
- **闹钟精度**：按分钟匹配，页面隐藏时定时器降到 1 Hz 仍会响；终端休眠或长时间卡顿跨过目标分钟则不补响。
- **不做系统级闹钟**：不注册 systemd timer、不改系统时间、不新增铃声资源；提示是通知横幅 + 中文 TTS
  （走既有播报降音通路）。

## 验证

```sh
# 纯函数：直接执行 TimeMath.js（与页面共用同一份实现）
node pi_terminal/tests/time_math_test.cjs

# 页面与设置边界（离线，本地 HTTP 桩，不需要真实网络）
cmake -S pi_terminal -B /tmp/terminal-dev -DBUILD_TESTING=ON
cmake --build /tmp/terminal-dev --target time_tools_test runtime_test --parallel 4
ctest --test-dir /tmp/terminal-dev -R 'time_tools_page|runtime_models_state_files' --output-on-failure
```

`time_tools_test` 覆盖：时钟格式化、四个标签切换后**当前页对象、高度与滚动区**、城市增删与上限、
跨页面重建后的城市选择、计时结束与闹钟信号、
"每钟只响一次"、世界时钟成功同步（6 次请求 + 偏移写入 + provider 文案）、双接口全失败时的
显式错误与提示层、自定义模式不回退公共接口、闹钟与计时器预设跨页面重建后仍存在。
`runtime_test.pageSettingsArePrefixScoped` 覆盖设置接口的前缀、长度与大小上限。

**这不是实机验收。** 板端仍需按 `docs/pi-ui-regression.md` 的既有入口跑
`screenshot_regression.py`（本页已加入 `language-{zh,en}-12` 场景），并在 1024×600 实机上确认
48px 热区、触摸、TTS 实际发声与真实上游接口可用性。

## 图标

新增 5 个官方 Material Icons Round（`schedule` / `alarm` / `hourglass_empty` / `av_timer` /
`public`），由 `pi_terminal/tools/fetch_material_icons.py --only ...` 生成，原始 URL 与 SHA256
记录在 `qml/icons/material/SOURCES.json`，双色变体随包发布，运行时绝不联网取图标。
