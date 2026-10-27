# Mr.Z 工作清单（P0）

> **手工维护**：本文件不是生成物。首版由 dev-tasks 产出（2026-10-28），此后按人工修订维护，重跑任何生成器都会覆盖本条。
>
> 个人执行清单，不替代 [总计划](04-dev-tasks.md) 与各任务卡。冲突时以任务卡和 [F 审核稿](04-dev-tasks-f-day1.md) 为准；**接口与参数以本文 §0 为准**（§0 是 F 2026-10-28 的裁决，任务卡尚未逐条回填）。

**基线**：Pi `9a3d221`（2026-10-28，main，含 PR #30 T-05 控制 schema 与 PR #32 时间工具）。

**进度**：今天是 **Day 3**。P0 起算日为 2026-10-26，Day 14 = **2026-11-08**。

**排序原则**：按依赖排序，不按日期。上游前置满足即可超前做，卡住的才等。下方「Day 闸门对照」只标项目级时间点。

**本机环境（2026-10-28 实测，已修正上一版）**：Pop!_OS 22.04 / x86_64。**不是**目标 Pi 3B / ARM64 Bookworm / Wayland。

| 工具 | 实测 |
|---|---|
| gcc / g++ | 有（11.x） |
| cmake | 有（4.4.3，`~/.local/bin`） |
| Qt | **有**：6.5.3 `~/.local/share/orange-pi-terminal-sdk/qt/6.5.3/gcc_64`，`source pi_terminal/scripts/env.sh` 后可用 |
| `libQt6Sql` + `libqsqlite.so` | 有 |
| SQLCipher | 无系统包，但 `apt-get download libsqlcipher-dev libsqlcipher0` 解包即用（已实测，见 §0.1） |
| Qt 源码 / `qsql_sqlite_p.h` | **无**——所以「重编 QSQLITE 插件」这条路走不通 |
| argon2 / openssl 开发头 | 无（只有运行时 `libargon2.so.1`、`libssl.so.3`） |
| autoconf / automake / libtool | 无 |
| `nmcli` | 无（热点脚本无法在本机跑） |

结论：**代码可以在这里写、编译、跑单测**；只有需要真 UID、真 systemd、真 BlueZ、真触摸的项必须上 Pi。上一版写「本机无编译器、无 cmake、无 Qt」是错的，会让人以为一行代码都写不了。

**与审查文档的关系**：本文的七项冻结参数在 `docs/pbr-secure-v1/F-REVIEW-2.md` **§5.3.2** 有同一份表述（与 D 的 §5.3.1 对称）。两份若不一致，以 F-REVIEW-2 为准并提 PR 修本文。

**现状标注**

- ❌ 代码中不存在，需从零实现（上一版用 ✅ 表示这个意思，与「已完成」的直觉相反，已换掉）
- ✅（配 `[x]`）**已由 F 裁决或已核实**，不用你做，只需按此实现
- ⚠️ 文档与代码不一致
- ❓ 需实机确认（真 UID / systemd / BlueZ / Wayland / Pi 硬件）
- 📋 仅文档描述，本轮未核查

---

## 0. F 裁决（2026-10-28 冻结，按此实现，不必再问）

这七条是**决定，不是建议**。有疑问按此做；确实做不了时提 PR 附实测数据，不要自行降级。

### 0.1 T-10 存储：直接调 SQLCipher C API，不走 Qt SQL 插件

**为什么**：Qt 6.5.3 SDK 只有 `libqsqlite.so`（普通 SQLite），没有 SQLCipher 驱动，也没有 `qsql_sqlite_p.h` 私有头；本机连 autoconf/automake/libtool 都没有。重编 `QSQLITE` 插件需要整套 Qt 源码，Pi 3B 上不划算，收益只是 `QSqlQuery` 的语法糖。

**怎么做**：

```cpp
// pi_terminal/src/security/SqlCipherStore.{h,cpp}
// 编译：-DSQLITE_HAS_CODEC ；链接 libsqlcipher
// 打开：sqlite3_open -> sqlite3_key(db, key, 32) -> PRAGMA cipher_memory_security
```

| 要求 | 值 |
|---|---|
| 每用户独立 DB 文件 | 是，文件名不含用户名/UID 明文，用随机 ID |
| DB key | 每库 32 字节 `QRandomGenerator::system()` |
| key 存放 | 只经 KEK 封装后落盘，KEK 由 PIN 走 PBKDF2 派生 |
| PIN 直接当 DB key | **禁止** |
| 错误 key | 必须返回 `SQLITE_NOTADB`(26) 并映射为 `STORAGE_UNAVAILABLE`，不得 fallback 到明文 |
| 测试替身 | **禁止**用普通 SQLite 顶替。本机无包时用 `apt-get download` 解包，见下 |

**本机已验证的取证方式（免 sudo，2026-10-28 实测）**：

```bash
cd /tmp && mkdir -p sqlc && cd sqlc
apt-get download libsqlcipher-dev libsqlcipher0
for d in *.deb; do dpkg -x "$d" root; done
gcc t.c -DSQLITE_HAS_CODEC -I root/usr/include \
    -L root/usr/lib/x86_64-linux-gnu -lsqlcipher -o t
LD_LIBRARY_PATH=$PWD/root/usr/lib/x86_64-linux-gnu ./t
```

