---
generated_by: dev-tasks
spec_version: 1.2
generated_at: 2026-10-26
---
# 基础设施任务

`esp/`及桥板`tests/`均位于独立ESP仓库，不在当前Pi checkout；Pi源/SDK路径以[事实复核](04-dev-tasks-f-review.md)为准。记录目标环境与实测环境，不能将历史Qt6.4.2记录当作本轮目标Qt6.5.3通过。

## T-02 Pi/Qt/USB串口基线

| 属性 | 内容 |
|---|---|
| 主责 | Mr.D（串口/PBR）、Mr.Z（Qt构建环境） |
| 协作 | Mr.F、Mr.L |
| 层级/审核 | ⚪ 基础设施；串口/部署 `audit` |
| TDD 模式 | ⚪ 不适用（环境/硬件验证） |
| review_profile | `audit` |
| 依赖 | T-01 |
| 追溯 | F-001 / AC-001 / IT-002 |
| 文件范围 | Pi应用构建入口、串口传输层、构建/实机记录 |
| 具体路径 | `pi_terminal/CMakeLists.txt`、`src/transport/PogoControlTransport.{h,cpp}`、`src/protocol/pibridge_protocol.cpp`、`tools/mock_bridge.py`；ESP桥板 `main/main.c`、`build.ps1`、`tests/` |

### 工作内容

- 在 Pi 3B、64-bit Bookworm、Qt 6.5.3、Wayland 构建主项目。
- 核验 USB-UART 115200 8N1。
- 同时记录临时 `/dev/ttyUSB0` 和稳定 `/dev/serial/by-id/`。
- 记录内核、编译器、Qt plugin path、串口权限组。
- D同时负责P0最小跨仓SDK来源清单（来源仓/commit/路径/哈希，Z验证构建）：Pi提供C ABI/配包/模拟器，ESP提供正式固件/共享组件；当前两仓同步检查均失败，先解释并修复各自映射/清单，再把副本当权威。完整SDK发布工具仍在P2，不阻止第1天只读预检。
- ESP桥板host test runner当前并发发送fixture依赖`windows.h`；Linux GCC不能直接完整运行。D须记录受支持工具链/平台，不能把局部RC522测试通过写作全套通过；需要Linux支持时在协议PR补可移植fixture。

### 测试方法

桥板拔插 10 次，记录稳定路径；合法 PBR ping 20 次并校验帧/CRC和响应。

### 验收标准

构建通过；20次 ping 全部收到正确响应；设备重插后稳定路径不变；Pi服务不需要 root 运行。

### Review 要点

不要依赖临时 `/dev/ttyUSB0` 作为产品固定路径；构建记录不得包含凭据。

## T-06 啸天采购与双槽电气核验

| 属性 | 内容 |
|---|---|
| 主责 | Mr.L |
| 协作 | Mr.F、Mr.D |
| 层级/审核 | ⚪ 电气/硬件变更 `audit` |
| TDD 模式 | ⚪ 不适用（采购和电气验证） |
| review_profile | `audit` |
| 依赖 | T-01 |
| 追溯 | F-007 / AC-007 / IT-006 |
| 交付物 | 采购清单、探头规格、接线/GPIO图、电压和负载测量 |
| 具体路径 | ESP桥板 `main/bridge_pins.h`、`components/board_pins/include/`、`main/Kconfig.projbuild`；啸天计划新增独立模块目录及接线/实测记录 |

### 工作内容

- 两对主动红外探头、每对独立输出；第 3 天前到货。
- 记录电压、电流、输出类型、遮挡/正常/断电状态。
- 核对两个槽位的 DET、总线、供电、共地、过流保护和实际 GPIO。
- 用户允许 L 修改接线/加复用隔离/调 GPIO；每次改动先断电并记录。

### 测试方法

断电连续性检查、逐槽通电、双模块负载、单槽拔出/抖动、GPIO高电平测量、连续30分钟运行。

### 验收标准

探头参数与实物相符；GPIO 不超过 3.3V；双槽拓扑可同时通信；单槽拔出不造成另一槽复位；接线图和复测记录可复现。

### Review 要点

任何过 3.3V 信号必须有合适接口/隔离；不得凭商品页标称忽略实际电气测量。
