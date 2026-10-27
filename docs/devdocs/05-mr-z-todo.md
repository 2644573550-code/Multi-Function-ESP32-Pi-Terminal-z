---
generated_by: dev-tasks
spec_version: 1.3
generated_at: 2026-10-28
owner: Mr.Z
source: docs/devdocs/04-dev-tasks*.md; docs/pbr-secure-v1/*; Pi main 82db652
---
# Mr.Z 工作清单（P0）

> 个人执行清单，不替代 [总计划](04-dev-tasks.md) 与各任务卡。冲突时以任务卡和 [F 审核稿](04-dev-tasks-f-day1.md) 为准。

**基线**：Pi `82db652`（2026-10-28，PR #30 / T-05 控制 schema）。
上一版清单基于 `f9f109a`，本次上游共 8 个 commit / 25 个文件，`pi_terminal/` 代码**有**变动（新增 `control_event.{h,cpp}`、`protocol_test.cpp` +84 行）。

**本机环境**：x86_64 / Ubuntu 26.04，**不是**目标 Pi 3B / ARM64 Bookworm / Qt 6.5.3 / Wayland。
本机无编译器、无 cmake、无 Qt、无 libsqlcipher。以下所有验收都必须在 Pi 实机复核，本机结果只能作开发预检。

**现状标注**

- ✅ 已核实：代码中不存在，需从零实现
- ⚠️ 文档与代码不一致，需先裁决
- ❓ 需实机确认，无法仅凭代码判断
- 📋 仅文档描述，本轮未核查

---

## 0. 上一版之后的四项范围变更

> 来自 `04-dev-tasks.md` 顶部修订与 `docs/pbr-secure-v1/`，直接影响你的排期。

1. **分工边界（2026-10-28 修订）**：**D 和 Z 只做 Pi 终端软件，不改 ESP 固件仓库。L 负责 ESP 固件，等 Pi 软件完成后再开工。** 采购/接线/电气核验不受限。
   → 任务卡里出现的 `esp/` 一律是「依赖该仓库现状」，不是你要改的。
2. **D 现在被卡在你身上**：`uidIndexKey()` 归 Z 实现，T-08 后半段与 T-12 都等 Z。
   → 你的 T-04 是全队关键路径，不只是你自己的进度。
3. **`deriveUidIndex` 签名已冻结**（DR-14）：D 实现并调用，**密钥生成/存储/轮换归你**。
4. **B-01 密码协议已由 F 交付**（规范修订 2 + 向量 + mbedtls 2.28/3.6.5 各 7/7），但**等 D 签认，未冻结**。与你的 T-10 KDF 参数无直接依赖，但 B-06 仍待 F 审定。

---

## 一、第 1 天预检回执（已逾期）

> [F 审核稿第 8 节](04-dev-tasks-f-day1.md#8-第一天预检回执与阻塞台账)要求当日提交。回执格式固定：
> `B编号 / owner / commit / 目标硬件与版本 / 命令与脱敏结果 / PASS或FAIL或NOT_RUN / 风险 / 修复动作 / 截止日 / PR链接`
> 没有执行日期和实际结果的模板不算证据。

### B-02 Z 部分：SQLCipher 驱动与 Qt 构建环境

- [ ] ✅ **确认目标 Qt 6.5.3 能否加载/构建 SQLCipher driver**，交实测日志
  - Qt 官方发行版不带 SQLCipher driver，需自编译 `QSQLITE` 插件替换
  - 失败即 T-10 阻塞，须当日上报 F，**不得用普通 SQLite 冒充**
- [ ] ✅ **记录实际加载库与 `cipher_version`**（`PRAGMA cipher_version`）
- [ ] ✅ **验证错误 key 被拒绝**，记录打开耗时
- [ ] ✅ **记录 Pi OS / Qt / plugin path**
  - 注意：目标 Qt 是 **6.5.3**，历史实机记录是 6.4.2，CMake 最低 6.4；三者需在回执里分清
- [ ] 📋 **验证 Pi 侧 SDK 来源/哈希清单可构建**（D 交清单，Z 验证）
  - ESP 侧条目现在归 L 提交，D 只消费路径与哈希，你只验证 Pi 侧那一半

### B-03：worker 隔离

- [ ] ✅ **记录 worker 与宿主 PID/UID**（当前无 worker，需先有最小实现才能测）
- [ ] ✅ **测 worker 访问 DB / `$HOME` / 管理端口 / WAN 全部被拒**
  - 仅 `QProcess` 或配置文本**不算通过**，须 OS 层实测
- [ ] ✅ **确认热点不转发 WAN，宿主自身网络仍可用**

### B-06：PIN / KEK 参数

- [ ] 📋 **选定成熟 KDF 库与参数**（Argon2id 或 PBKDF2，不得裸哈希存 PIN）
- [ ] 📋 **测 Pi 上 KDF 延迟与内存**
- [ ] 📋 **确认锁定状态持久化方案**（重启不清除有效锁定）
- [ ] 📋 **交首次管理员初始化方案**（本地受控，无默认 PIN/万能账号）
- [ ] ⚠️ **参数须经 F 审定后回填**，不得自行降低 KDF 强度

---

## 二、Z 主责任务

### 新增：`uidIndexKey()` — 解除 D 的阻塞

> 来自 `F-REVIEW-2.md` §5.3 / DR-14。签名已冻结，**D 只调用，你负责实现与密钥生命周期**。

- [ ] ✅ **新建 `pi_terminal/src/security/uid_index_key.h`**
  ```cpp
  bool uidIndexKey(QByteArray *out32);   // 成功 true；密钥不可用时 false
  ```
- [ ] ✅ **密钥生成/存储/轮换归你**（T-04/T-10 可信存储域），D 不接触、不缓存、不落盘
- [ ] ✅ **密钥缺失时 RFID 路径失败关闭**，不退回明文 UID 比较
  - D 的 `deriveUidIndex()` 返回空 `QByteArray` 即 `INVALID_MESSAGE`
- [ ] ❓ **确认密钥存放位置**：是并入每用户 DB 域、还是独立的整机密钥？F 未明确到文件级

**注**：这项不在 T-04/T-10/T-11 任一任务卡内，是 PR #30 之后新增给 Z 的交付物。D 侧已用固定测试密钥 stub 自测，你交付后他切真实现。

### T-04 用户、PIN 和会话服务（🔴 audit）

> 依赖 T-01、T-03 · 追溯 UT-002、IT-004 · 测试先行
> **现在是全队关键路径**：T-12、T-15 等 Z 的 T-04

- [ ] ✅ **新建 `pi_terminal/src/security/`**
  - `UserService.{h,cpp}`、`AuthService.{h,cpp}`、`PinPolicy.{h,cpp}`、`SessionManager.{h,cpp}`
- [ ] ✅ **接入 `pi_terminal/CMakeLists.txt` 与 `src/app/TerminalApplication.{h,cpp}`**
- [ ] ✅ **用户创建/停用，PIN 设置/验证/修改**，至少 6 位 ASCII 数字
- [ ] ✅ **错误限速与临时锁定**：每账号连错 5 次锁 60 秒；终端级 1 次/秒
  - 未知账号同样计入；错误提示不区分「账号不存在」与「PIN 错误」
  - 重启不可清除有效锁定；正确认证才清除该账号失败计数
- [ ] ✅ **单一 `activeUserId`**，退出/切换立即销毁旧会话能力
- [ ] ✅ **前台会话与任务能力分离**
  - 锁屏撤销前台历史读取，任务仍可向原用户受限追加
  - 测量完成/显式取消/明确停止后才能切用户
  - 迟到事件不得写入新用户
- [ ] ✅ **管理员与普通用户区分**，管理员不得读他人健康记录
- [ ] ✅ **PIN 服务在可信 C++ 层**，QML 不持有密钥
- [ ] ✅ **写 UT-002**：正确/错误 PIN、限速阈值、锁定超时、并发登录、切换、退出
- [ ] ❓ **服务端执行会话撤销**，不能只隐藏 UI 控件

**验收**：正确 PIN 建唯一会话；错误不泄漏账号存在性；达阈值锁定；切换后旧 token 全拒；管理员请求成员健康数据被拒并留审计记录。

### T-10 每用户 SQLCipher 最小存储（🔴 audit）

> 依赖 T-04 · 追溯 UT-005、IT-012 · 测试先行

- [ ] ✅ **新建 `SqlCipherStore.{h,cpp}`、`UserDatabase.{h,cpp}`**
- [ ] ✅ **CMake 加 `Qt6::Sql` 与 driver 配置**（当前 `CMakeLists.txt` 无 `Qt6::Sql`）
- [ ] ✅ **每用户独立 SQLCipher DB**，密钥只由可信 service 管理
- [ ] ✅ **每用户随机 DB key；PIN 经 KDF 派生 KEK 只用于封装/解封 DB key**
  - PIN 本身不作 SQLCipher key
- [ ] ✅ **登录后 key 只在 trusted service 短期内存态**
- [ ] ✅ **改 PIN 时验证旧 PIN 后重新封装 DB key，不重加密整库**
- [ ] ✅ **最小用户数据 API + 跨用户拒绝**
- [ ] ✅ **模块 worker / QML 拿不到 DB 文件、密钥或任意 SQL**
- [ ] ⚠️ **同步改造盆栽明文 JSON**（T-11/T-13 联动）
  - `module-devkit/examples/smart-planter/History.h` 当前是明文 JSON（`QJsonDocument`），须改为可信 SQLCipher 数据 API 并禁用 P0 旧写入
  - 旧记录无用户归属：**不自动导入首个用户、不自动删除**，另留迁移记录
- [ ] ✅ **写 UT-005**：A/B 各写标记，A 读 B、错误 key 打开、退出后重用旧 session
- [ ] ❓ **测 DB / WAL / log / temp 无 PIN、raw UID、明文健康值**
- [ ] 📋 **记录 Pi 3B 上驱动加载、DB 打开、关键 PIN/查询延迟**

**验收**：每用户 DB 加密；DB key 随机且异于 PIN；A 无法查改 B；锁屏后旧前台 API 拒访；日志无敏感值。

### T-11 基础 worker 进程隔离与默认拒网（🔴 audit）

> 依赖 T-03、T-04、T-10 · 追溯 IT-013 · 拒绝性集成测试先行

- [ ] ✅ **模块代码移出宿主进程**
  - `ModuleManager.cpp:31` 当前 `QLibrary::setLoadHints`，同进程加载；须改为独立低权限 worker + 有界版本 IPC
- [ ] ✅ **新建 `src/core/ModuleWorkerLauncher.{h,cpp}` + IPC 协议 + 最小 worker 入口**
- [ ] ✅ **worker 使用非特权 UID**，无 DB / 任意文件读权限，只访问专用运行目录和必要设备句柄
- [ ] ✅ **新增 systemd unit / profile**（仓库当前**无任何** `.service` 文件）
- [ ] ✅ **Pi 热点/WAN forwarding 默认拒绝**，worker 默认无公网 egress
- [ ] ✅ **ESP 网络配置归 L**（不改动，仅需知其约束）
- [ ] ✅ **IPC 限制消息长度、schema、命令能力、请求超时**
- [ ] ✅ **盆栽 C ABI 业务迁至 worker/可信服务**，宿主只跑受控 UI
- [ ] ❓ **worker 崩溃不带崩 Qt 主程序**，session/资源可回收
- [ ] 📋 **补锁屏任务能力测试**（与 T-13 联动）

**验收**：worker PID ≠ 宿主、UID 非特权；读不到 DB/密钥/`$HOME`；无公网；崩溃隔离。
**注意**：`QProcess` 本身不是沙箱，必须证明 UID、文件权限、网络策略、systemd 实际生效。

### T-09 Qt 登录、绑卡、锁屏、挂失（🟢 guarded）

> 依赖 T-04、T-08、T-10 · 追溯 E2E-002 · Pi/Wayland/Qt 6.5.3 实机

- [ ] ✅ **新增登录/锁屏/绑卡 QML 组件**（`qml/` 下 login/lock/user/pin 组件数 = **0**）
- [ ] ✅ **在 CMake 资源表登记**
- [ ] ✅ **PIN 页面**、低敏用户选择、错误/离线状态
- [ ] ✅ **锁屏**：无交互 5 分钟自动锁定；隐藏敏感内容；提示/语音不含姓名、读数、精确位置
  - 锁屏后必须重输 PIN，**不得刷卡解锁**
- [ ] ✅ **显式绑卡**：已登录 + 重验 PIN + 30 秒单次窗口；已绑他人的卡拒绝
- [ ] ✅ **管理员挂失**：重验管理员 PIN 后可撤销成员卡映射，但不能借此解锁 DB
- [ ] ✅ **切用户前显示任务完成/取消/明确停止流程**
- [ ] ❓ **QML 只调可信服务**，所有操作由服务端再次校验会话/权限
- [ ] ❓ **Wayland 实机触摸 + 键盘完成闭环**

**验收**：刷卡不直接登录；PIN 正确才登录；绑卡须短时窗口；管理员不能读他人健康数据。

### T-02 Z 部分：Qt 构建环境（⚪ audit，与 D 共担）

> 依赖 T-01 · 追溯 IT-002

- [ ] ❓ **在 Pi 3B / 64-bit Bookworm / Qt 6.5.3 / Wayland 上构建主项目**
- [ ] ❓ **记录内核、编译器、Qt plugin path、串口权限组**
- [ ] ⚠️ **Pi 服务不得以 root 运行** — 与 T-11 非特权 UID 一并核对

---

## 三、Z 协作项

> 主责在他人，你负责其中用户/会话/页面/授权部分。
> 修订后 D 只做 Pi 侧，L 等软件完成后才动固件——你的协作项基本都在 Pi 侧。

### T-08 RC522 到 Pi 安全事件闭环

> 主责 D（PBR/事件链路）+ Z（用户匹配回调与密钥）· 依赖 T-04、T-07

- [ ] ✅ **`uidIndexKey()` 已在上方「新增」列出，优先交付以解 D 的阻塞**
- [ ] ✅ **UID 匹配回调**：用 `deriveUidIndex()` 结果选用户（D 实现并调用，你提供密钥）
- [ ] ✅ **选中用户后只显示 PIN 页**，不直接登录
- [ ] ✅ **raw UID 只短暂驻留内存**，不进 DB / log / QSettings；QML / worker 只见选择结果
- [ ] ❓ **验证未配对/篡改/重复/过期事件全部拒绝**
- [ ] ❓ **确认 UID hex 大小写都接受**（校验时折叠，`deriveUidIndex` 收原始字节）

### T-12 模块业务数据 TLS/BLE 安全传输（Z 部分）

> 主责 D（**Pi 侧**对端）· Z 负责 Pi TLS/BLE 会话及用户授权 · **D 明确在等你**
> **ESP 侧 TLS 归 L，等软件完成后对接**

- [ ] ✅ **Pi 端 TLS 校验**（`HotspotLanTransport.cpp` 当前 SSL/TLS 代码行数 = **0**）
  - TLS ≥ 1.2；指纹白名单以 profile 为准、QSettings 为缓存
- [ ] ✅ **撤销立即断连并销毁派生密钥**
- [ ] ✅ **Pi 端 BLE 安全**（`BleTransport.cpp` 当前无认证/加密）
  - P0 建议固定 LAN-only 并显式禁用 BLE profile，须验证禁用生效
- [ ] ❓ **断线 / 切用户 / 撤权后旧 session 不可继续发数据**
- [ ] ❓ **传输密钥与身份密钥分离**，不复用 `CONFIG_APP_PI_DATA_TOKEN`
- [ ] 📋 **`TLS 会话 ≠ 设备已认证` 写进代码注释与文档**

### T-13 / T-14 / T-15 / T-16 页面与会话权限（Z 部分）

> 修订后这几项的主责明确拆为：L 固件、D Pi 侧接口/字段/路由、Z 页面/UI。
> **你不碰固件。**

- [ ] ✅ **T-13 用户归属/UI**：盆栽全部传感器数据按用户归属显示；退出后旧用户上下文不能读取
- [ ] ✅ **T-14 页面/会话权限**：啸天受控状态 UI 复用安全业务 lane，不当作已有插件
  - 啸天 `firmware/` 尚无实现（L 负责），你只做页面
- [ ] ✅ **T-15 两模块分别运行**：用户/device_id/slot_id 不丢失
- [ ] ✅ **T-16 双槽同时运行**：两个 slot/device/event/user context 不串；单槽拔出不影响另一槽
- [ ] ❓ **补测离槽继续、旧槽换设备不继承授权、桥失联时本机 PIN 解锁查看本人任务**

---

## 四、T-01 签认

> [IT-001](04-dev-tasks-f-day1.md#9-it-001-审查签认与验收顺序) 中 Z 行仍为「待本人确认」。
> F 文档审定 + 密码协议交付**不**替代你的技术签认。

- [ ] ✅ **签认确认范围**：PIN/session、SQLCipher、worker IPC/OS 隔离、UI 边界
  - 记录格式：日期 / commit / PR 或 Issue
- [ ] 📋 **确认权限矩阵**（F 审核稿第 2 节）与实现一致
- [ ] 📋 **确认设备生命周期语义**（锁屏不停任务、切前收尾、迟到事件不串档）
- [ ] 📋 **确认 `uidIndexKey` 密钥归属切分**（DR-14）你接受
- [ ] 📋 **回执内容确认后再签认**，不预签不存在的能力

---

## 五、阻塞项速查

| 阻塞 | 影响 | 处置 |
|---|---|---|
| SQLCipher driver 不可用 | T-10 全线 | 当日上报 F，不得 SQLite 冒充 |
| KDF 参数未审定 | T-04 / T-10 无法定验收 | 提交测量，等 F 回填 |
| `uidIndexKey()` 未交付 | **D 的 T-08 后半段、T-12** | 优先做，见「新增」节 |
| `QLibrary` 迁 worker | T-11 关键路径 | 先做最小边界，不追完整 seccomp |
| BLE 无法强制加密 | T-12 | P0 固定 LAN-only，验证禁用 |
| 本机无工具链 | 全部代码任务 | 需先装编译器/cmake/Qt 才能验证 |
| 本机非 Pi | 全部验收 | 本机只预检，实机补证 |

**明确不在 P0**：生产 CA、完整备份恢复、完整 systemd/seccomp 加固、啸天测速与户外、正式 NFC 卡、血压计、OTA。
**明确不在 Z 范围**：ESP 固件（归 L，等 Pi 软件完成后开工）。

---

## 附：上游本次变更摘要（`f9f109a` → `82db652`）

| 类别 | 内容 |
|---|---|
| 代码 | 新增 `pi_terminal/src/protocol/control_event.{h,cpp}`（183+22 行）；`protocol_test.cpp` +84 行；`HostEventDetector.h` +6；`PogoControlTransport.cpp` +7；`CMakeLists.txt` +2 |
| 规范 | 新增 `docs/pbr-secure-v1/`：`pbr-secure-v1.md`（513 行）、`F-REVIEW-2.md`、`PR-NOTES.md`、`vectors.json`、`gen_vectors.py`、`mbedtls_check.c`、`00-decisions-for-f.md` +263 |
| 分工 | D/Z 只做 Pi 软件；L 负责 ESP 固件且等软件完成；T-12～T-16 主责按 Pi 侧/固件侧拆分 |
| 预检 | B-01 部分回执（F 交付规范，等 D 签认）；B-07 记为遗留不阻塞 |
| 路径 | `mock_bridge.py` 从 `tools/` 修正为 `pi_terminal/tools/` |
