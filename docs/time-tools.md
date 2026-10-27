# 时间（时钟 · 计时器 · 秒表 · 闹钟）

内置应用页 `section 12`，入口在首页/应用抽屉的"时间"卡片。实现分四块：
`pi_terminal/qml/TimeTools.qml`（页面 UI）、`TimeMath.js`（纯函数 + 城市目录）、
`AlarmEngine.qml`（闹钟/计时器/整点报时的运行状态）、`TimeSync.qml`（校时与城市选择），
另加设置页里的 `TimeSyncSettings.qml` 分区与 `SystemTools` 上的一对设置读写接口。

## 范围

| 模块 | 行为 |
| :--- | :--- |
| 时钟 | 指针小钟在上、数字钟（时:分:秒）在下，日期与星期再下一行；指针表盘**随白天/夜晚变浅/变深**（设置里可改常白/常黑/跟随）；表盘右侧的喇叭按钮播报当前时间。不再提供「数字 / 指针」二选一（`time/clock-style` 键废弃） |
| 世界时钟 | 20 个城市目录，可勾选/取消，最多 8 个；每个城市一块：**指针小时钟在上、城市名在下**（不再显示 UTC 偏移）；未校时用内置标准时区表；校时入口在设置页（见下）；卡片内一行说明夏令时未知 |
| 计时器 | 一行 6 个紧凑预设（1/3/5/10/15/30 分）+ 一排 **类 iOS 轮盘**（时/分/秒 三个 Tumbler）；**运行状态归 AlarmEngine**，页面关掉也到点弹通知并语音播报 |
| 秒表 | 开始/暂停/继续、计次（**不限条数**，列表在卡片内滚动）、复位；主数字精确到 0.1 s |
| 闹钟 | 最多 8 个；每天/仅一次；可设名称、铃声、响铃时长与再响间隔；到点弹通知横幅 + 全局响铃弹窗（停止/再响） |
| 整点报时 | 开关 + 起止小时（0–23，可跨午夜，含两端）；只在 :00:00 播报一次；闹钟响铃或再响等待期间跳过 |
| 校时 | 设置页「时间与报时」分区做成系统「日期和时间」的形态：**自动设置日期和时间**开关（默认开，启动静默校时一次、之后每 6 小时一次）、只读的日期时间/时区、自动选线路开关、时间格式（24 小时 / 上午·下午）；自动校时失败不弹窗，只有手动「立即校时」失败才说明原因 |

## 双栏布局与滚动

**默认就是双栏**（类平板 UI），只有窄到放不下才退回单栏（左栏在上、整页滚动）：

    twoPane = width >= 760 * uiScale

760 是 1.0 倍下的设计宽度；乘 `uiScale` 就是严格按作者唯一的缩放边界换算
（`autoScale ∈ [0.65, 1.4]` × 界面比例 `{1, 1.25, 1.5}` = `Theme.scaleFactor`）。
这样每一档缩放的相对容量一致，不会"缩小时反而更容易变单栏"：

| 场景 | 页面宽 | uiScale | 断点 | 结果 |
| :--- | ---: | ---: | ---: | :--- |
| Pi 1024×600 窗口 | 800 | 0.75 | 570 | 双栏 |
| 桌面 1400×900 窗口 | 1097 | 1.09 | 828 | 双栏 |
| 大字档（textScale 1.5，页面 1024） | 1024 | 1.5 | 1140 | 单栏 |
| 窄窗口 | 700 | 1.0 | 760 | 单栏 |

不看高度：短屏也保持两栏，正文放不下就在卡片内部滚动（真机视口下卡片 253px、正文 129px，
时钟卡与城市列表都靠内部滚动）。

两栏模式下**卡片撑满所在行，正文在卡片内部滚动**；页面本身不滚。这条约束由三处代码共同保证，
改任何一处都要重跑 `twoPanePanesFillViewport`：

1. 页面 `column.height = flick.height`（不是 `max(视口, 内容高)`）；
2. `Loader` 在双栏时 `Layout.minimumHeight: 0`，否则它的隐式高度（=整页内容高）会被当成最小高度；
3. `TimePane` 在 `scrollBody` 时 `Layout.minimumHeight: 0`，否则 `Layout.fillHeight` 会被下限顶回去。

