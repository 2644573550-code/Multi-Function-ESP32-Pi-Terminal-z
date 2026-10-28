# 调试钩子与自动化入口（团队共用）

> 本文列的开关都是**给开发和自动化测试用的**，不是产品功能。
> 生产启动不要设置这些环境变量、不要带这些命令行参数。
>
> 新增钩子请同时更新本文，否则别人 debug 时不知道有这个东西。

---

## 1. 命令行参数

| 参数 | 作用 | 注意 |
|---|---|---|
| `--config <path>` | 指定产品配置 JSON（模块 profile、包公钥、缓存目录） | 不传则用默认位置 |
| `--demo` | 挂模拟模块、注入虚拟热点租约、桌面调试项可见 | **不会**自动登录、**不会**自动信任 |
| `--ui-test` | 自动走一遍动态页/动作/断开，用于 GUI 集成审计 | 会关闭登录门禁并注入 `ui-test-user` |
| `--ui-audit <dir>` | 跑响应式布局与触摸注入测试，产物写到目录 | |
| `--quit-after <ms>` | 到点自动退出 | 截图/回归脚本常用 |
| `--screenshot <path>` | 退出前截图 | 与 `--quit-after` 配合 |
| `--open-module <instance>` | 等该模块 `ready` 后自动打开它的页面 | 需要 `phase === "home"` 才会成功 |
| `--invoke-action <action>` | 打开模块页 2.5 秒后触发一个动作 | 与 `--open-module` 连用 |
| `--preview-page <page>` | 直接打开内置页面做版式检查 | 仅内置页面，不含模块页 |
| `--init-admin <name>` | 建第一个管理员（注册表非空即拒绝） | 恢复码只打印一次 |
| `--init-pin-stdin` | 初始 PIN 从 stdin 读，不走 TTY 提示 | 自动化用 |
| `--metrics <path>` | 退出时写帧率与内存指标 | |
| `--paint-test` | 打开画板并报告画布几何 | 会自动确认首次信任 |
| `--app-drawer-test` | 打开应用抽屉并报告几何 | |
| `--keyboard-test` | 打开悬浮键盘并验证中文候选 | |
| `--window-animation-test` | 审计卡片翻页与窗口尺寸动画 | |
| `--quick-panel-test` | 打开快捷面板 | |
| `--notification-test` | 弹一条诊断通知并试中文播报 | |
| `--notification-layout-test` | 快捷面板里显示示例通知卡 | |
| `--notification-banner-test` | 显示一条应用风格通知横幅 | |
| `--render-benchmark` | 持续动画首页轮播，用于渲染测量 | |
| `--window-size <WxH>` | 指定初始逻辑窗口尺寸 | 布局回归常用 |

---

## 2. 环境变量

### 2.1 演示与登录

| 变量 | 作用 | 风险提示 |
|---|---|---|
| `ORANGE_PI_AUTO_LOGIN=<pin>` | 启动时用**第一个**用户自动登录 | 明文 PIN 在进程环境里可见；只在本机演示/自动化用 |
| `ORANGE_PI_AUTO_TRUST=1` | 跳过模块首次信任弹窗，直接加载 | 等于自动信任任意已签名的包；只在受控演示用 |
| `ORANGE_PI_CONTROL_DEVICE=<tty>` | 指定 PBR1 控制串口 | 演示时传模拟桥板的 PTY 路径 |

`ORANGE_PI_AUTO_LOGIN` 配合 `--init-admin` 可以做出"打开就是桌面"的一次性环境，
`tools/demo_dual_module.py` 就是这么跑的。

### 2.2 模块加载

| 变量 | 作用 |
|---|---|
| `ORANGE_PI_MODULE_INPROCESS=1` | 回退到同进程 `QLibrary` 加载，不起 worker 子进程 |
| `ORANGE_PI_MODULE_WORKER=<path>` | 指定 worker 可执行文件 |
| `ORANGE_PI_WORKER_SOCKET=<path>` | 指定 worker 的 Unix socket |
| `ORANGE_PI_WORKER_SESSION=<id>` | worker 会话标识 |
| `ORANGE_PI_WORKER_DATA_DIR=<path>` | worker 的私有数据目录 |

