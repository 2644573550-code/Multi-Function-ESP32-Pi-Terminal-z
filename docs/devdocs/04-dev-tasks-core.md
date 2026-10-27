# 核心逻辑任务

路径所属仓库、当前事实和目标实现的区别见[总计划](04-dev-tasks.md#0-项目基线与代码现状)；接口/权限以[F审核稿](04-dev-tasks-f-day1.md)为准。F文档审定不替代各任务的实现、技术签认或实机验收。

## T-03 桥板/模块身份与密钥边界

| 属性 | 内容 |
|---|---|
| 主责 | Mr.D（协议/设备身份） |
| 协作 | Mr.F、Mr.L |
| 层级/审核 | 🔴 核心逻辑；`review_profile=audit` |
| TDD 模式 | 测试先行：身份唯一、撤销和错误身份测试→最小配对实现→复审密钥边界 |
| review_profile | `audit` |
| 依赖 | T-01 |
| 追溯 | F-002 / AC-002 / UT-001、IT-003 |
| 身份字段 | `bridge_id`、`device_id`、`slot_id`、`boot_id`、key_id/version |
| 文件范围 | 现有：`pi_terminal/src/core/ConnectionManager.cpp`、`HostEventDetector.h`；**依赖远端（不改动）**：`esp/S3_PiBridge_LinkIt_Host/main/Kconfig.projbuild`、`main/main.c` 的身份字段现状；新增Pi身份验证/信任存储接口（由F在T-01命名冻结） |

### 工作内容

- 桥板身份走项目统一签发路线；P0 只建立测试签发/验证闭环，不宣称生产 CA 完成。
- P0测试签发私钥仅保留在F控制的签发环境；Pi只持有验证公钥，桥板仅持有自己的逐板测试私钥/证书。禁止将issuer private key编进通用镜像或提交Git。
- 模块身份区分正式签发和 DIY 现场生成两条路线。
- P0盆栽与啸天各使用一份现场生成的唯一DIY测试身份；首次配对由Pi登记。正式签发模块路线仍需保留，但不把项目CA私钥发给普通用户。
- 身份撤销、重配、丢失设备处置。
- 私钥只入设备和 Pi 的受控秘密存储；禁止提交仓库。
- 现有 `CONFIG_APP_PI_DATA_TOKEN` 是业务session credential，不得复用为 bridge/module identity key、TLS key或BLE应用层密钥；私有 `sdkconfig` 不得提交。

### 测试方法

使用至少两块模块和一块桥板：错误密钥、身份冒用、撤销后重连、跨槽移动重新认证控制身份/位置；已有有效安全业务不因位置变化重启，旧槽新设备不能继承任务能力。

### 验收标准

- bridge_id、device_id 唯一且互不复用。
- 桥板只接受项目签发的测试身份。
- DIY 模块身份逐台唯一，第二台不能复用第一台的 key。
- 撤销后拒绝 attach、status、RFID 和业务事件。
- 扫描 PR 和日志，不含私钥/共享 secret。

### Review 要点

不得用 module_id、slot_id 或 RFID UID 作为密码学身份；桥板不得复用模块 DIY 密钥路线。

## T-04 用户、PIN和会话服务

| 属性 | 内容 |
|---|---|
| 主责 | Mr.Z |
| 协作 | Mr.F |
| 层级/审核 | 🔴 核心逻辑；`review_profile=audit` |
| TDD 模式 | 测试先行：PIN/session失败用例→服务实现→限速/撤销重构 |
| review_profile | `audit` |
| 依赖 | T-01（2026-10-28 解除对 T-03 的依赖：PIN 认证与桥板身份无代码交集） |
| 追溯 | F-003 / AC-003 / UT-002、IT-004 |
| 接口 | `UserService`、`AuthService`、`PinPolicy`、`SessionManager`；均待新增 |
| 文件范围 | 新增 `pi_terminal/src/security/UserService.{h,cpp}`、`AuthService.{h,cpp}`、`PinPolicy.{h,cpp}`、`SessionManager.{h,cpp}`及最小任务能力上下文；接入 `pi_terminal/CMakeLists.txt` 和 `src/app/TerminalApplication.{h,cpp}` |

### 工作内容

- 用户创建/停用，PIN 设置/验证/修改。
- PIN 至少 6 位；错误限速和临时锁定。
- 单一 `activeUserId`；退出和切换立即销毁旧会话能力。
- 前台会话与绑定启动用户的任务能力分开；锁屏撤销前台历史读取，任务仍可受限追加原用户记录。测量完成/明确取消或持续任务明确停止后才能切用户/退出；重启须原用户解锁及设备重新认证后恢复。
- 区分管理员与普通用户；管理员不得读取健康记录。
- PIN 服务由可信 C++ service 提供，QML 不持有密钥。

### 测试方法

UT 覆盖正确/错误 PIN、限速阈值、锁定超时、并发登录、切换和退出。

### 验收标准

- 正确 PIN 建立唯一当前用户会话。
- 错误 PIN 不泄漏账号是否存在或敏感认证细节。
- 达到尝试阈值后必须进入限速/锁定。
- 切换用户后旧 session token/API 调用全部拒绝。
- 锁屏后前台旧API拒绝，运行中任务只可向原用户追加新记录，不可读历史；切换前任务收尾，迟到事件不写新用户。
- 管理员请求成员健康数据被拒绝并留下不含敏感正文的审计记录。

### Review 要点

会话撤销必须服务端执行；不能仅依赖隐藏 UI 控件。

## T-07 测试用AEAD、重放保护、ACK和去重

| 属性 | 内容 |
|---|---|
| 主责 | Mr.D |
| 协作 | Mr.F、Mr.L |
| 层级/审核 | 🔴 核心逻辑；`review_profile=audit` |
| TDD 模式 | 测试先行：篡改/重放/TTL/ACK测试→AEAD状态机实现→独立安全审查 |
| review_profile | `audit` |
| 依赖 | T-03、T-05 |
| 追溯 | F-004 / AC-004 / UT-004、IT-007 |
| 接口 | `PbrSession`、`ReplayGuard`、`EventAckManager` |
| 文件范围 | 现有Pi：`pi_terminal/src/protocol/pibridge_protocol.cpp`、`src/transport/PogoControlTransport.cpp`；**依赖远端（不改动）**：`esp/S3_PiBridge_LinkIt_Host/main/main.c`、`main/rfid_service.c` 的帧格式；新增PBR安全封装/会话模块 |

### 工作内容

- P0 用项目签发的逐桥测试身份和逐模块测试身份建立双向认证。
- 建立测试 AEAD session；nonce/sequence/boot_id 绑定会话。
- 加入 `event_id`、ACK、有界重传、TTL、Pi 幂等去重。
- 无密钥/认证失败/AEAD tag 错误一律关闭；禁止降级到 CRC-only 业务帧。
- 提供仅含假密钥的测试向量和生成工具；不提供可用私钥。

### 测试方法

篡改 tag/载荷、伪造设备、重放、重复、乱序、丢 ACK、TTL 到期、Pi/桥板重启和 sequence wrap 测试。

### 验收标准

- 修改受保护字段后 AEAD 验证失败。
- 未签发桥身份和未配对模块均被拒绝。
- 旧事件重放不会触发第二次操作。
- 未 ACK 时最多重发配置的有限次数；过期事件丢弃。
- 重启后更新 boot/session identity，不复用 nonce。
- 安全失败不会接受 CRC-only 降级帧。

### Review 要点

不得自创算法；F 必须审核 AEAD 库、KDF、nonce 唯一性、key separation 和失败关闭语义。

## T-10 每用户SQLCipher最小存储（P0）

| 属性 | 内容 |
|---|---|
| 主责 | Mr.Z |
| 协作 | Mr.F |
| 层级/审核 | 🔴 数据安全；`review_profile=audit` |
| TDD 模式 | 测试先行：错误key/跨用户测试→SQLCipher实现→密钥访问复审 |
| review_profile | `audit` |
| 依赖 | T-04 |
| 追溯 | F-011 / AC-011 / UT-005、IT-012 |
| 文件范围 | Pi C++ trusted data service、SQLCipher C API 链接配置（**不用 Qt SQL 插件**）、DB schema/migration |
| 代码路径 | 新增 `pi_terminal/src/security/SqlCipherStore.{h,cpp}`、`UserDatabase.{h,cpp}`；更新 `pi_terminal/CMakeLists.txt`（`find_library(SQLCIPHER_LIB sqlcipher)` + `-DSQLITE_HAS_CODEC`）及DB/schema测试。**不走 `QSqlDatabase`**：Qt 6.5.3 SDK 无 SQLCipher 驱动、无 `qsql_sqlite_p.h`，重编 QSQLITE 不可行（2026-10-28 F 裁决） |

### 工作内容

- 第 1 天验证 SQLCipher C API 可链接：`sqlite3_key` 打开、`PRAGMA cipher_version`、错误 key 返回 `SQLITE_NOTADB`(26)；形成通过日志。**不再验证 Qt driver 加载**——路线已改为 C API。
- 每用户独立 SQLCipher DB，密钥只由可信 service 管理。
- 每用户生成随机 DB key；用户 PIN 经成熟KDF派生KEK后只封装/解封DB key，PIN本身不作SQLCipher key。
- 登录后key只在trusted service短期内存态；PIN修改在验证旧PIN后重新封装DB key，不重加密整库。
- P0验证错误PIN/错误KDF输出无法解封；独立离线恢复凭证和完整恢复路径作为P1，不得伪称P0已可恢复。
- 模块 worker/QML 不得获得 DB 文件、密钥或任意 SQL。
- 当前盆栽`module-devkit/examples/smart-planter/{module.cpp,History.h}`使用按实例索引的明文JSON历史，须同T-11/13改为可信SQLCipher数据API并禁用P0旧写入。旧记录无用户归属，不自动导入首个用户、不自动删除；用户确认迁移/删除另留记录。
- 实现最小用户数据 API 和跨用户拒绝。
- 本 P0 不要求完整 USB 备份/恢复、恢复码和生命周期删除流程；这些在 P1。

### 测试方法

以两个用户写入不同标记数据；用用户 A context 查询 B、用错误 key 打开 DB、检查 DB/WAL/log/temp 文件。

### 验收标准

- 每用户 DB 加密；无密钥/错误 key 无法读取。
- SQLCipher DB key为随机值且与PIN不同；PIN只用于KEK封装。
- A context 无法查询、改写 B 的记录。
- lock后旧前台session API不能继续访问DB；仅原有受限任务能力可由可信服务追加原用户记录；任务停止/退出后所有旧能力拒绝。
- 原始 UID、PIN、明文健康值不得出现在应用日志。
- Pi 3B 上驱动加载、DB 打开和关键 PIN/查询延迟测量记录在 PR。

### Review 要点

存储路线已由 F 裁决（2026-10-28）：直接调 SQLCipher C API，不走 Qt SQL 插件。不得把普通 SQLite 标记成 SQLCipher 完成；CMake 找不到 `libsqlcipher` 时须 `FATAL_ERROR`，不得静默降级。

## T-11 基础worker进程隔离与默认拒网（P0）

| 属性 | 内容 |
|---|---|
| 主责 | Mr.Z |
| 协作 | Mr.D（IPC/进程身份）、Mr.L（ESP网络模式）、Mr.F（安全审查） |
| 层级/审核 | 🔴 进程/网络安全；`review_profile=audit` |
| TDD 模式 | 拒绝性IT先行：worker访问DB/文件/WAN测试→隔离实现→Pi实机重测 |
| review_profile | `audit` |
| 依赖 | T-03、T-04、T-10 |
| 追溯 | F-012 / AC-012 / IT-013 |
| 文件范围 | Pi module launch/IPC、worker uid/目录权限、systemd unit/profile、Pi hotspot/WAN rules；**ESP 网络配置归 L（不改动）** |
| 代码路径 | 现有Pi：`pi_terminal/src/core/ModuleManager.{h,cpp}`、`src/modules/api/IModulePlugin.h`、`src/modules/api/pi_module_api.h`；新增 `src/core/ModuleWorkerLauncher.{h,cpp}`、IPC协议和最小worker入口；Pi安装配置新增systemd unit/profile |

### 工作内容

- P0 最小要求：模块代码不再在宿主进程内直接以 `QLibrary` 加载执行；至少独立低权限 worker + 有界版本 IPC。
- 现有盆栽插件/历史及任意QML页面不能原样视作隔离完成；C ABI业务迁至worker/可信服务，宿主仅运行受控UI，T-13同步改造并补锁屏任务能力测试。
- worker 使用非特权 UID；无数据库/任意文件读权限；仅访问专用运行目录和必要设备句柄。
- Pi 热点/WAN forwarding 默认拒绝；worker 默认无公网 egress；ESP 产品默认配置不启用任意外网 STA。
- 只做默认拒绝和策略验证；域名代理、复杂动态授权、完整 syscall/seccomp hardening 列 P1。

### 测试方法

启动真实/测试 worker；尝试读取用户 DB、主进程配置、`/home`、本机管理服务、WAN 地址；检查拒绝并验证宿主继续运行。拔网/撤销策略后检查现有连接。

### 验收标准

- worker PID 与宿主不同、运行 UID 非特权。
- worker 无法读取 SQLCipher 文件、密钥、Pi 主用户目录。
- worker 默认无法访问公网；Pi 默认不转发热点流量到 WAN。
- worker 崩溃不导致 Qt 主程序崩溃，且 session/资源可回收。
- IPC 限制消息长度、schema、命令能力和请求超时。

### Review 要点

`QProcess` 本身不等于沙箱；检查 UID、文件权限、网络策略和 systemd 配置实际生效。测试应证明拒绝，而非只检查配置文本。