`TimePane` 的正文预算 `bodyTop = headerHeight + gap*1.5`、`bodyBottomInset = gap`，隐式高度与内部
`Flickable` 用同一套账；两者不一致时单栏"按内容高"的卡片自己都会溢出半格。溢出超过
`4 * scaleFactor` 才认定"可滚动"，并常驻一条自绘细指示条 + 底部渐隐——控件样式的 `ScrollBar`
静止时会淡出，屏上等于没有提示（世界时钟末行半截最像 bug）。

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
| `time/format` | 时间格式：`24` / `12`（上午·下午，城市钟同样跟随） |
| `time/sync-auto` | 自动校时：`1`（默认）/ `0` |
| `time/cities` | 已选城市（IANA 时区 id 数组） |
| `time/chime` | 整点报时：`{"enabled":bool,"from":0-23,"to":0-23}` |
| `time/clock-face` | 指针表盘配色：`auto`（跟随白天/夜晚，默认）/ `light`（常白）/ `dark`（常黑） |

三个状态拥有者（`TimeTools` / `TimeSync` / `AlarmEngine`）的 `loading*` 标志**初值都是 `true`**：
QML 在 `Component.onCompleted` 之前就会把属性初值写进属性，若那时允许持久化，默认值会先覆盖
磁盘设置、`load()` 再读回默认值，表现为"重启后设置全丢"。`storedSettingsSurviveConstruction`
把这条顺序钉住了。

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

## 卡片头部与轮盘

- **头部一行**：主标题在左、小字（副标题）在右。主标题 + 副标题 + 间距放不下时，副标题
  **整条不显示**（例如真机宽度下的「新建闹钟」「中国时间」），而不是把主标题挤成「中国…」。
  原先小字各占一行，标题区高 90px 以上，双栏短屏下正文只剩一百多像素。
- **计时器轮盘**：`Tumbler` 三列一行，每列上方标出「时 / 分 / 秒」，`visibleItemCount: 3`、
  `wrap: true`，中间一条选择带，距离中心越远越淡。轮盘首次布局会自己滚一下
  （`currentIndex` 抖动），那不是用户操作：组件只在 `moving || flicking`（用户真的在滚）
  时才回写模型，另外还有 600ms 的 `ready` 兜底。否则「预设 5 分钟」会被改成乱值
  （真机上已复现、污染过 `time/timer-preset`，现由 `timerWheelsDoNotChangeValuesOnTheirOwn` 守住）。
- **闹钟时间选择**：一行两列——「时」「分」两列各自「标签在上、紧凑步进器在下」
  （`StepperBox.compact`：按钮 42px、数字框 48px），比原来的两行占满窄一半。

## 铃声片段的 15 秒截取

自选铃声可以指定片段起点：`AlarmEngine.ringClipMs = 15000`，记录字段 `ringtoneStartMs`
（0–300000，5 秒步进，随闹钟持久化）。

- 响铃：先载入音源、`LoadedMedia` 后 seek 到起点再播；15 秒到点由 `clipTimer` 回到起点重播
  （`loops: 1`，不再让 `MediaPlayer` 自己循环整个文件），直到 `ringSeconds` 到点。
- 试听：同样从起点播，但只播一遍，15 秒后自动停。
- 内置铃声本身就是 15 秒节选，所以「片段起点」行只在选了自定义文件时出现；
  编辑已有闹钟会把起点回填到表单。
- `ringtoneClipOffset` 覆盖：记录落盘、响铃偏移、试听偏移、5 秒步进与 0/5 分钟上下限、
  内置铃声不显示该行。

## 指针表盘的白天/夜晚配色

`AnalogClock` 有一个 `daytime` 判定：`06:00–17:59` 算白天。白天盘面浅色（`#f1f4f8`）+
深色刻度与指针（`#101a26`），夜间反过来（`#0a1119` / `#e8f1ff`），切换带 400ms 过渡。
判定输入是**该表盘自己的小时数**，所以城市钟按各自城市的时间走（半夜看北京是深色、
看纽约可能是浅色）。设置页「时间与报时」里可改成常白 / 常黑 / 跟随（`time/clock-face`，
由 `Main.qml` 持有并持久化，时间页与设置分区共用）。

## 窄卡片的排版约束

双栏后每栏只有 340px（真机视口），所有左栏一律**竖排、标签在左控件在右**，任何一行都不得
排到卡片外：

- 计时器：预设压成**一行 6 个**紧凑胶囊（字号 caption、高度 36），下面是轮盘。
  早先预设列数按**页面宽度**算（`root.width < 620`），340px 卡片里被塞成 6 列，
  「15/30 分钟」直接跑到卡外；三组步进器挤在一行时「秒」也会被挤出去。
- 闹钟：时/分 各占一行；铃声拆成「名称一行 + 默认/试听/选择铃声一行」。
- 报时入口：整块卡片可点会盖住样式按钮（报时 `MouseArea` 声明在 `Column` 之后，
  压在上面），改成表盘右侧 48px 的喇叭按钮，样式按钮恢复响应。

