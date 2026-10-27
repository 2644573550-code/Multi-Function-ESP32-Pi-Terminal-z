---
generated_by: dev-tasks
spec_version: 1.2
generated_at: 2026-10-26
---
# 接口与UI任务

路径中的`pi_terminal/`属于Pi仓库，`esp/`属于独立ESP仓库；共享SDK与啸天资料现状见[事实复核](04-dev-tasks-f-review.md)。新增安全服务/接口均是目标，不是现有能力。

## T-01 需求、接口和验收冻结

| 属性 | 内容 |
|---|---|
| 主责 | Mr.F |
| 协作 | 全员 |
| 层级/审核 | 🔴 `audit` |
| TDD 模式 | ⚪ 不适用（架构/接口冻结；用审查清单验证） |
| review_profile | `audit` |
| 依赖 | 无 |
| 追溯 | F-001 / AC-001 / IT-001 |
| 文件范围 | 本任务更新 `docs/devdocs/04-dev-tasks*.md` 的接口/验收定义；核对报告及两仓库路径，不改实现代码 |

### 工作内容

冻结用户流程、权限矩阵、身份密钥边界、PBR帧/事件、错误码、slot状态、SQLCipher API、worker IPC、网络默认策略及验收顺序。

交付入口：[F第一天审核稿与预检台账](04-dev-tasks-f-day1.md)。F已按用户授权审定范围/权限/安全边界；密码协议细节、实物字段和D/Z/L签认未收齐前，不将T-01标记完成。文档边界允许先开展各自可行性预检，不要求等预检完成才允许执行预检。

### 测试方法

D/Z/L 对字段和依赖逐项签字确认；检查任务编号唯一、依赖无环、P0任务均有owner和可判定测试。

### 验收标准

所有P0任务均有owner、依赖、测试方法和可判定验收条件；报告v4.0与本次P0范围扩展的差异明确写出。

### Review 要点

不得把“项目签发桥身份”误写为“桥板 DIY 密钥”；模块 DIY 只允许逐台唯一。

## T-05 PBR编解码和桥板事件状态机

| 属性 | 内容 |
|---|---|
| 主责 | Mr.D |
| 协作 | Mr.L |
| 层级/审核 | 🟡 `guarded` |
| TDD 模式 | 测试先行：分片/坏帧/重同步测试→parser实现→边界复审 |
| review_profile | `guarded` |
| 依赖 | T-01 |
| 追溯 | F-005 / AC-005 / UT-003、IT-005 |
| 接口 | direction/kind/sequence/length/CRC framing、attach/status/detach、`rfid_card` |
| 文件范围 | 现有Pi：`pi_terminal/src/protocol/pibridge_protocol.cpp`、`src/transport/PogoControlTransport.cpp`、`pi_terminal/tools/mock_bridge.py`（主机侧假桥板，自测用）；**依赖远端（不改动）**：`esp/S3_PiBridge_LinkIt_Host/main/main.c`、`rfid_service.c`、`tests/` |

### 工作内容

- 串口二进制帧解析：ROM噪声、半包/粘包、坏 CRC 重同步、partial timeout。
- PBR 当前是 CRC framing，不是安全层；安全会话由 T-07 叠加。
- RFID 与 LinkIt attach/detach 独立路由。
- 双槽字段用 `bridge_id + slot_id + device_id + boot_id`，不沿用单一全局 attached bool。
- 保留当前触点/业务分离：detach释放物理槽但不停止认证/授权仍有效的无线任务。桥板掉线禁止新的读卡/物理接入确认，已有任务继续；恢复重验控制身份/位置但不无故重建有效业务。T-05/11/16扩展`ConnectionManager.cpp`、`tests/session_test.cpp`及页面，覆盖槽位复用、迟到事件、冲突隔离和本机PIN解锁；不把当前仅更新状态视作这些安全检查已实现。RFID为桥板级事件，不强制slot/device。

### 测试方法

测试噪声、分片、超长、坏 CRC、重复序号、两个 slot 交错事件。

### 验收标准

正确帧全解析，错误帧不阻塞后续帧，事件不串槽；parser有界且可超时恢复。

### Review 要点

禁止把 CRC 描述成 AEAD/MAC；限制 parser 缓冲和 JSON 深度。

## T-08 RC522到Pi安全事件闭环

