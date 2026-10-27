# Mr.F 第一天交付：P0 接口与安全边界审核稿

日期：2026-10-26 · 任务：T-01，协作 T-03 · 契约版本：`p0-contract-v1`（互动澄清修订）

**状态（2026-10-28 更新）：F 文档审定通过；详细密码协议已由 F 起草并交付（`docs/pbr-secure-v1/`，规范修订 2 + 向量 + mbedtls 双向验证），**等待 D 技术签认**；D/Z/L 技术签认、实机预检仍待完成；T-01 / IT-001 未通过。** F 确认的是范围、职责、安全边界、分阶段冻结规则和密码协议方案，**不是实现、实机结果或其他成员签认**。依据见[事实复核与审定记录](04-dev-tasks-f-review.md) 与 [`F-REVIEW-2.md`](../pbr-secure-v1/F-REVIEW-2.md)。

## 1. 基线、范围和变更规则

- Pi 仓库：`Axe665/Multi-Function-ESP32-Pi-Terminal`；本轮 fetch 后 `main` 与 `origin/main` 同为 `20013b242e8f28da788a3bd966272f960e56be08`，领先/落后 0/0。
- ESP 仓库：`gggitj/ESP-Modular-Terminal-Service`；本轮重新读取远端 main，`16aebdd5e2bd17c0addd0b00e3e963cef3252584`。本文 ESP 路径均相对该仓库，不要求 Pi 仓库存在同名目录。
- 依据：技术建议报告 v4.1（本轮订正）、[总计划](04-dev-tasks.md)、T-01～T-17；用户指定目标为 Pi 3B / ARM64 Bookworm / Qt 6.5.3 / Wayland。历史盆栽验证记录使用 Qt 6.4.2、labwc/Wayland，不能冒充目标版本本轮实测。
- P0 为 14 天；SQLCipher、低权限 worker、加密业务数据、真实双路啸天和双模块同时运行均为硬门槛，覆盖报告原阶段排序。备份恢复、生产 PKI、完整 sandbox 加固、测速及户外验证仍属 P1/P2。
- 本轮仅修改建议报告与任务/审核文档；不修改 README、实现代码、设备配置或真实身份；不生成真实密钥，不连接设备、不部署、不刷写。
- 本文获确认后，字段、权限、默认值或安全语义变更须在关联任务 PR 写明影响、测试和另一仓库交叉链接，由 F 批准。未知 schema/版本禁止猜测兼容或明文降级。

## 2. 用户流程和权限矩阵

固定流程：桥板认证 → 模块各自认证 → 刷卡仅选择用户 → PIN → 唯一用户会话 → 授权模块 → 分别运行 → 同时运行。也允许从低敏用户列表进入 PIN 页；未绑定卡不能自动建用户或登录。

PIN 为至少 6 位 ASCII 数字，初始策略为每账号连续失败 5 次锁定 60 秒；另有终端级 1 次/秒的认证尝试上限，未知账号同样计入。重启不可清除有效锁定；正确认证才清除该账号失败计数。错误提示不区分未知账号/错误 PIN。KDF 参数须经 Pi 测量后由 F 审定，不以裸哈希存 PIN。

本文5分钟锁屏、30秒绑卡、ACK/TTL/队列/IPC上限等为F审定的P0初始工程策略，不是已测性能。L必须按实际探头校准去抖/采样和短脉冲边界，D/Z必须验证Pi内存/延迟；不合适时提交实测和修订，不为满足文档数字掩盖漏报或资源不足。

无交互 5 分钟自动锁屏；锁屏立即撤销前台会话和历史读取能力、清理敏感页面，**不停止已授权任务，也不改变其启动用户**。可信服务仅为仍在运行的任务保留必要 DB 解锁状态；独立任务能力仅可提交绑定设备/任务的新样本，不能读取历史、切用户或导出。锁定后必须重新输入 PIN，不得刷卡解锁；提示/语音不含姓名、读数或精确位置。