实测结果：正确 key `rc=0`；错误 key `rc=26 (file is encrypted or is not a database)`；`PRAGMA cipher_version` = `3.4.1`；DB 文件头非 `SQLite format`。**这条路径你现在就能在本机跑通，不用等 Pi。**

**依赖获取**：Pi 上 `sudo apt install libsqlcipher-dev`（Bookworm 有 3.4.1）。CMake 用 `find_library(SQLCIPHER_LIB sqlcipher)` + 回退到 `SQLCIPHER_ROOT`，**不硬编码路径**；找不到时 `message(FATAL_ERROR)`，不静默降级成 SQLite。

### 0.2 PIN 的 KDF：PBKDF2-HMAC-SHA256，迭代 ≥600,000

用 Qt 自带 `QPasswordDigestor::deriveKeyPbkdf2`（`Qt6::Network`，零新依赖，现在可测）。

| 参数 | 值 |
|---|---|
| 算法 | PBKDF2-HMAC-SHA256 |
| 迭代 | **生产 600,000 起**，硬编码下限，**不得由配置或环境变量降低** |
| salt | 16 字节，`QRandomGenerator::system()`，每用户唯一 |
| 输出 | 32 字节 KEK |
| 单元测试 | 可降到 1,000 做快速用例，但须断言"生产常量 ≥ 600000" |
| 裸哈希存 PIN | 禁止 |
| Argon2id | P0 不用。代码留注释：`shortcut: PBKDF2 而非内存硬 KDF，Pi 实测延迟有余量时升级 Argon2id` |

**为什么不是 Argon2id**：本机无 `argon2.h`，Pi 要多一条依赖；PBKDF2 是 Qt 自带、可立刻验证、OWASP 现行推荐值明确。安全性差距在 P0 的威胁模型（离线无限猜 PIN 已明示不防）下不构成降级。

### 0.3 依赖解耦：T-04 不等 T-03，T-09 拆 a/b

原依赖图里 D 和 Z 互相堵死（T-04→T-03 是 Z 等 D，T-09→T-08 又是 Z 等 D，而 D 又在等 Z 的 `uidIndexKey` 和 T-04）。已拆除：

| 任务 | 原依赖 | 现依赖 | 理由 |
|---|---|---|---|
| **T-04** | T-01、T-03 | **T-01** | PIN 认证与桥板身份在代码上无交集 |
| **T-09a** PIN/锁屏/切用户 | T-04、T-08、T-10 | **T-04、T-10** | 不需要 RFID |
| **T-09b** 绑卡/挂失 | 同上 | **T-07、T-08、T-09a** | 只有这段真需要卡 |
| T-03 | T-01 | T-01 | 不变（D） |
| T-08 | T-04、T-07 | T-04、T-07 | 不变（D） |

T-09 仍是**一张卡**，内部标 a/b 两段，不新增任务编号。总表与追溯矩阵编号不变。

**你因此今天可开工**：T-04（§3 D4）、T-09a（§3 D9 前半）、D1 的 `uidIndexKey()`，都不再等 D。

### 0.4 worker 隔离：systemd `DynamicUser=yes` + `PrivateNetwork=yes`

不建持久账号。unit 里声明，systemd 启动时分配临时 UID。

```ini
# pi_terminal/packaging/orange-pi-worker@.service
[Service]
DynamicUser=yes
PrivateNetwork=yes
PrivateTmp=yes
ProtectHome=yes
ProtectSystem=strict
NoNewPrivileges=yes
RestrictAddressFamilies=AF_UNIX
```

| 判定项 | IT-013 通过条件 |
|---|---|
| worker PID 的 UID | **≠ 宿主 UID**（不要求固定值） |
| unit 生效 | `systemd-analyze verify` 通过，`systemctl show` 里 DynamicUser=yes |
| 无公网 | worker 内 `curl` 外网失败；宿主自身网络仍正常 |
| 读不到 DB/`$HOME` | `cat` 相应路径失败 |
| 崩溃隔离 | kill worker 后宿主存活、资源回收 |

安装期需要**一次** sudo 把 unit 装进 `/etc/systemd/system/`；运行期不再提权。这与 `setup-desktop.py` 的"绝不 sudo"原则不冲突——那个脚本装的是用户态 SDK，不是 system service。

`QProcess` 本身**不是**沙箱，只起进程不加这些声明一律判 FAIL。

### 0.5 BLE：保留，用 BlueZ 配对做状态门控

Qt 6.5.3 的 `QLowEnergyController` **没有任何 pair/encrypt 接口**（逐行查过头文件）。Qt 唯一能触到加密的地方是 `QBluetoothLocalDevice::requestPairing()`，它把配对交给 BlueZ，成功后由内核强制链路加密。

**T-12 的 BLE 验收标准据此改写**：

**实现归属**：`BleTransport`/`HotspotLanTransport` 在任务卡里是 **D 的文件范围**（T-12 主责 D）。下面这张表是给 D 的实现要求，Z 只做传输层之上的会话门控。