| 属性 | 内容 |
|---|---|
| 主责 | Mr.D（PBR/事件链路）、Mr.Z（用户匹配回调） |
| 协作 | Mr.L |
| 层级/审核 | 🟡 `guarded` |
| TDD 模式 | 集成测试先行：伪造/有效RFID事件→Pi事件链路实现→真实桥板复测 |
| review_profile | `audit` |
| 依赖 | T-04、T-07 |
| 追溯 | F-006 / AC-006 / IT-008、E2E-001 |
| 文件范围 | 现有Pi：`src/transport/PogoControlTransport.cpp`、`src/core/HostEventDetector.h`、`src/core/ConnectionManager.cpp`；新增RFID事件router/auth callback；**依赖远端（不改动）**：`main/rfid_service.c` 的事件字段形状 |

### 测试方法

在串口/Pi上分别发送有效签名桥事件、未配对桥事件、AEAD篡改、重复event_id、过期event_id和正常UID；记录所有接收结果并扫描日志/测试目录。

### 验收标准

真实 RC522→项目签发桥身份认证→AEAD `rfid_card`→Pi event_id 去重→本机密钥派生 UID 索引→选中用户→只显示 PIN 页面。raw UID 只短暂驻留内存，不进入 DB/log。PBR承载控制/RFID；盆栽传感器业务数据仍走模块合同固定BLE或Pi hotspot LAN/TCP数据面，不塞入PBR。

### Review 要点

检查桥板签发身份与模块DIY key未混用；UID不得成为认证秘密；事件解密和PIN认证分层。

## T-09 Qt登录、绑卡、锁屏、挂失

| 属性 | 内容 |
|---|---|
| 主责 | Mr.Z |
| 协作 | Mr.F、Mr.D |
| 层级/审核 | 🟢 `guarded` |
| TDD 模式 | 实现→交互验证→补UI自动化/回归测试 |
| review_profile | `guarded` |
| 依赖 | T-04、T-08、T-10 |
| 追溯 | F-003 / AC-003、F-006 / AC-006 / E2E-002 |
| 环境 | Qt 6.5.3、Wayland、Pi 3B |
| 文件范围 | 现有 `pi_terminal/qml/Main.qml`、`src/core/ModuleCardModel.*`、`src/core/PageProvider.*`；新增登录/锁屏/绑卡QML组件并在CMake资源表登记 |

### 工作内容

实现 PIN、锁屏、低敏用户选择、登录后显式绑卡、管理员 PIN 挂失和错误/离线状态；QML 只调用可信服务。

### 测试方法

在Pi/Wayland上用已绑定卡、未绑定卡、管理员、正确/错误PIN分别完成登录、绑卡、挂失、锁屏和退出流程；校验页面及trusted service状态。

### 验收标准

刷卡不能直接登录；PIN正确后登录；绑卡必须已登录且显式进入短时窗口；管理员可重验PIN撤销卡映射，但不能读取他人健康数据。锁屏隐藏敏感内容且停止前台读取，不停止已授权原用户任务；切用户前显示任务完成/取消或明确停止流程。Wayland实机触摸和键盘完成闭环。

### Review 要点

QML不得读取SQLCipher文件、raw UID或持有数据库密钥；所有操作须由trusted service再次做会话/权限验证。

## T-12 模块业务数据安全传输（P0）

| 属性 | 内容 |
|---|---|
| 主责 | Mr.D（**Pi 侧**协议/加密传输对端） |
| 协作 | Mr.L（ESP 侧实现，等软件完成后开工）、Mr.Z（Pi TLS/BLE 会话及用户授权）、Mr.F（安全评审） |
| 层级/审核 | 🔴 安全协议；`review_profile=audit` |
| TDD 模式 | 安全集成测试先行：明文/未配对连接拒绝与证书校验失败测试→安全lane实现→网络抓包/降级复测 |
| review_profile | `audit` |
| 依赖 | T-01、T-03、T-04 |
| 追溯 | F-013 / AC-013 / IT-014 |
| 数据面 | PBR/LinkIt 只控制；业务payload走固定BLE或Pi hotspot LAN/TCP安全lane |
| 文件范围 | Pi 侧（D）：`pi_terminal/src/transport/HotspotLanTransport.{h,cpp}`、`BleTransport.{h,cpp}`、`CMakeLists.txt`；**ESP 侧归 L，D 不改动**：`esp/shared/module_data_service/module_data_service.c`、`module_data_wifi.c`、`module_data_ble_gatt.c`、`include/module_data_service.h`；具体TLS/BLE security API以IDF5.5.4兼容验证结果冻结 |