切用户/退出前，测量先完成或显式取消并标不完整，持续监测须用户明确停止；随后撤销全部旧前台/任务能力、关库及回收worker，不能把运行中任务转给新用户。停用用户、撤销设备/任务授权立即停止相关任务。Pi/模块重启不自动恢复旧授权，须原用户解锁并重新认证后恢复。这些语义沿用报告已确认需求，订正上一稿“锁屏即停任务”的偏差。

| 操作 | 未登录/锁屏 | 当前普通用户 | 管理员 | 模块 worker |
|---|---|---|---|---|
| 低敏用户选择、PIN 认证 | 允许；不显示健康数据 | 允许退出后切换 | 同普通用户 | 禁止 |
| 读取/写入健康记录 | 前台禁止；已启动任务可由可信服务受限追加原用户记录 | 仅本人、已授权设备及允许的数据类型 | 仅本人；管理角色不授予他人数据访问 | 仅通过受限数据 API；锁屏后仅原任务追加，不可读历史或指定用户 |
| 绑卡 | 禁止 | 重新验证本人 PIN 后打开 30 秒单次窗口；已绑定他人的卡拒绝 | 只能按本人流程绑自己的卡 | 禁止 |
| 挂失/撤销卡映射、停用用户 | 禁止 | 允许撤销本人映射 | 重新验证管理员 PIN 后可撤销成员映射/停用成员；不能借此解锁 DB | 禁止 |
| 创建用户/管理角色 | 禁止；仅首次离线受控初始化例外 | 禁止 | 重新验证管理员 PIN 后允许；不能导出成员密钥 | 禁止 |
| 配对/撤销桥板或模块身份 | 禁止 | 禁止 | 本地显式确认并重新验证管理员 PIN | 禁止 |
| 授权设备访问本人数据 | 禁止 | 按设备及能力显式授予/撤销 | 不能替成员授予健康数据访问 | 只能使用宿主已授予能力 |
| 模块公网、任意文件、SQL/DB key | 禁止 | P0 不提供模块公网授权入口 | 同左，不能绕过 P0 策略 | 全部禁止 |

首次管理员创建必须本地受控初始化、无默认 PIN/万能账号；已有管理员后关闭该入口。PIN 遗忘不提供绕过解密，恢复流程为 P1；部署前必须告知数据不可恢复风险。

## 3. 威胁模型和身份密钥

| 威胁/边界 | P0 必须防止 | 证据 |
|---|---|---|
| 串口/无线攻击者 | 冒充桥板/模块、篡改、重放、明文或旧协议降级 | UT-001/004、IT-003/007/014 |
| 可克隆 RFID UID | 刷卡直接登录、raw UID 持久化 | IT-008、E2E-001/002 |
| 恶意模块/worker | 伪造 user/slot/device、读其他用户 DB、读宿主私有文件、联网外传 | IT-004/012/013、IT-011 |
| 普通用户/管理员越权 | 跨用户健康数据访问、旧会话继续读写 | UT-002/005、IT-004/012 |
| 断线、重启、单槽故障 | 重用 nonce/旧能力、事件串槽、伪造正常值 | IT-007/009/010/011 |

Pi 可信服务、OS 管理员和受控签发环境属于信任根；P0 不声称抵御 root 被攻破、设备侵入式取钥、离线无限 PIN 猜测或拒绝服务。SQLCipher 不替代运行时隔离；worker 隔离不依赖模块自律。