| 要求 | 做法 |
|---|---|
| 发数据前必须已配对 | `QBluetoothLocalDevice::pairingStatus(addr) != Paired` → 拒绝 `writeCharacteristic`，返回 `TRANSPORT_SECURITY_REQUIRED` |
| 未配对不降级 | 不得 fallback 到明文 GATT；不得"先发再补配对" |
| 配对失败 | 断开并记录，不重试超过 3 次 |
| 适用范围 | **仅 `transport_mode == "fixed_ble"` 的模块**走 BLE |
| 与 LAN 并存 | `fixed_hotspot_lan` 模块仍强制 LAN TLS；两类不混用 |
| Pi 侧实现 | **归 D**（`BleTransport` 在 T-12 的 D 文件范围内）。Z 只做传输层之上的会话门控 |
| ESP 侧 | **归 L**，等 Pi 软件完成后再对 |
| 实机验收 | 需真 BlueZ + 真模块，标 ❓ |

`QBluetoothLocalDevice::requestPairing()` 在部分 BlueZ 配置下需要 PolicyKit 授权。若 Pi 上实测失败，**提 PR 说明并附 `journalctl` 输出**，不要改成明文放行。

**现状提醒**：仓库里目前**没有任何 `fixed_ble` 的真实模块 manifest**（盆栽是 `fixed_hotspot_lan`）。所以这段代码写完只有单测能跑，实机验收推到有 BLE 模块时。

### 0.6 `uidIndexKey`：整机单钥，0600 文件

```cpp
// pi_terminal/src/security/uid_index_key.{h,cpp}
bool uidIndexKey(QByteArray *out32);   // 成功 true；不可用 false
```

| 项 | 冻结值 |
|---|---|
| 位置 | `QStandardPaths::AppConfigLocation + "/uid_index.key"`，权限 **0600**。**不得放 `AppLocalDataLocation`**：`ModuleManager::dataDirectory()` 正是把该目录交给模块（`ModuleManager.cpp:61-65`），放那里等于让每个模块都能读到整机 UID 密钥 |
| 数量 | **整机一个**，不是每用户一个 |
| 生成 | 首次使用时 `QRandomGenerator::system()` 32 字节 |
| 轮换 | 提供显式接口，轮换后**所有旧 UID 索引失效**，须重新绑卡；不得静默迁移 |
| 缺失/不可读 | 返回 `false`；调用方（D 的 `deriveUidIndex`）返回空 → `INVALID_MESSAGE`，**失败关闭** |
| 落日志/DB/QSettings | 禁止 |
| 谁来算 HMAC | **不是 Z**。D 的 `deriveUidIndex()` 内部调你的 `uidIndexKey()` 取密钥后自己算 HMAC；你只提供 32 字节密钥，不实现派生 |

**为什么是整机单钥**：刷卡的用途是**选用户**，发生在任何用户登录之前。密钥若存在每用户 DB 里，未登录时读不到，刷卡选用户就成了鸡生蛋。整机单钥的代价是"任何用户登录后都能算出全机 UID 索引"，但索引本身不可反推 UID，且卡↔用户映射表在各用户库里仍隔离——这个代价可接受。

### 0.7 模块 QML 用独立 QQmlEngine

现在模块页通过 `Loader.setSource()` 加载进**宿主同一个 QQmlEngine**（`Main.qml:667`），根 context 里的 `connectionManager`、`systemTools`、`musicService`、`desktopShell` 全部可见。T-13 要求"QML 只见用户选择结果"在现结构下**不可执行**。

**做法**：模块页用独立的 `QQmlEngine` + 独立 `QQmlContext`，只注入：

| 注入项 | 内容 |
|---|---|
| `sessionModel` | 该模块自己的数据（现状保留） |
| `userContext` | 只读：`activeUserId`、`locked`、`isAdmin` |
| 主题/i18n | 显式传入必要值，不依赖宿主 context |

**禁止注入**：`connectionManager`、`systemTools`、`musicService`、任何数据库或密钥访问对象。

代价：多一个引擎实例（内存多一份），主题与 i18n 需显式传递。收益：模块页无法绕过用户归属直接读宿主状态，T-13 的验收条款才成立。

---

### 0.8 上游范围变更（影响排期）

1. **分工边界（2026-10-28 修订）**：**D 和 Z 只做 Pi 终端软件，不改 ESP 固件仓库。L 负责 ESP 固件，等 Pi 软件完成后再开工。** 采购/接线/电气核验不受限。
2. **D 现在被卡在你身上**：`uidIndexKey()` 归 Z 实现，T-08 后半段与 T-12 都等 Z。你的 T-04 是全队关键路径。
3. **`deriveUidIndex` 签名已冻结**（DR-14）：D 实现并调用，**密钥生成/存储/轮换归你**。
4. **B-01 密码协议已由 F 交付**（规范修订 2 + 向量 + mbedtls 2.28/3.6.5 各 7/7），**等 D 签认，未冻结**。

---

### 0.9 卡点速览：谁在等谁

