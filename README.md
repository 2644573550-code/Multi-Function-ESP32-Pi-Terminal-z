# Multi-Function ESP32 Pi Terminal

面向 Raspberry Pi 与 ESP32-S3 模块的可扩展终端项目。Pi 侧提供原生 Qt/QML 桌面与模块运行时；模块通过固定 BLE 或局域网传输传感器数据，并可向终端提供独立功能页面。

> **项目状态：持续开发。** 下方路线图中的“已具备”仅表示仓库中已有实现或示例；“规划中”不是已交付功能。安全、隐私和硬件依赖项须经过专项设计与实机验证后才能标记完成。

## 项目组成

| 目录 | 内容 |
| --- | --- |
| [`pi_terminal/`](pi_terminal/) | Raspberry Pi 原生 Qt 6 / QML 终端、模块管理、文件与媒体功能 |
| [`module-devkit/`](module-devkit/) | 模块开发 SDK、模板、智能盆栽示例、配包工具及开发文档 |
| [`skills/`](skills/) | 项目维护 skill 与参考资料 |
| [`docs/`](docs/) | 架构、构建、浏览器、媒体和界面说明 |
| [`third_party/`](third_party/) | 项目使用的第三方参考代码及组件，许可信息见各自目录 |

ESP 固件主工程不在本仓库根目录内；`module-devkit/sdk/` 是模块 SDK 导出副本，并附有来源和 SHA-256 清单。ESP-IDF、Qt 交叉工具链及签名私钥需由开发者按本机环境单独安装或保管。

## 当前已有基础

- Pi 侧原生 Qt/QML 终端、模块会话管理和可加载模块运行时。
- 模块 C ABI、签名包校验、SDK、模板及 `smart-planter` 示例。
- 固定 BLE / 热点 LAN 传输模式；认证与会话就绪状态由协议校验。
- 示例模块包含环境读数、历史、电量显示、LCD 与软件低电量保护策略。供电来源检测取决于硬件接线；未知时不推断为外接电源或电池供电。
- Pi 终端现有的文件、浏览器、媒体、输入法和触控界面功能。具体能力边界与验证结果见 `docs/`。

## 路线图

路线图按依赖关系推进；优先完成安全、隐私和硬件边界，再扩展面向用户的模块能力。未标完成的事项均为规划或待验证工作。

### 0. 现有平台与开发体验

- [x] 建立 Pi 终端、模块 ABI、SDK / 模板及示例模块的基础结构。
- [x] 提供 BLE 与固定热点 LAN 的模块通信路径及签名包验证工具。
- [x] 提供环境模块示例：读数展示、历史、电量和软件供电保护策略。
- [ ] 完善跨机器开发说明：明确 ESP-IDF / Qt 版本、工具链安装、私有配置生成及硬件接线要求。
- [ ] 为同事提供脱离维护者个人路径、凭据和机器环境的可复现构建流程。

### 1. 安全、隐私与弱网基础（优先）

- [ ] 梳理威胁模型、数据分类、最小权限、身份生命周期、密钥轮换与安全更新策略。
- [ ] 为设备与用户数据定义本地存储、访问控制、保留期限、删除和备份策略。
- [ ] 设计受限网络 / 离线场景：明确缓存、重试、超时、断线恢复及数据冲突行为。
- [ ] 评估传输链路端到端保护；HTTPS/TLS 需结合证书校验、设备身份和密钥管理，不将“启用 HTTPS”单独视为完整安全方案。
- [ ] 明确威胁模型与更新签名后，再设计 OTA、回滚保护和故障恢复流程；不直接远程刷写未经验证的固件。

### 2. 身份与多用户数据隔离

- [ ] 评估 NFC / RFID 作为本地用户识别或卡片切换方式；定义丢卡、复制卡、解绑和离线处理规则。
- [ ] 设计终端内的用户登录 / 切换 UI 与会话状态管理。
- [ ] 设计按用户隔离的设置、记录和模块数据；明确设备共享、权限边界、导出与删除行为。
- [ ] 在存储结构和访问控制测试完成前，不宣称已支持安全的多用户数据隔离。

### 3. 可选模块与健康相关扩展

- [ ] 评估可插拔的安全围栏模块（截图中“module plan”项）；先定义定位来源、区域配置、误报处理、隐私与断网行为。
- [ ] 评估血压计等健康设备的数据接入、设备兼容范围、单位与测量有效性。
- [ ] 若实现健康数据功能，明确其为数据记录 / 展示还是医疗用途；完成法规、风险和隐私评估前，不提供诊断或治疗建议，也不宣称医疗器械功能。
- [ ] 为新增模块定义权限声明、数据模式、兼容性和可撤销授权流程。

### 4. 电源与交互硬件

- [ ] 评估桥板电池电量管理：电量计、供电来源检测、低电量策略、校准和异常读数处理。
- [ ] 评估双触点握手 / 输入方式，并验证去抖、误触、静电和耐久性。
- [ ] 在真实硬件上验证低电量、外部供电切换、断连及安全关机行为；软件电量读数不能替代硬件保护电路。

### 5. 安全更新与发布

- [ ] 制定签名密钥托管、发布授权、设备升级认证和密钥轮换流程。
- [ ] 设计分阶段 OTA、断电恢复、版本兼容和防降级策略。
- [ ] 建立固件 / 模块包来源、哈希、签名和发布记录的可审计流程。
- [ ] 在升级与恢复测试通过前，OTA 保持未交付状态。

## 开发入口

### Raspberry Pi 终端

构建需 Qt 6 开发环境及与目标系统相匹配的工具链 / sysroot。常见构建入口：

```sh
source pi_terminal/scripts/env.sh
cmake -S pi_terminal -B /tmp/pi-terminal-build \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build /tmp/pi-terminal-build --parallel
```

交叉编译及测试细节见 [`docs/native-runtime-modules.md`](docs/native-runtime-modules.md)、[`docs/pi-ui-regression.md`](docs/pi-ui-regression.md) 和 `build_scripts/`。

### 模块开发

从仓库根目录进入 `module-devkit/`，先检查本机工具链，再初始化模块：

```sh
cd module-devkit
python3 module_devkit.py doctor
python3 module_devkit.py init /path/to/my-module --module-id 200 --name "My Module"
python3 module_devkit.py validate /path/to/my-module
```

从零搭建、签名包和 ESP 固件变体的说明见 [`module-devkit/README.md`](module-devkit/README.md)。构建所需的私有 `sdkconfig`、包签名私钥和产品凭据应在本机安全配置，不要提交到版本库。

## 参与方式

欢迎通过 Issue 讨论需求，并以 Pull Request 提交改动。`main` 的实际审查要求以 GitHub 仓库当前启用的 Ruleset / 分支保护规则为准；请在合并前确认规则处于 **Active**，且目标分支为 `main`。硬件相关改动请附上板卡、接线、固件配置和可复现验证说明，不要在 Issue / PR 中公开密钥、令牌、Wi-Fi 密码或设备个人数据。

## 文档索引

- [模块 SDK 与智能盆栽示例](module-devkit/README.md)
- [项目代码地图](docs/project-code-map.md)
- [原生运行时模块与构建](docs/native-runtime-modules.md)
- [界面回归与验证边界](docs/pi-ui-regression.md)
- [浏览器移植范围](docs/viper-browser-port.md)
- [项目维护 skill](skills/esp32-pogo-debugger/SKILL.md)