- 桥板：项目逐板签发测试身份；issuer 私钥仅在 F 控制的签发环境，Pi 持验证公钥，桥板仅持自己的私钥/凭据。双方认证，Pi 自身认证凭据也不得进入通用镜像。
- 模块：保留正式签发与 DIY 两条路线；P0 盆栽/啸天各用独立 DIY 身份，经本地管理员确认指纹登记，网络首次出现不得自动信任。
- 密钥角色分离：issuer、桥板身份、模块身份、PBR 会话双方向密钥、TLS 传输密钥、业务 session credential、UID 索引密钥、用户 DB key/KEK 不得复用。证书或配对记录须绑定身份公钥与传输身份。
- `CONFIG_APP_PI_DATA_TOKEN` 仅是业务 session credential，不是设备身份，也不能替代 TLS/AEAD。不得传入 QML、日志或 PR。
- `TrustStore`只接受可信认证路径写入配对/撤销记录；撤销立即断开连接、销毁派生会话并拒绝后续事件。跨槽移动必须重新认证控制身份及位置绑定，但不无故重建仍有效的独立安全业务会话；不能凭旧`module_instance`继承授权。
- raw UID 仅在桥板与 Pi 可信内存短暂存在；Pi 用独立本机秘密经 HMAC-SHA-256 对 `ASCII("p0-rfid-index-v1") + uid_len:u8 + 原始UID字节` 派生索引，禁止普通 SHA 散列或直接散列大小写不同的 hex 文本。QML/worker 只见用户选择结果，不见 UID/索引。

## 4. 控制事件契约（D 主责，Z/L 消费）

### 4.1 帧和安全层

现有 `PiBridge::encode/decode` 外层不变：`PBR1 | direction:u8 | kind:u8 | sequence:u32le | length:u16le | payload | CRC16le`，direction 0=Pi→桥，1=桥→Pi；kind 1=事件、2=命令、3=错误；payload ≤2048 字节；CRC16 初值 `0xffff`、多项式 `0x1021`，覆盖 direction 至 payload，仅检错。

P0 认证后 payload 必须走 T-07 安全封装，长度限制含封装开销。direction/kind/sequence/length 和身份/会话上下文必须纳入认证覆盖；不得先派发 JSON 再验证 tag。认证前仅放行有界握手，不接受 attach/RFID/业务事件。旧 CRC-only 接口仅作隔离测试，不能进入 P0 产品路径。

**不能伪称已冻结的项**：握手字节布局、成熟协议/库、算法套件、KDF、nonce/AAD 编码和测试向量须 D 提交、F 审核，记录在 B-01；未关闭前 T-07 安全实现不可宣称可互操作。禁止自行拼接一个“看似 AEAD”的握手。

> **2026-10-28 进展**：上述六项已由 F 起草并交付（`docs/pbr-secure-v1/pbr-secure-v1.md` 修订 2、`vectors.json`、`mbedtls_check.c`）。**交付不等于冻结**——须 D 技术签认后才算冻结，B-01 仍未关闭。

### 4.2 解密后统一字段

以下为新目标 `schema="p0-control-v1"`；旧 `terminal-rfid-v1` 不能因字段相似直接被信任。所有身份字段须与认证连接中的身份一致，不信任消息自报身份。

| 字段 | 类型/约束 | 适用 |
|---|---|---|
| `event` | `attach/status/detach/rfid_card/ack/pong/bridge_status` | 全部；未知类型拒绝；后两项保留当前诊断响应名称 |
| `bridge_id`, `key_id` | 1～64 字符 `[A-Za-z0-9_-]`；绑定已验证凭据 | 全部 |
| `boot_id` | 每次桥板启动更新的 128 位随机数，32 位小写 hex | 全部；不同于当前旧版 16 hex RFID boot_id |
| `session_id` | 每次认证更新的 128 位随机数，32 位小写 hex | 全部；不是用户会话 |
| `event_id` | 会话内递增 1～4294967295 的整数；重传保持不变 | 除 ACK 外必填 |
| `uptime_ms`, `ttl_ms` | 单调启动时间，非负安全整数；事件 TTL 固定 3000ms | 除 ACK 外必填 |
| `slot_id` | 整数 0 或 1；逻辑编号到 GPIO 映射由 L 实测确认 | 仅模块事件必填 |
| `binding_generation` | 每次新物理绑定更新的128位随机数，32位小写hex，纳入控制认证覆盖 | 模块位置事件必填；离槽业务只附可空位置快照 |
| `device_id`, `device_boot_id` | 逐模块身份；模块启动标识为 32 位小写 hex | 仅模块事件必填 |
| `module_id`, `module_instance` | 现有类型号 1～255、实例标签 1～64 字符 `[A-Za-z0-9_-]` | attach/status；不是密码学身份 |
| `reader_id`, `uid_len`, `uid` | reader 为 1～64 字符标签；长度 4/7/10；UID 为对应长度 hex | 仅 RFID，且只在加密载荷内 |
| `ack_event_id`, `result` | 已接收事件编号；`accepted/duplicate/expired/rejected` | 仅 ACK；不含原始载荷 |