### 工作内容

- P0把盆栽固定为 LAN-only profile，在模块服务端与Pi客户端实现TLS；Pi校验证书链/设备绑定/主机身份，桥板/模块身份密钥与传输密钥分离。
- 若P0 profile仍保留BLE，必须强制加密和经过认证的配对/应用层挑战认证；不能只因GATT连接成功就接受业务payload。建议首轮固定LAN以控范围；未实现安全的BLE profile须显式禁用并验证拒绝。
- 未加密、未认证、身份不匹配、过期会话或TLS/BLE安全协商失败均关闭；P0 profile 禁止回退现有明文TCP/未认证GATT。
- 用逐设备测试凭据；密钥仅在设备/Pi，绝不提交仓库。
- PBR只传attach/status/detach和RC522等控制事件；不将盆栽历史/测量payload塞进PBR。

### 测试方法

对启用的P0业务profile做有效/错误证书或设备key、未配对连接、握手篡改、断线重连和降级测试；用抓包验证业务payload不能以明文识别；尝试连接任何被禁用的明文/BLE profile，确认拒绝；确认两模块使用不同凭据。

### 验收标准

- P0固定LAN业务profile要求TLS且设备认证；若启用BLE，同样必须加密并认证。
- Pi能验证模块身份；错误/共享/撤销凭据拒绝。
- BLE未建立安全配对/应用认证前不接受GATT业务数据。
- LAN不接受明文TCP session；TLS失败无fallback。
- 抓包看不到传感器明文或session credential。
- 断线/切用户/撤权后旧session不可继续发数据。
- 测试密钥和设备凭据不进入Git、日志或PR。

### Review 要点

D提交 Pi 侧协议/key separation 设计，F审核算法/证书验证/fail-closed，Z证明Pi端严格校验；ESP TLS/BLE 属性实测由 L 在软件完成后执行；PBR控制面与业务数据面不能混淆。

## T-13 盆栽模块安全接入和完整数据

| 属性 | 内容 |
|---|---|
| 主责 | Mr.L（ESP 固件）、Mr.D（**Pi 侧接口与字段**）、Mr.Z（用户归属/UI） |
| 层级/审核 | 🟡 `guarded` |
| TDD 模式 | 集成测试先行：模块会话/数据归属→业务数据接入→端到端复测 |
| review_profile | `guarded` |
| 依赖 | T-03、T-05、T-11、T-12 |
| 追溯 | F-008 / AC-008 / IT-009、E2E-003 |
| 文件范围 | ESP仓：`esp/S3_Pi_Module/main/main.c`、`main/Kconfig.projbuild`、`esp/shared/pi_sensor_device/`、`esp/shared/module_data_service/`及SDK导出副本；Pi仓：`module-devkit/examples/smart-planter/{module.cpp,History.h,qml/}`、`ConnectionManager.cpp`、可信存储API、安全transport及受控状态UI |

### 测试方法

在经T-12保护的固定BLE或Pi hotspot LAN/TCP业务lane上采集所有已实现传感器字段；同时确认PBR仅负责控制/读卡；断开、重启、切换用户和延迟数据分别注入。

### 验收标准

独立模块身份认证、真实 attach/status、所有已实现传感器值显示、时间/新鲜度/故障状态正确；退出用户后旧用户上下文不能继续读取；模块断开显示未知而非伪造正常。数据必须走经配置选择的BLE-only或LAN-only lane，不得把LinkIt/PBR控制帧冒充传感器业务数据。

“全部”包括温度、湿度、气压、照度、battery_V/battery_pct及供电/保护状态；缺传感器或无可靠外部电源检测则显示不可用/unknown，charging当前为null，不伪造测量。P0固定LAN TLS、禁用BLE；现有Qt6.4.2/明文历史验证不代替Qt6.5.3/SQLCipher/worker新路径验收。

### Review 要点

确认module transport profile与Pi TransportPolicy一致；私有token只在本地配置，不进入Git/日志；用户上下文不可跨session复用。

## T-14 啸天两路真实输入和页面