`analogClockFollowsDayAndNight` 覆盖：自动模式 12 点浅盘 / 23 点深盘、常白与常黑覆盖时刻、
以及每个城市钟按自己的小时数决定深浅。`paneContentStaysInsideCard` 在真机视口（800×365、卡片 340）下遍历四个页签的每张卡片，
断言任何可见控件的左右边界都不越过卡片边界；`clockStyleButtonsAreClickable` 用真实鼠标
点击验证「数字/指针」真的会切样式（回归上面那个热区盖住按钮的 bug）。

## 字号与 token

`CarText/CarButton.fontPixelSize` 是**像素语义**（内部换算成 pt），`Theme.body/title/hero`
是 **pt 语义**，两者差约 1.33 倍，不能互相替换。时间页的说明文字、城市时间、读数统一走像素
通道，用的是 `Theme.fontCaption/fontLabel/fontStrong/fontDisplay`（= 12/13/22/24 ×
`scaleFactor`）。响应式读数（数字钟、计时器、秒表）保留 `min(上限 × uiScale, 宽度/N)` 的算式，
它们的上限本来就由可用宽度决定。

## 闹钟点行回填编辑

点「已设置闹钟」里的任意一行 → 左栏回填该条，标题变「编辑闹钟」，按钮变「保存修改 / 取消」：

- 保存走 `AlarmEngine.updateAlarm(index, record)`：**就地修改**，保留原来的启用状态；
  时刻+名称与其他条目重复时拒绝写模型并提示，仍留在编辑态。
- 取消回新建态，表单复位为默认（07:30、每天、默认铃声、30s、5min）。
- 删除正在编辑的那条 → 自动回新建态；删除它前面的条目 → 编辑编号前移一位，仍编辑同一条
  （靠引擎的 `alarmRemoved(index)`，不依赖 ListModel 的 `countChanged`）。

## 校时：让用户无感

校时的状态与请求都在 `TimeSync.qml`（`Main.qml` 实例化一次）。**用户不需要点「校时」**：
`autoSync` 默认开启，启动 6 秒后静默同步一次，之后每 6 小时一次；失败只更新状态行
（「尚未校时」/「上次校时未成功」），不弹窗、不打断。设置页只暴露系统设置那种形态：

- 自动设置日期和时间（开关）+ 只读的「日期和时间」
- 自动选择校时线路（开关；关掉即自定义服务器，出现地址输入框）
- 只读的「时区」（终端不修改系统时区，如实显示本机偏移）
- 时间格式：24 小时 / 上午·下午（时钟数字与城市钟同步切换）
- 状态行 + 「立即校时」：手动失败才弹窗（每条线路失效都说明原因），仍不新建 Window/Popup 层级

时钟页只显示城市列表与「管理城市」，不复述校时状态。

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
显式错误、自定义模式不回退公共接口、闹钟与计时器预设跨页面重建后仍存在。

本轮新增：`twoPanePanesFillViewport`（两档视口 × 四页签：并排、等高、底边不越视口、正文可滚）、
`twoPaneBreakpointFollowsScale`（真实 Pi 视口 800@0.75、760/700、1024/1400 与 0.65/1.5 缩放的
断点矩阵）、`alarmRowEditFlow`（真鼠标点行 → 回填 → 就地保存保留启用位 → 取消 → 冲突拒绝 →
删条收干净）、
`paneScrollAffordanceIsVisible`（App 真实视口 1097×502 下必须出现指示条与渐隐，滚到底渐隐消失，
假溢出不得画滚动条）、`hourlyChime`（整点一次、时段含两端/跨午夜、响铃跳过、持久化）、
`settingsSectionDrivesOwners`（模式/地址/状态/开关写的都是 TimeSync 与 AlarmEngine）、
`storedSettingsSurviveConstruction`（构造顺序不覆盖磁盘设置）。
`runtime_test.pageSettingsArePrefixScoped` 覆盖设置接口的前缀、长度与大小上限。

**这不是实机验收。** 板端仍需按 `docs/pi-ui-regression.md` 的既有入口跑
`screenshot_regression.py`（本页已加入 `language-{zh,en}-12` 场景），并在 1024×600 实机上确认
48px 热区、触摸、TTS 实际发声与真实上游接口可用性。

## 图标

新增 5 个官方 Material Icons Round（`schedule` / `alarm` / `hourglass_empty` / `av_timer` /
`public`），由 `pi_terminal/tools/fetch_material_icons.py --only ...` 生成，原始 URL 与 SHA256
记录在 `qml/icons/material/SOURCES.json`，双色变体随包发布，运行时绝不联网取图标。