RC522是桥板级事件，**不要求也不伪造slot_id/device_id**。控制位置绑定键为`bridge_id+slot_id+binding_generation+device_id`；RFID去重键为`bridge_id+boot_id+session_id+event_id`。独立业务任务主键为`device_id+device_boot_id+business_session_id+task_id+origin_user_id`，slot_id仅是可空的位置属性，不参与授权身份。桥板attach不替代模块业务身份认证。

安全会话内Pi诊断命令仍为kind=2的`command=ping/status`，响应绑定`request_sequence`；桥板级诊断同样不伪造slot/device。认证前ping/status仅在隔离基线测试中允许，产品安全路径只放行握手。ACK和诊断的具体安全封装同属B-01协议交付。

解析拒绝缺字段/错误类型/重复 JSON key/深度>8/越界值；Pi接收缓存≤16384字节，半帧1000ms超时清理；ESP继续使用按最大帧长有界的缓存，不要求扩到16KiB。u32序号不得环绕复用；到上限先重新认证。协议认证序号与event_id分离：重传同一逻辑事件使用新的认证序号/nonce。所有随机会话/启动标识使用平台密码学安全随机源。

ACK 在 tag/身份/TTL 校验、去重并入有界可信队列后发送；仅表示接收，不表示 PIN 登录成功。发送后 500ms 等 ACK，最多额外重传 3 次且不得超过 TTL；重传保持原始事件时间和内容，不能刷新 TTL。ACK 本身不要求 ACK。重复有效事件再次 ACK，但不再次触发操作；队列满返回 `BUSY`，不得先 ACK 后丢弃。

TTL 用握手建立并验证的对端单调时间偏移校验；时间基准失效或无法确认年龄则拒绝并重新建会话，不能把“刚收到”当作“刚发生”。去重记录保留至会话结束，最多4096项；满时重建会话而非提前遗忘。重启清除旧会话授权，不接受旧会话重放。

### 4.3 槽位状态

物理槽位状态独立为`absent → authenticating → bound`，控制失联时为`unknown`；它与模块业务ready分开。业务ready要求已建立的设备认证、TLS、有效前台或任务能力及worker，不等于端口打开。锁屏可保留原任务ready，不开放前台读取；拔出后显示离槽/`contact_detached`，不能将物理位置标为仍在线。

**互动确认后的P0策略**：保留当前触点/业务分离方向，不采用上一稿“拔槽/桥失联即停全部业务”。任务绑定`device_id+device_boot_id+业务session_id+task_id+原用户能力代次`，slot_id仅为当前物理位置。拔出释放旧槽，认证及授权仍有效的无线任务继续；旧槽可接新设备，新设备不得继承旧任务。控制事件按位置绑定代次路由，迟到事件不能更新新占槽设备。

桥板USB失联时已有安全业务任务继续，位置显示unknown并提示控制故障；禁止新的刷卡认证、绑卡、设备信任登记及依赖桥板的位置接入确认。允许已登记用户通过本机低敏用户列表/PIN解锁，查看本人任务、停止或撤权；PIN限速和会话权限不放宽。业务链路故障、认证失效、撤权或主动停止仍使对应任务失效，不因“继续无线”放过安全失败。

重插或桥板恢复后重新认证控制身份、核对设备和位置；一致则恢复位置显示，不无故中断安全业务；冲突则隔离新位置绑定并提示，不把旧授权转给新设备，也不以槽位冲突本身销毁独立有效任务。Pi或模块本身重启导致业务会话失效时，仍按第2节重新解锁/认证恢复。T-05/11/16补覆盖这两条生命周期及槽位复用用例，保留现有contact_detached正确语义。