| 组 | 任务 | 能不能现在开工 |
|---|---|---|
| **A. 今天就能做，无外部前置** | D1 `uidIndexKey()`、D4（T-04）、D8（T-10）、D9a（T-09a）、D11（T-11） | **能。** 本机有 gcc/cmake/Qt 6.5.3，SQLCipher 可解包用，§0 已定全部参数 |
| **B. 前置是你自己** | D9b 绑卡/挂失（等 T-07/T-08）、D12 BLE（自己写完再等 L 对 ESP 侧） | 顺序做，不必问人 |
| **C. 等 D** | D8b 的 UID 匹配回调（等你交密钥后 D 切真实现）、D12 的 TLS 对端联调 | 只等接口，不等设计 |
| **D. 等实机** | 全部 ❓ 项：真 UID、systemd 生效、BlueZ 配对、Wayland 触摸、Pi 3B 性能 | 写代码+单测先行，实机补证 |
| **E. 等 L** | ESP 侧 BLE 配对与 TLS、`hello_ok.uptime_ms` | 与你无关，别替固件做假设 |

**一句话**：**A 组五件事现在全部可以做完**，做完后你就能解除对 D 的全部阻塞（`uidIndexKey` + T-04），P0 关键路径就在你手上。

---

## Day 闸门对照

项目级时间点，供对照，不约束你的执行顺序。**§0 的裁决已把 Day 1/2-3 的阻塞解除**——依赖拆分后你可以直接从 §2 D1 开工。

| Day | 项目要求 Z 交付 | 你的状态 | 现在能不能做 |
|---|---|---|---|
| 1 | SQLCipher 可行性与 worker UID/sandbox 预检（B-02、B-03、B-06） | 未交（见 §1） | **能**：SQLCipher 本机可跑（§0.1），KDF 已定（§0.2），worker unit 可写（§0.4） |
| 2-3 | **PIN/session 骨架、SQLCipher schema 骨架** | 待开工（见 §2 D1、§3 D4） | **能**：T-04 已解除对 T-03 的依赖（§0.3） |
| 4-6 | 每用户 DB、worker 最小隔离 | 提前可做（见 §3 D8、§3 D11） | 提前可做 |
| 7 | 中期演示：SQLCipher 跨用户拒绝、worker 拒网 | — | DB 部分本机可测；拒网需 Pi |
| 8-10 | 登录 UI、TLS/BLE 业务 lane | 提前可做（见 §3 D9、§4） | T-09a 已解除对 T-08 的依赖（§0.3） |
| 13 | PR 自测与 F 审核 | — | — |
| 14 | T-16 完整用户旅程与 P0 签字 | — | — |

> Day 3 闸门原文：「第3天关键预检或第7天关键闭环失败时，F保留全部硬门槛并重新排期、记录负责人和新截止日」。预检未交不等于 P0 自动失败，但 F 需要记录 blocker。

---


## 1. 第 1 天预检回执