| 属性 | 内容 |
|---|---|
| 主责 | Mr.L（硬件/固件）、Mr.D（**Pi 侧事件字段与接入接口**）、Mr.Z（页面/会话权限） |
| 层级/审核 | 🟡 `guarded` |
| TDD 模式 | 集成测试先行：两路遮挡/恢复输入→事件实现→探头实机验证 |
| review_profile | `guarded` |
| 依赖 | T-06、T-07、T-11、T-12 |
| 追溯 | F-009 / AC-009 / IT-010、E2E-004 |
| 文件范围 | ESP仓既有`module-devkit/examples/xiaotian/`目前仅需求/计划，目标新增其`firmware/`；module_id由D/L查重确认，0x19只是候选；Pi仓由Z新增受控状态UI并复用安全业务lane，不把该目录当已有插件；未定GPIO不写死 |

### 测试方法

每一路分别遮挡/恢复至少20轮，再交替、同时、短脉冲和断线；记录原始输入与Pi事件对应关系。

### 验收标准

两对探头均真实接线、独立遮挡/恢复、去抖有效、每段事件和页面状态对应。P0不声称测速、户外距离、误报率或闭合区域验收。

### Review 要点

电压/输出类型必须依据实测；没有硬件支持的tamper/断线不能伪报正常。

## T-15 两模块分别运行

| 属性 | 内容 |
|---|---|
| 主责 | Mr.L（实机）、Mr.D（**Pi 侧路由与状态机**）、Mr.Z（用户/页面） |
| 层级/审核 | 🔴 `audit` |
| TDD 模式 | E2E先行：两模块独立运行/断开测试→闭环实现→回归 |
| review_profile | `audit` |
| 依赖 | T-08、T-09、T-10、T-11、T-12、T-13、T-14 |
| 追溯 | F-010 / AC-010 / E2E-005 |

### 测试方法

分别启动盆栽、关闭后启动啸天；每种模块冷启动、重启、断开再连，并验证状态/数据归属。

### 验收标准

盆栽和啸天各自独立完成认证、运行和页面显示；一方停止不影响另一方。

### Review 要点

验证PBR attach状态与BLE/LAN业务数据lane分开；用户、device_id、slot_id不得丢失。

## T-16 双槽同时运行和单槽故障

| 属性 | 内容 |
|---|---|
| 主责 | Mr.L（实机）、Mr.D（**Pi 侧路由与状态机**）、Mr.Z（用户/页面）、Mr.F（裁决） |
| 层级/审核 | 🔴 `audit` |
| TDD 模式 | 系统测试先行：双槽并发/拔槽测试→双路路由实现→压力复测 |
| review_profile | `audit` |
| 依赖 | T-15 |
| 追溯 | F-010 / AC-010 / IT-011、E2E-006 |

### 测试方法

两个slot连续同时运行至少30分钟，期间交替发送盆栽数据与啸天事件；拔出/重插任一slot并重启桥板一次。

### 验收标准

先分别运行，再同时运行；盆栽数据和啸天事件并行到 Pi；两个 slot/device/event/user context 不串；单槽拔出不影响另一槽；重启后分别重认证。

### Review 要点

控制事件按bridge/slot/绑定代次校验；业务按device/业务session/task/原用户能力校验，slot仅描述位置，离槽或桥失联可为null/unknown。补测离槽继续、旧槽换设备、桥失联PIN解锁/停任务、重插一致/冲突和业务安全失效；不能以module_id/slot单独作为安全身份。


## T-17 P0最终验收和PR合并

| 属性 | 内容 |
|---|---|
| 主责 | Mr.F |
| 协作 | 全员 |
| 层级/审核 | 🔴 `audit` |
| TDD 模式 | ⚪ 不适用（人工验收门禁；逐项复核证据） |
| review_profile | `audit` |
| 依赖 | T-16 |
| 追溯 | F-010 / AC-010 / Journey-001 |

### 测试方法

F按P0完成清单逐项观察Pi、桥板和两个ESP模块实机运行，核对对应PR/测试结果及任何未测项。

### 验收标准

逐项核对 [P0完成条件](04-dev-tasks.md#7-p0完成条件)，所有安全、SQLCipher、worker隔离、啸天真实输入、双槽同时运行验收均通过；所有PR已由F审核。任一硬性项失败即P0未通过。

### Review 要点

不得把报告规划文字、模拟器结果或未到货硬件写成通过证据。