## 5. 可信服务、SQLCipher 与 worker API（Z 主责，D 对接）

以下冻结服务职责/调用语义，不承诺现有 ABI 已具备这些方法；新增服务在 `pi_terminal/src/security/`，由 `TerminalApplication.{h,cpp}` 装配，资源/测试登记到 `pi_terminal/CMakeLists.txt`。

| 接口 | 最小调用与结果 | 边界 |
|---|---|---|
| `TrustStore` | `lookupIdentity/revokeIdentity`；仅可信配对流程可登记 | D 实现，Z 使用；不对 QML 暴露 key |
| `UserService` | `listUsers/createUser/disableUser`，返回低敏视图 | 管理操作重验管理员权限 |
| `AuthService` + `PinPolicy` | `verifyPin/changePin`，返回状态码/锁定剩余时间 | 不返回 PIN、hash、KEK 或 DB key |
| `SessionManager` | `lock/logout`、只读 `activeUserId/locked` | 单一当前用户；会话能力由服务签发且仅可信侧持有 |
| `UserDatabase` + `SqlCipherStore` | `openForSession/close/appendRecord/queryRecords` | DB 用户由可信会话确定；不接受 worker 自报 userId、SQL 或文件路径 |
| `ModuleWorkerLauncher` | `start/stop`，绑定设备/槽位/会话/能力 | P0 模块代码不在宿主 QLibrary 执行 |

worker/任务绑定设备和业务会话/能力；上表槽位仅为位置快照，不是worker生命周期或授权主键。桥失联后的本机PIN解锁仍可查看/停止本人任务，不允许绕过设备配对。

数据 API 仅允许已声明的数据类型和设备，分页查询每页≤100条；记录envelope含可信分配的用户、device/slot、task_id、接收时间及样本序号。前台能力与任务能力分开；锁屏撤销前台读写，仅保留绑定启动用户的任务追加。切换/退出必须先按第2节收尾任务再关库/销毁worker；任何迟到回调都不得挂入新用户页面或改写归属。

每用户独立随机256位DB key；PIN通过成熟KDF派生KEK后以认证加密封装DB key。PIN修改必须验证旧PIN并原子重封装，失败保留可用旧封装。只在可信内存解封；不把PIN直接当SQLCipher key。SQLCipher驱动须验证`cipher_version`非空、实际库加载、错误key无法读取和DB/WAL/temp不泄漏；普通QSQLITE加载成功不算通过。浏览器自带SQLite不是用户SQLCipher库。

现有盆栽`History.h/module.cpp`按module_instance散列路径写明文JSON温度历史，无用户边界。T-10/11/13必须将P0历史读写迁到可信SQLCipher服务并禁用旧JSON写入；旧记录没有可证明的userId，不得自动归给首位登录用户。默认保留原文件且禁止P0业务读取，迁移/删除须用户明确确认；本轮不处理或删除运行数据。

worker IPC 固定 `schema="p0-worker-v1"`，消息为 `u32le长度+UTF-8 JSON`，单消息≤65536字节、深度≤8、每 worker 在途请求≤16、超时5000ms；字段为 `schema/request_id/op/body`，响应为 `schema/request_id/code/body`。操作白名单仅 `publishSample/readOwnRecords/reportState/stop`；publish 必须严格校验模块数据 schema，状态文本不作可执行 QML/路径。未知 op/版本/越界消息拒绝，超长立即断开。

宿主通过私有继承IPC句柄和已启动worker PID/UID绑定权限；请求中的userId/deviceId不能改变绑定。每次调用校验前台或任务能力及其代次，锁屏任务只能追加原任务数据。模块只提供受限UI数据模型，由宿主可信QML渲染，不能把模块任意QML留在宿主执行以绕开worker；旧C ABI/任意QML示例不能直接宣称兼容新worker边界。