> [F 审核稿第 8 节](04-dev-tasks-f-day1.md#8-第一天预检回执与阻塞台账)。回执格式固定：
> `B编号 / owner / commit / 目标硬件与版本 / 命令与脱敏结果 / PASS或FAIL或NOT_RUN / 风险 / 修复动作 / 截止日 / PR链接`
> 没有执行日期和实际结果的模板不算证据。

### B-02 Z 部分：SQLCipher 接入与 Qt 构建环境

- [x] ✅ **「Qt 6.5.3 能否加载 SQLCipher driver」已有确定答案：不需要也不可行**（§0.1）
  - 事实：SDK 的 `plugins/sqldrivers/` 只有 `libqsqlite.so`；无 Qt 源码、无 `qsql_sqlite_p.h`；本机无 autoconf/automake/libtool。重编 `QSQLITE` 插件这条路不通。
  - **裁决：改用 SQLCipher C API**，不再依赖 Qt SQL 插件。
  - 因此「driver 加载失败即 T-10 阻塞」这条作废，T-10 不再被它阻塞。
- [x] ✅ **本机已实测（2026-10-28）**：`PRAGMA cipher_version` = `3.4.1`；错误 key 返回 `SQLITE_NOTADB`(26)；DB 文件头非 `SQLite format`
  - 你只需在 **Pi 上复测一次**并记入回执，不必重新摸索方法（§0.1 有完整命令）
- [ ] ❌ **Pi 上的打开耗时与 DB 大小**（本机 x86_64 数据不代入 Pi 3B）
- [ ] ❌ **记录 Pi OS / Qt / plugin path**
  - 目标 **6.5.3**，历史实机 **6.4.2**，CMake 最低 **6.4**；三者须在回执里分清
- [ ] 📋 **验证 Pi 侧 SDK 来源/哈希清单可构建**（D 交清单，Z 验证）
  - ESP 侧条目归 L 提交，D 只消费路径与哈希；你只验证 Pi 侧那一半

### B-03：worker 隔离

- [ ] ❌ **记录 worker 与宿主 PID/UID**（当前无 worker，需先有最小实现才能测）
- [ ] ❌ **测 worker 访问 DB / `$HOME` / 管理端口 / WAN 全部被拒**
  - 仅 `QProcess` 或配置文本**不算通过**，须 OS 层实测
- [ ] ❌ **确认热点不转发 WAN，宿主自身网络仍可用**

### B-06：PIN / KEK 参数

- [x] ✅ **KDF 已由 F 裁定**：PBKDF2-HMAC-SHA256，迭代 ≥600,000，salt 16B，输出 32B（§0.2）
  - 用 `QPasswordDigestor::deriveKeyPbkdf2`，零新依赖；不选 Argon2id 的理由也已在 §0.2
- [ ] 📋 **测 Pi 上 KDF 延迟与内存**（600k 迭代在 Pi 3B 上预计 0.5～1.5 秒，须实测）
  - 若实测超过 2 秒，提 PR 附数据；**不得自行降低迭代数**
- [ ] 📋 **确认锁定状态持久化方案**（重启不清除有效锁定）
- [ ] 📋 **交首次管理员初始化方案**（本地受控，无默认 PIN/万能账号）

---

## 2. D1 — 解除 D 的阻塞（最高优先）

> 这是**别人在等你的唯一硬阻塞**。D 已用固定测试密钥 stub 自测，你交付后他切真实现。
> 签名冻结见 `F-REVIEW-2.md` §5.3 / DR-14。不属于 T-04/T-10/T-11 任一张任务卡。

- [ ] ❌ **新建 `pi_terminal/src/security/uid_index_key.h`**
  ```cpp
  bool uidIndexKey(QByteArray *out32);   // 成功 true；密钥不可用时 false
  ```
- [x] ✅ **密钥存放位置已由 F 裁决**：整机单钥，`AppConfigLocation/uid_index.key`，0600（§0.6；**不是** `AppLocalDataLocation`，那是模块可读目录）
  - 并行要求：D 的 `deriveUidIndex()` 在密钥缺失时返回空 `QByteArray` → `INVALID_MESSAGE`
- [ ] ❌ **实现密钥生成**，32 字节 `QRandomGenerator::system()`
- [ ] ❌ **实现密钥存储**，0600 文件；D 不接触、不缓存
- [ ] ❌ **实现密钥轮换路径**：轮换后所有旧 UID 索引失效，须重新绑卡；**不得静默迁移**
- [ ] ❌ **密钥缺失时返回 false**，RFID 路径失败关闭，不退回明文 UID 比较
- [ ] ❌ **写测试**：密钥存在/缺失/轮换后旧索引不再匹配

---

## 3. Z 主责任务（按依赖排序）

### D4 — T-04 用户、PIN 和会话服务（🔴 audit）

> 依赖 **T-01**（已解除 T-03，见 §0.3）· 追溯 UT-002、IT-004 · 测试先行
> **Day 2-3 闸门要求「PIN/session 骨架」**，后续 T-10、T-12、T-15 全部依赖它
> **你现在就能开工**：PIN 认证与桥板身份在代码上无交集，不必等 D 的 T-03。

- [ ] ❌ **新建 `pi_terminal/src/security/`**
  - `UserService.{h,cpp}`、`AuthService.{h,cpp}`、`PinPolicy.{h,cpp}`、`SessionManager.{h,cpp}`
- [ ] ❌ **接入 `pi_terminal/CMakeLists.txt` 与 `src/app/TerminalApplication.{h,cpp}`**
- [ ] ❌ **先交骨架**（闸门 Day 2-3）：接口定义 + 空实现 + UT-002 测试骨架可编译
- [ ] ❌ **用户创建/停用，PIN 设置/验证/修改**，至少 6 位 ASCII 数字
- [ ] ❌ **KDF 按 §0.2 实现**：PBKDF2-HMAC-SHA256，迭代 ≥600,000 硬编码，salt 16B 随机，输出 32B
  - 用 `QPasswordDigestor::deriveKeyPbkdf2`（`Qt6::Network`），不加新依赖
  - **不得**由配置/环境变量降低迭代数；单测断言生产常量 ≥600000
- [ ] ❌ **错误限速与临时锁定**：每账号连错 5 次锁 60 秒；终端级 1 次/秒
  - 未知账号同样计入；错误提示不区分「账号不存在」与「PIN 错误」
  - 重启不可清除有效锁定；正确认证才清除该账号失败计数
- [ ] ❌ **单一 `activeUserId`**，退出/切换立即销毁旧会话能力
- [ ] ❌ **前台会话与任务能力分离**
  - 锁屏撤销前台历史读取，任务仍可向原用户受限追加
  - 测量完成/显式取消/明确停止后才能切用户
  - 迟到事件不得写入新用户
- [ ] ❌ **管理员与普通用户区分**，管理员不得读他人健康记录
- [ ] ❌ **PIN 服务在可信 C++ 层**，QML 不持有密钥
- [ ] ❌ **补全 UT-002**：正确/错误 PIN、限速阈值、锁定超时、并发登录、切换、退出
- [ ] ❓ **服务端执行会话撤销**，不能只隐藏 UI 控件

**验收**：正确 PIN 建唯一会话；错误不泄漏账号存在性；达阈值锁定；切换后旧 token 全拒；管理员请求成员健康数据被拒并留审计记录。

### D8 — T-10 每用户 SQLCipher 最小存储（🔴 audit）

> 依赖 T-04（D4）· 追溯 UT-005、IT-012 · 测试先行
> **Day 2-3 闸门要求「SQLCipher schema 骨架」**，Day 4-6 要求每用户 DB 完成

- [x] ✅ **B-02 结论已定**：**不走 Qt SQL 插件**，直接调 SQLCipher C API（§0.1）
  - Qt SDK 无 SQLCipher 驱动、无 `qsql_sqlite_p.h`、本机无 autoconf，重编 QSQLITE 不可行
  - 本机已实测 `libsqlcipher-dev` 可免 sudo 解包使用（§0.1），**你不必等 Pi**
- [ ] ❌ **新建 `SqlCipherStore.{h,cpp}`、`UserDatabase.{h,cpp}`**（用 `sqlite3_*`，不是 `QSqlDatabase`）
- [ ] ❌ **CMake 加 `find_library(SQLCIPHER_LIB sqlcipher)` + 头文件路径**，找不到 `FATAL_ERROR`
  - 加 `-DSQLITE_HAS_CODEC`；**不要**加 `Qt6::Sql`
- [ ] ❌ **错误 key 必须返回 `SQLITE_NOTADB`(26) 并映射 `STORAGE_UNAVAILABLE`**，禁止 fallback 明文
- [ ] ❌ **先交 schema 骨架**（闸门 Day 2-3）：表结构 + 迁移框架 + 空实现
- [ ] ❌ **每用户独立 SQLCipher DB**，密钥只由可信 service 管理
- [ ] ❌ **每用户随机 DB key；PIN 经 KDF 派生 KEK 只用于封装/解封 DB key**
  - PIN 本身不作 SQLCipher key
- [ ] ❌ **登录后 key 只在 trusted service 短期内存态**
- [ ] ❌ **改 PIN 时验证旧 PIN 后重新封装 DB key，不重加密整库**
- [ ] ❌ **最小用户数据 API + 跨用户拒绝**
- [ ] ❌ **模块 worker / QML 拿不到 DB 文件、密钥或任意 SQL**
- [ ] ⚠️ **同步改造盆栽明文 JSON**（T-11/T-13 联动）
  - `module-devkit/examples/smart-planter/History.h` 当前是明文 JSON（`QJsonDocument`），须改为可信 SQLCipher 数据 API 并禁用 P0 旧写入
  - 旧记录无用户归属：**不自动导入首个用户、不自动删除**，另留迁移记录
- [ ] ❌ **补全 UT-005**：A/B 各写标记，A 读 B、错误 key 打开、退出后重用旧 session
- [ ] ❓ **测 DB / WAL / log / temp 无 PIN、raw UID、明文健康值**
- [ ] 📋 **记录 Pi 3B 上驱动加载、DB 打开、关键 PIN/查询延迟**

**验收**：每用户 DB 加密；DB key 随机且异于 PIN；A 无法查改 B；锁屏后旧前台 API 拒访；日志无敏感值。

### D11 — T-11 基础 worker 进程隔离与默认拒网（🔴 audit）

> 依赖 T-03、T-04（D4）、T-10（D8）· 追溯 IT-013 · 拒绝性集成测试先行
> **Day 4-6 闸门要求「worker 最小隔离」**

- [ ] ❌ **模块代码移出宿主进程**
  - `ModuleManager.cpp:31` 当前 `QLibrary::setLoadHints`，同进程加载；须改为独立低权限 worker + 有界版本 IPC
- [ ] ❌ **新建 `src/core/ModuleWorkerLauncher.{h,cpp}` + IPC 协议 + 最小 worker 入口**
- [ ] ❌ **worker 使用非特权 UID**（按 §0.4：`DynamicUser=yes`，不建持久账号）
- [ ] ❌ **新增 systemd unit**（仓库当前**无任何** `.service` 文件）
  - `pi_terminal/packaging/orange-pi-worker@.service`，含 `DynamicUser=yes`/`PrivateNetwork=yes`/`PrivateTmp=yes`/`ProtectHome=yes`/`ProtectSystem=strict`/`NoNewPrivileges=yes`
  - 安装期一次 sudo 装进 `/etc/systemd/system/`，运行期不提权
- [ ] ❌ **Pi 热点/WAN forwarding 默认拒绝**，worker 默认无公网 egress
  - `configure-hotspot.sh` 现在用的 `ipv4.method shared` 会开 NAT/转发，须显式关掉，或改用无转发的 AP 配置
- [ ] ❌ **ESP 网络配置归 L**（不改动，仅需知其约束）
- [ ] ❌ **IPC 限制消息长度、schema、命令能力、请求超时**
- [ ] ❌ **盆栽 C ABI 业务迁至 worker/可信服务**，宿主只跑受控 UI
- [ ] ❓ **worker 崩溃不带崩 Qt 主程序**，session/资源可回收
- [ ] 📋 **补锁屏任务能力测试**（与 T-13 联动）

**验收**（按 §0.4 判定，不要求固定 UID）：worker PID 的 UID ≠ 宿主 UID；`systemd-analyze verify` 通过且 `systemctl show` 里 `DynamicUser=yes`；读不到 DB/密钥/`$HOME`；worker 内 `curl` 外网失败而宿主网络正常；kill worker 后宿主存活。
**注意**：`QProcess` 本身不是沙箱，必须证明 unit 实际生效，不能只交配置文件。

### D9 — T-09 Qt 登录、绑卡、锁屏、挂失（🟢 guarded）

> 原依赖 T-04、T-08、T-10 已拆为两段（§0.3）。仍是**一张任务卡**，不新增编号。
> 追溯 E2E-002 · Pi/Wayland/Qt 6.5.3 实机

#### D9a — PIN / 锁屏 / 切用户（**现在就能做**）

> 依赖 **T-04（D4）、T-10（D8）**。不需要 RFID。

- [ ] ❌ **新增登录/锁屏/用户选择 QML 组件**（`qml/` 下 login/lock/user/pin 组件数 = **0**）
- [ ] ❌ **在 CMake 资源表登记**（`UI_QML_FILES`，`CMakeLists.txt:168` 起）
- [ ] ❌ **PIN 页面**、低敏用户选择、错误/离线状态
- [ ] ❌ **锁屏**：无交互 5 分钟自动锁定；隐藏敏感内容；提示/语音不含姓名、读数、精确位置
  - 锁屏后必须重输 PIN，**不得刷卡解锁**
- [ ] ❌ **切用户前显示任务完成/取消/明确停止流程**
- [ ] ❌ **模块页改用独立 `QQmlEngine`**（§0.7；不做这步 T-13 的隔离验收无法成立）
  - 现为 `Main.qml:667` 的 `Loader.setSource()`，与宿主同引擎
  - 只注入 `sessionModel` / 只读 `userContext` / 主题；**禁止**注入 `connectionManager` 等宿主对象
- [ ] ❌ **管理员配对/撤销页面**（F 裁决：页面归你，后端归 D 的 TrustStore）
  - 需要重验管理员 PIN；列出已配对桥板与模块；撤销后立即断连的视觉反馈
  - 只调接口，**不自己实现 TrustStore**
- [ ] ❓ **QML 只调可信服务**，所有操作由服务端再次校验会话/权限
- [ ] ❓ **Wayland 实机触摸 + 键盘完成闭环**

#### D9b — 绑卡 / 挂失（等 RFID 链路）

> 依赖 **T-07、T-08（D 的）、D9a**。只有这一段真需要卡。

- [ ] ❌ **显式绑卡**：已登录 + 重验 PIN + 30 秒单次窗口；已绑他人的卡拒绝
- [ ] ❌ **管理员挂失**：重验管理员 PIN 后可撤销成员卡映射，但不能借此解锁 DB
- [ ] ❓ **刷真卡完成绑定/挂失闭环**（需真 RC522 + 真桥板）

**验收**：D9a —— PIN 正确才登录、锁屏需重输、管理员能配对/撤销且不能读他人数据；D9b —— 刷卡不直接登录、绑卡须短时窗口、挂失不解锁 DB。

### D2 — T-02 Z 部分：Qt 构建环境（⚪ audit，与 D 共担）

> 依赖 T-01 · 追溯 IT-002 · **别人所有 Pi 侧工作都依赖这个能构建**

- [ ] ❓ **在 Pi 3B / 64-bit Bookworm / Qt 6.5.3 / Wayland 上构建主项目**
- [ ] ❓ **记录内核、编译器、Qt plugin path、串口权限组**
- [ ] ⚠️ **Pi 服务不得以 root 运行** — 与 D11 非特权 UID 一并核对

---

## 4. Z 协作项

> 主责在他人，你负责其中用户/会话/页面/授权部分。
> 修订后 D 只做 Pi 侧，L 等软件完成后才动固件——你的协作项基本都在 Pi 侧。

### D8b — T-08 RC522 事件闭环（Z 部分：密钥与匹配）

> 主责 D（PBR/事件链路）· 依赖 T-04（D4）、T-07（D 的）· **D 在等你的 `uidIndexKey`**

- [ ] ❌ **`uidIndexKey()` 已在 §2 D1 列出，优先交付**
- [ ] ❌ **UID 匹配回调**：用 `deriveUidIndex()` 结果选用户（D 实现并调用，你提供密钥）
- [ ] ❌ **选中用户后只显示 PIN 页**，不直接登录
- [ ] ❌ **raw UID 只短暂驻留内存**，不进 DB / log / QSettings；QML / worker 只见选择结果
- [ ] ❓ **验证未配对/篡改/重复/过期事件全部拒绝**
- [ ] ❓ **确认 UID hex 大小写都接受**（校验时折叠，`deriveUidIndex` 收原始字节）

### D12 — T-12 业务数据 TLS/BLE（Z 部分：**只是会话与授权**）

> **主责是 D，不是你。** 任务卡把 `HotspotLanTransport.{h,cpp}`、`BleTransport.{h,cpp}` 都划在 D 的文件范围内——传输层代码你不改。
> 你在 T-12 里只负责**传输层之上的会话与用户授权**，以及给 D 提供 profile 判定结果。
> **D 在等你的 T-04（D4）**；ESP 侧 TLS 归 L。

- [ ] ❌ **会话绑定用户与能力**：TLS/BLE 连接建立后，业务数据必须挂到当前会话的用户与已授权能力上
  - 没有会话就不接受业务数据；`activeUserId` 变更时旧连接降级为不可用
- [ ] ❌ **撤销立即断连**：撤权/停用用户/切用户时，由你发出「该会话已失效」信号
  - 断连动作与密钥销毁的传输层实现归 **D**，你只提供信号和状态
- [ ] ❌ **向 D 提供 profile 判定**：哪个模块走 `fixed_ble`、哪个走 `fixed_hotspot_lan`
  - 复用现有 `TransportPolicy`（`src/core/TransportPolicy.h`），不要另写一套
- [ ] ❌ **BLE 的会话侧前置**：未登录/锁屏时，即使 GATT 已配对也不得把业务数据交给模块
  - 配对门控（`pairingStatus == Paired`）由 **D** 在 `BleTransport` 实现（§0.5）
- [ ] ❓ **断线 / 切用户 / 撤权后旧 session 不可继续发数据**（与 D 联调验证）
- [ ] 📋 **`TLS 会话 ≠ 设备已认证` 写进你负责的会话代码注释**

### D13-D16 — 页面与会话权限（Z 部分）

> 修订后这几项主责明确拆为：L 固件、D Pi 侧接口/字段/路由、Z 页面/UI。**你不碰固件。**

- [ ] ❌ **T-13 用户归属/UI**：盆栽全部传感器数据按用户归属显示；退出后旧用户上下文不能读取
  - **前置**：D9a 里的模块页独立引擎（§0.7）。那一步不做，这条无法验收——不要在这条里再做一遍引擎改造
- [ ] ❌ **T-14 页面/会话权限**：啸天受控状态 UI 复用安全业务 lane，不当作已有插件
  - 啸天 `firmware/` 尚无实现（L 负责），你只做页面
- [ ] ❌ **T-15 两模块分别运行**：用户/device_id/slot_id 不丢失
- [ ] ❌ **T-16 双槽同时运行**：两个 slot/device/event/user context 不串；单槽拔出不影响另一槽
- [ ] ❓ **补测离槽继续、旧槽换设备不继承授权、桥失联时本机 PIN 解锁查看本人任务**

---

## 5. T-01 签认

> [IT-001](04-dev-tasks-f-day1.md#9-it-001-审查签认与验收顺序) 中 Z 行仍为「待本人确认」。
> F 文档审定 + 密码协议交付**不**替代你的技术签认。Day 1 闸门要求收集技术签认。

- [ ] ❌ **签认确认范围**：PIN/session、SQLCipher、worker IPC/OS 隔离、UI 边界
  - 记录格式：日期 / commit / PR 或 Issue
- [ ] 📋 **确认权限矩阵**（F 审核稿第 2 节）与实现一致
- [ ] 📋 **确认设备生命周期语义**（锁屏不停任务、切前收尾、迟到事件不串档）
- [ ] 📋 **确认 `uidIndexKey` 密钥归属切分**（DR-14）你接受
- [ ] 📋 **回执内容确认后再签认**，不预签不存在的能力

---

## 6. 阻塞项速查

**§0 已解除的阻塞**（下面这张表里不再出现，别再等）：本机工具链、SQLCipher 驱动、`uidIndexKey` 位置、KDF 参数、BLE 加密方案、T-04/T-09a 对 D 的依赖。

还剩这些：

| 阻塞 | 影响 | 处置 |
|---|---|---|
| 真 UID / systemd / BlueZ / Wayland | 全部 ❓ 项 | 本机可写可编译，实机补证；不因此改设计 |
| `DynamicUser` 需在 Pi 上验 unit 生效 | D11 验收 | 本机写 unit，`systemd-analyze verify` 能跑；UID/拒网要 Pi |
| 热点 `ipv4.method shared` 会开转发 | D11 拒网 | 改无转发配置；本机无 nmcli，无法预验 |
| 无 `fixed_ble` 真实模块 | D12 BLE 实机 | 代码 + 单测现在可写，实机等有 BLE 模块 |
| 本机无 `nmcli` / 无 argon2 头 | 热点脚本 / 将来升级 | 不影响 P0 主线（§0.2 已选 PBKDF2） |
| `QBluetoothLocalDevice::requestPairing()` 需 PolicyKit | D12 BLE 配对 | Pi 上实测；失败提 PR 附 `journalctl`，**不得**改明文放行 |
| 无 ARM64 交叉编译环境 | 所有 Pi 二进制 | 本机只验 x86_64；Pi 上重编 |

**明确不在 P0**：生产 CA、完整备份恢复、完整 systemd/seccomp 加固、啸天测速与户外、正式 NFC 卡、血压计、OTA。
**明确不在 Z 范围**：ESP 固件（归 L，等 Pi 软件完成后开工）。

---

## 附：上游变更摘要（`f9f109a` → `9a3d221`）

| 类别 | 内容 |
|---|---|
| 代码 | 新增 `pi_terminal/src/protocol/control_event.{h,cpp}`（183+22 行）；`protocol_test.cpp` +84 行；`HostEventDetector.h` +6；`PogoControlTransport.cpp` +7；`CMakeLists.txt` +2 |
| 规范 | 新增 `docs/pbr-secure-v1/`：`pbr-secure-v1.md`（513 行）、`F-REVIEW-2.md`、`PR-NOTES.md`、`vectors.json`、`gen_vectors.py`、`mbedtls_check.c`、`00-decisions-for-f.md` +263 |
| 分工 | D/Z 只做 Pi 软件；L 负责 ESP 固件且等软件完成；T-12～T-16 主责按 Pi 侧/固件侧拆分 |
| 预检 | B-01 部分回执（F 交付规范，等 D 签认）；B-07 记为遗留不阻塞 |
| 路径 | `mock_bridge.py` 从 `tools/` 修正为 `pi_terminal/tools/` |
| 上游新增 | `9a3d221` PR #32 时间工具（`feat/time-tools`）：校时、12/24 小时、表盘主题；**与 Z 的 P0 范围无交集**，但基线已前移 |