`INPROCESS=1` 是排障用的：worker 起来不的时候先切回同进程，确认是模块本身
的问题还是 IPC 的问题。**生产不要设**，那等于放弃隔离。

### 2.3 热点与网络

| 变量 | 作用 |
|---|---|
| `ORANGE_PI_HOTSPOT_SUDO=1` | 热点操作用 sudo 提权 |
| `TERMINAL_GST_*` | 音视频播放的队列/连接参数调优 |

### 2.4 语音

| 变量 | 作用 |
|---|---|
| `TERMINAL_FLITE_BIN=<path>` | 指定 flite 可执行文件 |
| `TERMINAL_TTS_ROOT=<path>` | 指定音色/词典根目录 |
| `TERMINAL_TV_AUTOPLAY=1` | 电视页自动播放（调试用） |

这些在 `src/ui/SystemTools.cpp` 的音色发现逻辑里读，测试用它们指向临时目录。

---

## 3. 模拟器（`pi_terminal/tools/`）

| 脚本 | 用途 |
|---|---|
| `mock_bridge.py` | 单槽 PBR1 桥板模拟，真 PTY |
| `mock_bridge_multi.py` | **双槽 + 电池遥测 + 刷卡**的桥板模拟 |
| `mock_module.py` | 模块业务端点模拟（TCP，可选 TLS），含 82/83 数据 |
| `demo_dual_module.py` | 端到端演示：双槽 + 桥板 + 电池 + 打包 + 截图 |
| `package_tool.py` | 打包/验签/装包工具（也用于手工造包） |

### 3.1 演示怎么跑

```bash
# 先按 docs/devdocs/02-* 的构建步骤编出宿主与两个示例模块
python3 pi_terminal/tools/demo_dual_module.py \
  <build-dir> /tmp/demo-out \
  --fence-so <xiaotian-fence>/libmodule.so \
  --bp-so <blood-pressure>/libmodule.so
```

它会打印各阶段状态，并在 `/tmp/demo-out/shot.png` 留下截图。

### 3.2 模拟器要点（踩过的坑）

- 模块 `.so` **内嵌 manifest 与包内 manifest 必须等价**，否则
  `embedded manifest/ABI differs`。改完 manifest 要连带重建 `.so`。
- 包的 `arch` 必须等于 `uname -m`，宿主用
  `QSysInfo::currentCpuArchitecture()` 校验，否则 `descriptor incompatible`。
- LAN 业务面是 TLS，profile 必须带 `pin_sha256`（证书 DER 的 SHA-256），
  且证书 `CN = <module_id>`（规范 §0.8.1）。
- 围界模块要去抖：连续 2 帧同值才认可，心跳 9 秒过期。
  **模拟模块必须持续上报**，只回一帧会一直显示"状态未知"。

---

## 4. 模块页调试

模块页跑在独立 `QQmlEngine` 里，只能看到宿主注入的四样东西：
`sessionModel`、`moduleData`、`userContext`、`hostTheme`。

**两个最常见的坑（都栽过）**：

1. 模块页里写 `property var sessionModel: null` 会**遮蔽**注入的
   context property，页面永远拿到 null。页面不要声明与注入项同名的属性。
2. 数据接口**不能叫 `data`**：`QQuickItem` 自带 `data` 属性（子项列表），
   注入会被遮蔽，`data.appendSample()` 实际调到子项列表上。
   所以接口名是 `moduleData`。

主题与缩放从 `hostTheme` 取：

```qml
readonly property var theme: (typeof hostTheme !== "undefined" && hostTheme) ? hostTheme : ({})
readonly property real uiScale: (theme.scale > 0) ? theme.scale : 1
```

实时数据绑定：

```qml
readonly property var latestData: sessionModel ? sessionModel.data : ({})
onLatestDataChanged: applySample(latestData)
```

`SessionModel.data` 是 `NOTIFY changed` 的属性，直接绑就能收到更新；
不需要额外 `Connections`。

---

## 5. 相关测试

- `ctest -E "media_vlc|ui_responsive_touch|ui_highdpi_touch"` —— 主机侧全量
  （这三项加 `media_vlc_*` 在无设备环境会失败，属环境性）
- `pi_terminal/tests/screenshot_regression.py` —— 截图回归
- 模块自带测试：`module-devkit/examples/*/tests/`，各自 `ctest`