worker 专用非特权 UID、专用运行目录、必要只读运行库，不能读用户 DB、主配置、Pi 用户目录或任意宿主文件；拒绝本机管理端口及公网。Pi 热点禁止向 WAN 转发；P0 两模块均使用 `fixed_hotspot_lan + package_transport=data`，业务 TLS 经可信 transport 中介，BLE profile 禁用且测拒绝，不能通过启动参数恢复明文通道。浏览器/音乐自身联网不授权模块联网。

TLS 最低 1.2，使用成熟 Qt/ESP-IDF API；验证信任链或明确登记的逐设备信任、有效期、用途及设备绑定/端点身份，不忽略证书错误。DIY 自签仅允许显式配对的指纹，不接受任意自签。TLS 之后仍须双向设备/主机认证及用户会话授权；具体 mTLS 或应用认证路线由 D/Z/L 预检后在 B-01 签定。时钟无效时应报错，不得跳过有效期校验。

## 6. 业务数据冻结（L 主责，D/Z 对接）

盆栽沿用 ESP `esp/S3_Pi_Module/main/main.c` 的大小写与单位：

| 字段 | 类型/显示 |
|---|---|
| `temperature_C`, `humidity_RH`, `pressure_hPa`, `light_lux` | 有限数或 null；单位 ℃、%RH、hPa、lux |
| `battery_V`, `battery_pct` | 有限数或 null；电量为0～100的整数；不能因未安装电池而伪造数值 |
| `power_source`, `external_power`, `power_protection`, `charging` | 沿用固件枚举及布尔/null；当前 charging 为 null，显示未知，不推断正在充电 |
| `source`, `heartbeat`, `linkit` | 诊断元信息；heartbeat 不作安全身份/认证序号 |

新增安全业务envelope `schema="p0-data-v1"`含`device_id/device_boot_id/business_session_id/task_id/sample_seq/sample_uptime_ms/payload`及可空的`slot_id/binding_generation`位置快照；user/task能力由Pi可信宿主绑定，模块不能选择用户或任务。slot释放只结束位置快照，不结束已认证业务session/task；控制位置新binding_generation使旧位置回报作废，但有效传感器样本仍归原设备任务。序号1～4294967295且不得回绕。接收时记Pi单调时间；超过10秒无有效样本标stale，业务断开立即unknown。缺失/无效传感器显示不可用，不能填0或沿用旧值冒充新值；BMP280无湿度时不得声称湿度测量成功。L回填实物字段对照，不凭代码声称探头已安装。

啸天需求/执行资料当前只在ESP仓库`module-devkit/examples/xiaotian/`，没有已实现的固件/插件；Pi仓库没有该示例目录。P0目标工程放在ESP仓库既有示例下`module-devkit/examples/xiaotian/firmware/`（待新增），复用共享安全业务服务；逻辑类型名`xiaotian`，数值module_id待D/L核查已用号后回填B-05，0x19仍只是旧候选，不声称存在中央registry。Pi受控页面由Z在Pi仓库新增，不在宿主加载示例任意QML。

两路输入 payload：`channel` 为0/1，`state` 为 `clear/blocked/unknown`，`event_id` 为会话内唯一整数，`sample_uptime_ms` 为非负单调时间；置于上述安全业务 envelope。每路稳定50ms后提交一次状态变化，状态与事件分别去重；采样周期/短脉冲能力由 L 实测回填。未具备断线检测时只能显示不可判定，不能伪报 clear。GPIO/供电/有效电平不在文档中猜定；以 B-04 接线签认后写固件。

## 7. 统一错误码与可见性

可信 API/IPC 使用以下字符串；传输认证前不回显敏感错误细节。UI 只展示本地化提示，审计仅含 code、测试用非秘密关联号及脱敏结果，不含 PIN、UID、令牌、密钥、健康值或请求正文。

| code | 语义/处理 |
|---|---|
| `OK` | 操作完成；ACK accepted 不等于用户认证成功 |
| `INVALID_MESSAGE` / `UNSUPPORTED_VERSION` | 类型/长度/schema非法；拒绝，不猜测解析 |
| `AUTH_FAILED` / `RATE_LIMITED` | 通用认证失败；限速时返回允许公开的剩余等待时间 |
| `NOT_AUTHORIZED` / `SESSION_EXPIRED` | 权限不足或旧会话；不泄露其他用户对象是否存在 |
| `IDENTITY_REJECTED` / `TRANSPORT_SECURITY_REQUIRED` | 未配对、撤销或安全协商失败；关闭连接且无fallback |
| `REPLAY` / `EVENT_EXPIRED` | 不执行事件；仅对认证过的请求做受限回应 |
| `SLOT_MISMATCH` | 控制面当前slot/binding_generation与设备不符；拒绝位置更新和新接入，不因旧槽释放终止仍有效的独立设备业务任务 |
| `DATA_UNAVAILABLE` / `BUSY` / `TIMEOUT` | 无有效样本、队列满或超时；不伪造成功 |
| `STORAGE_UNAVAILABLE` / `WORKER_UNAVAILABLE` | DB/worker不可用；阻止 ready，错误不附带秘密路径/正文 |

## 8. 第一天预检回执与阻塞台账

**下表区分待提供实机证据与已证实的仓库问题，均未通过。** 当日截止前未交或不通过，由F升级为blocker，指定修复负责人/截止日，不降低P0验收。不得在报告中粘贴凭据或包含真实UID/个人数据的抓包。

| ID / 关联任务 | 负责人 | 当日最小回执 | 当前状态 / 失败路线 |
|---|---|---|---|
| B-01 / T-03/07/12 | D，Z/L协作；F审核 | 库/IDF版本、桥/Pi双向认证及AEAD套件/字节规范/向量；ESP TLS和Pi身份校验最小握手、内存峰值；BLE禁用证据 | **部分回执（2026-10-28）**：F 侧已交付规范修订 2、`vectors.json`、`mbedtls_check.c`（2.28 与 3.6.5 各 7/7）；库版本锁定 IDF 5.5.4/mbedtls 3.6.5 与 Qt 6.5.3。**仍缺**：D 的技术签认、ESP 侧实机、内存峰值、BLE 禁用证据、TLS 最小握手。未关闭，T-07 不开工 |
| B-02 / T-02/10 | Z（驱动），D（串口/SDK） | Pi OS/Qt/plugin path、SQLCipher实际加载库与cipher_version、错误key拒绝、打开/KDF延迟；串口by-id及权限、构建日志；两仓SDK来源/哈希与同步检查 | Pi/驱动待回执；SDK检查已证实失败：Pi缺13项ESP源映射，ESP清单需刷新；D先交跨仓显式来源与哈希清单，Z验证构建，禁止SQLite冒充 |
| B-03 / T-11 | Z，D协作 | worker与宿主PID/UID、DB/主目录/管理端口/WAN拒绝测试，热点不转发；宿主自身网络仍可用 | 待回执；仅QProcess/配置文本不通过，由Z补OS隔离并实测 |
| B-04 / T-06 | L，D协作 | 双槽原理/接线图、DET/总线/供电/隔离清单；两对探头下单及预计到货日，隐去个人订单信息 | 待回执；电气不安全先断电修正；第3天未到货或双槽不支持升级红色阻塞 |
| B-05 / T-13/14 | L、D，Z确认字段 | 盆栽字段与实物能力、啸天module_id注册/输入电平/采样周期，双槽逻辑编号映射 | 待回执；未测项保持unknown，不猜GPIO、不缩成单路 |
| B-06 / T-04/10 | Z；F审核 | PIN/KEK成熟库和参数、Pi延迟/内存测量、锁定持久化及首次管理员初始化方案 | 待回执；不以降低KDF或默认PIN绕过；F审定后回填参数 |
| B-07 / T-01/03/17 | F主责，D/L核查设备凭据 | main已跟踪的`module-devkit/artifacts/`及示例embedded_package清单；先只读扫描，不输出值；D/L在受控构建环境确认配置来源、凭据类型/有效性，报告仅含脱敏结论 | 已证实产物入Git。授权扫描：0个PEM私钥块、0个匹配字段名=长值模式、20处凭据相关字符串（可能只是格式串），故内容/有效性仍未知；立即阻止再次分发该固件，当日完成受控核查；有效凭据先轮换，清理跟踪/历史另行明确授权 |

每份回执统一格式：`B编号 / owner / commit / 目标硬件与版本 / 命令与脱敏结果 / PASS或FAIL或NOT_RUN / 风险 / 修复动作 / 截止日 / PR链接`。没有执行日期/实际结果的模板不算证据。

### 发送给三位同事的当日交接

- **给 D**：确认第4节字段、桥板级RFID与双槽路由；提交B-01安全设计、B-02串口部分，协助B-03/B-05；Pi/ESP各独立PR并交叉链接，先交篡改/重放/降级拒绝测试。
- **给 Z**：确认第2/5/7节用户、SQLCipher及IPC语义；提交B-02驱动、B-03隔离、B-06参数预检；未得当前会话能力的QML/worker不得读数据。
- **给 L**：确认第3/6节身份及实物字段；提交B-04采购/接线及B-05能力清单，协助B-01 ESP TLS；未核电气不得双槽通电。
- **F收口**：已按用户互动确认设备生命周期/排期边界；B-01/B-06具体设计仍须提交后审，不能预签不存在的方案。SDK失败归D主责、Z协作，第2天前交显式来源/哈希路线；B-07保持暂停分发并由D/L在受控环境核查，本轮模式扫描不证明秘密缺失或失效；其他预检失败由F记录负责人及新截止日，不削减P0硬门槛。未创建远端Issue/PR或代发消息。

## 9. IT-001 审查、签认与验收顺序

- [x] 17张任务卡均有owner、依赖、验收用例和可判定条件；无循环依赖。
- [x] 控制/业务面、权限、目标接口、错误码、SQLCipher/worker边界和P0/报告差异已形成可审查文字。
- [x] F已按用户授权审定范围、职责、安全边界及分阶段冻结规则（非代码/实机通过）。
- [ ] D确认字段及安全规范；Z确认服务/驱动/隔离；L确认字段/硬件。
- [ ] B-01/B-06的具体密码协议/参数完成审核（**B-01 规范已交付待 D 签认**），B-05类型号与实物字段回填，无影响开工的未定项。
- [ ] B-01～B-07均收到相应真实证据；FAIL有owner、修复路线及截止时间，B-07发布风险完成处置。

| 签认人 | 确认范围 | 记录（日期/commit/PR或Issue） |
|---|---|---|
| F | 范围、权限、安全边界和阻塞处置；**密码协议方案（规范/向量/一致性检查）**；不含实现与实机结果 | 2026-10-26 范围确认；2026-10-28 密码协议交付（`docs/pbr-secure-v1/`，修订 2 + 向量 + mbedtls 2.28/3.6.5 各 7/7）；DR-01～DR-14 裁决见 `00-decisions-for-f.md` §6/§7 与 `F-REVIEW-2.md` |
| D | 控制帧/事件、身份、AEAD/TLS规范、路由 | 待本人确认 |
| Z | PIN/session、SQLCipher、worker IPC/OS隔离、UI边界 | 待本人确认 |
| L | 采购/电气、ESP TLS可行性、传感器与双路输入 | 待本人确认 |

验收顺序固定：IT-001 → 环境/身份/电气（IT-002/003/006）→ 协议/PIN/SQLCipher/隔离/业务加密（IT-004/005/007/012/013/014）→ 真实RFID与页面/分别数据（IT-008/009/010，E2E-001～004）→ 两模块分别运行（E2E-005）→ 连续30分钟同时运行及单槽故障（IT-011/E2E-006）→ Journey-001/F审核合并。

任何上游依赖未通过不得把下游演示记为正式验收；完整测试映射见[测试计划](04-dev-tasks-test.md)。F 第一天文档工作不等于 T-17/P0 完成。
