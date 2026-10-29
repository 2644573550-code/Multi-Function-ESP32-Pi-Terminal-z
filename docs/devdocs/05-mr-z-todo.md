# Mr.Z 工作清单（P0）

> **手工维护**：本文件不是生成物。首版由 dev-tasks 产出（2026-10-28），此后按人工修订维护，重跑任何生成器都会覆盖本条。
>
> 个人执行清单，不替代 [总计划](04-dev-tasks.md) 与各任务卡。冲突时以任务卡和 [F 审核稿](04-dev-tasks-f-day1.md) 为准；**接口与参数以本文 §0 为准**（§0 是 F 2026-10-28 的裁决，任务卡尚未逐条回填）。

**基线**：Pi `54c9bcd`（2026-10-29，main，含 PR #38 Z 的 P0 安全层、PR #40 flite 语音库、PR #41 软件侧补全、PR #42 技术说明同步到 §14）。

**进度**：今天是 **Day 4**。P0 起算日为 2026-10-26，Day 14 = **2026-11-08**。

> **2026-10-29 同步说明**：本文件此前停在 PR #38 合并前的状态，勾选标记与实际代码严重脱节
> （标注"不存在"的登录 QML、systemd unit、worker 隔离等其实早已合入 main）。
> 本次按 commit `54c9bcd` 的实际代码逐项核对回填，**只改勾选与状态标注，不改 §0 的任何裁决参数**。
> 核对方式见文末「附录：2026-10-29 核对方法」。仍为 `[ ]` 的项都在文末「尚未完成清单」里给出原因。

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

**两套编号别混**：
- 本文的 **§0.1～§0.10** 是**裁决条款**（参数/规格）。
- 本文的 **D4/D8/D9/D11/D12** 是**你的待办任务号**（对应 T-04/T-10/T-09/T-11/T-12）。
- `F-REVIEW-2.md` 里的 **Z-1～Z-7** 是**参数编号**，§5.3.2 那张表；与你的任务号是两套，不要互相对照。

**与审查文档的关系**：本文的七项冻结参数与 BLE/LAN 规格在 `docs/pbr-secure-v1/F-REVIEW-2.md` **§5.3.2**（含 D2 节）有同一份表述（与 D 的 §5.3.1 对称）。两份若不一致，以 F-REVIEW-2 为准并提 PR 修本文。

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

**依赖获取**：Pi 上 `sudo apt install libsqlcipher-dev`。CMake 用 `find_library(SQLCIPHER_LIB sqlcipher)` + 回退到 `SQLCIPHER_ROOT`，**不硬编码路径**；找不到时 `message(FATAL_ERROR)`，不静默降级成 SQLite。

**版本差异（2026-10-28 核实 Debian 包关系）**：

| 发行版 | 版本 | 说明 |
|---|---|---|
| bookworm 主源 | **3.4.1** | 本机实测所用版本 |
| bookworm-backports | 4.6.1 | 若启用 backports 会拿到 4.x |
| trixie | 4.6.1 | — |

**3.x 与 4.x 的默认 KDF 参数不同**（4.x 默认迭代更高、KDF 算法不同）。因此：
- **DB 文件不跨大版本互换**。P0 在 Pi 上构建时锁定一个版本，DB 带上版本标记。
- 不要依赖 PRAGMA 默认值；用 `sqlite3_key` 后显式设 `PRAGMA cipher_page_size` 等参数前先确认版本。
- 若 Pi 上是 4.x 而本机验证是 3.4.1，**必须在 Pi 上重跑 §0.1 那组验证**（错误 key 返回 26、`cipher_version`），不得直接套用本机结果。

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

安装期需要**一次** sudo 把 unit 装进 `/etc/systemd/system/`；运行期不再提权。这与 `setup-desktop.py` 的「绝不 sudo」原则不冲突——那个脚本装的是用户态 SDK，不是 system service。

**版本前提已核实（2026-10-28）**：Debian bookworm 的 systemd 是 **252**；`DynamicUser=` 与 `PrivateNetwork=` 均自 **232** 起可用，前提满足，无需 backports。

**一个注意点**：`DynamicUser=yes` 下进程 UID 每次启动都变，所以**不要**用固定 UID 写文件属主或用 `uid=` 判定。worker 的专用运行目录用 `StateDirectory=`/`RuntimeDirectory=` 让 systemd 自动分配属主，不要手工 `chown`。

`QProcess` 本身**不是**沙箱，只起进程不加这些声明一律判 FAIL。

### 0.5 BLE：保留（规格见 §0.8.2）

Qt 6.5.3 的 `QLowEnergyController` **没有任何 pair/encrypt 接口**（逐行查过头文件）。Qt 唯一能触到加密的地方是 `QBluetoothLocalDevice::requestPairing()`，它把配对交给 BlueZ。

**但这不够**：Just Works 配对的链路密钥可能被中间人获知。所以 P0 要求**链路层配对 + 应用层挑战-响应**两层。完整参数、密钥来源、失败处理全部在 **§0.8.2**，本节不重复。

**归属**：`BleTransport` 在 T-12 的 **D 文件范围**内，实现归 D；Z 只做传输层之上的会话门控。

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

> **2026-10-29 状态注（不改本条款）**：本条**已实现**。`src/core/ModulePageHost.{h,cpp}` 用独立
> `QQmlEngine`，只注入 `sessionModel` / `userContext` / `moduleData` / 主题；`Main.qml` 已无
> `setSource()`。上面"现在模块页通过 `Loader.setSource()`"描述的是裁决当时的状况，保留原文备查。

---

### 0.8 BLE 与 LAN 通信（2026-10-28 定稿，P0 必须实现）

你在 P0 要同时实现 **BLE 和 LAN** 两条业务 lane。下面七条是已定参数，不要再自行取舍。

#### 0.8.1 LAN：模块当 TLS server，Pi 当 client，指纹固定

实测确认（2026-10-28，本机 x86_64 / Qt 6.5.3 / OpenSSL 3.0.2）：`QSslServer` + 自签证书 + 客户端 `VerifyNone` + 握手后比对 SHA-256 指纹，**可以跑通**（`handshake OK, pinned_match=1`，`isSelfSigned=1`）。

| 项 | 定值 |
|---|---|
| 方向 | **模块监听、Pi 主动连**（现状：`module_data_service.c:279-292` 是 `listen`，Pi `ConnectionManager.cpp:238` 调 `connectToModule`）。**不改方向** |
| 模块证书 | **自签**，模块侧生成，含 `CN=<module_id>` 与设备序列 |
| Pi 校验 | 首次连接用 `VerifyNone` 完成握手，**随后立刻**比对 `peerCertificate().digest(Sha256)` 与 TrustStore 里登记值；不一致立即断开，返回 `IDENTITY_REJECTED` |
| 登记 | 首次配对时由管理员确认后写入 TrustStore（D 的 T-03）；**换模块必须重新登记** |
| 协议 | `QSsl::TlsV1_2OrLater`；实测支持 TLS 1.2 与 1.3 |
| 失败 | 无 fallback。握手失败 = 连接失败，**不得**退回明文 TCP |
| 抓包 | 不得见到传感器明文或 session credential |

**为什么不用 CA**：P0 是逐设备信任，`f-day1:136` 已允许「明确登记的逐设备信任」。项目 CA 属 P1。

**为什么先 `VerifyNone` 再比指纹**：自签证书不在任何 CA 链上，`VerifyPeer` 必然失败。指纹比较必须在握手后单独做，**不能省略**——省略就等于不校验。

#### 0.8.2 BLE：BlueZ 配对（Just Works）+ **应用层挑战-响应**

两层都要，缺一不可。链路层用 BlueZ 配对（Just Works 可接受），应用层再用对称密钥做一次挑战-响应。

| 层 | 要求 |
|---|---|
| 链路层 | `pairingStatus(addr) == Paired` 才能 `writeCharacteristic`；不要求 MITM/数字比较（应用层会兜底） |
| 应用层 | 连接后先做挑战-响应，**通过前不发送任何业务数据** |
| 算法 | **HMAC-SHA256，对称密钥 32 字节**，与 `pbr-secure-v1` 已有 HMAC 结论一致，不引入新算法 |
| 消息 | `challenge`(32B 随机) → `response = HMAC-SHA256(k, challenge ‖ module_id ‖ boot_id)` → Pi 比对 |
| 密钥来源 | **T-03 配对时经 LAN 下发，模块写 NVS**（见 0.8.3） |
| 失败 | 3 次失败断开，返回 `TRANSPORT_SECURITY_REQUIRED`；不降级明文 |
| 重放 | challenge 每次新生成，不接受重复；会话内不复用 |

**必须做应用层认证的理由**：Just Works 配对的链路密钥可能被中间人获知；只有应用层密钥能证明「对面是这台特定模块」。

#### 0.8.3 模块密钥下发（新增步骤，归 T-03）

原 T-03 只有「登记指纹」，没有下发对称密钥。新增：

| 项 | 定值 |
|---|---|
| 时机 | 首次配对，管理员在 Pi 上确认后 |
| 通道 | **一次性的 LAN 连接**（此连接仍需先完成 0.8.1 的握手） |
| 内容 | 32 字节随机密钥，`QRandomGenerator::system()` 生成 |
| 模块侧 | 写入 NVS，**不得**写入固件镜像或 `sdkconfig` |
| 幂等 | 已下发过则拒绝重复下发，须先显式撤销 |
| 日志 | 任何一侧都不得打印密钥 |

**这条需要 L 配合**：ESP 固件现在**完全没有 NVS 密钥存储**（`module_data_service/` 零处）。L 实现前，BLE 应用层认证无法实机验证。

#### 0.8.4 单条业务消息上限：512 字节

| 项 | 定值 |
|---|---|
| BLE 单条业务 payload | **≤512 字节**，超出返回 `INVALID_MESSAGE` |
| 分片 | 由 `BleTransport` 内部按 20 字节切，**对上层透明** |
| 原因 | `BleTransport.h` 现有实现就按 20 字节分片；512 与 PBR payload 上限一致 |

#### 0.8.5 lane 热切换：允许，但必须重新认证

现有代码只有 `initialTransport()` 固定一条 lane，**没有任何切换状态机**，需要你新写。

| 规则 | 内容 |
|---|---|
| 触发 | LAN 断开后允许 BLE 接手；反之亦然 |
| 会话 | **必须重新做一次认证**（0.8.1 的指纹 / 0.8.2 的挑战-响应），**不得搬用旧会话密钥** |
| 未完成分片 | 切换时丢弃，不重传 |
| 次数 | 单次 attach 内最多切换 3 次，超出则 attach 失败 |
| 记录 | 切换事件记入日志（不含密钥） |

#### 0.8.6 `fixed_ble` + `hotspot_lan` 包并存：允许

`TransportPolicy` 现在允许这个组合（`runtime_test.cpp:154` 已断言 `valid()`），**保持现状不改**。

| 组合 | 含义 |
|---|---|
| `fixed_ble` + `data` | 业务数据走 BLE |
| `fixed_ble` + `hotspot_lan` | **业务数据走 BLE，插件包走 LAN 热点**（BLE 带宽不够传 .so） |
| `fixed_hotspot_lan` + `data` | 全部走 LAN |

文档此前写的「两类不混用」**作废**：指的是业务数据 lane，不是包传输。

#### 0.8.7 T-12 验收拆两组证据

| 组 | 谁交 | 内容 |
|---|---|---|
| **Pi 侧三组** | **Z（会话部分）/ D（传输部分）** | ① 未配对 BLE 拒发；② 挑战-响应失败拒发；③ TLS 指纹不匹配拒绝、无明文回退 |
| **ESP 侧三组** | **L** | ① BLE `sm_io_cap` 配置与加密属性；② TLS server 与自签证书；③ NVS 密钥存储 |

**Pi 侧现在就能验**（用本机的假模块 + 自签证书），**不需要等 L**。ESP 侧等 L 实现后补。

### 0.9 卡点速览：谁在等谁

| 组 | 任务 | 能不能现在开工 |
|---|---|---|
| **A. 今天就能做，无外部前置** | D1 `uidIndexKey()`、D4（T-04）、D8（T-10）、D9a（T-09a）、D11（T-11）、**D12 的 Pi 侧会话与授权** | **能。** 本机有 gcc/cmake/Qt 6.5.3 + OpenSSL，SQLCipher 可解包用，§0 已定全部参数 |
| **B. 前置是你自己** | D9b 绑卡/挂失（等 T-07/T-08）；lane 热切换状态机（§0.8.5，自己新写） | 顺序做，不必问人 |
| **C. 等 D** | D8b 的 UID 匹配回调、T-12 的**传输层**（`BleTransport`/`HotspotLanTransport` 归 D）、模块密钥下发（归 T-03/D） | 只等接口，不等设计 |
| **D. 等实机** | 全部 ❓ 项：真 UID、systemd 生效、BlueZ 配对、Wayland 触摸、Pi 3B 性能 | 写代码+单测先行，实机补证 |
| **E. 等 L** | ESP 侧 BLE `sm_io_cap`、TLS server 与自签证书、**NVS 密钥存储**、`hello_ok.uptime_ms` | 与你无关，别替固件做假设 |

**一句话**：**A 组六件事现在全部可以做完**，做完后你就能解除对 D 的全部阻塞（`uidIndexKey` + T-04），P0 关键路径就在你手上。

**BLE/LAN 的边界**（§0.8）：传输层代码归 **D**，你只做传输层之上的**会话门控与授权**；模块密钥下发归 **T-03（D）**；你负责验证「未授权/未认证时业务数据进不来」。

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


### 0.10 上游范围变更（影响排期）

1. **分工边界（2026-10-28 修订）**：**D 和 Z 只做 Pi 终端软件，不改 ESP 固件仓库。L 负责 ESP 固件，等 Pi 软件完成后再开工。** 采购/接线/电气核验不受限。
2. **D 现在被卡在你身上**：`uidIndexKey()` 归 Z 实现，T-08 后半段与 T-12 都等 Z。你的 T-04 是全队关键路径。
3. **`deriveUidIndex` 签名已冻结**（DR-14）：D 实现并调用，**密钥生成/存储/轮换归你**。
4. **B-01 密码协议已由 F 交付**（规范修订 2 + 向量 + mbedtls 2.28/3.6.5 各 7/7），**等 D 签认，未冻结**。

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
  - 本机已测：`cipher_version` = 4.13.0；Pi 预计 3.4.1，**必须重跑** §0.1 那组验证
- [ ] ❌ **记录 Pi OS / Qt / plugin path**
  - 目标 **6.5.3**，历史实机 **6.4.2**，CMake 最低 **6.4**；三者须在回执里分清
- [ ] 📋 **验证 Pi 侧 SDK 来源/哈希清单可构建**（D 交清单，Z 验证）
  - ESP 侧条目归 L 提交，D 只消费路径与哈希；你只验证 Pi 侧那一半

### B-03：worker 隔离

> 代码与 unit 已合入（见 §3 D11）。下列三项是**OS 层实测证据**，本机无 systemd unit、无 Pi，
> 全部未执行。本机 `ModuleWorkerClient::degraded() == true`（直接起二进制），**不构成隔离证据**。

- [ ] ❌ **记录 worker 与宿主 PID/UID**（unit 已存在，需 Pi 上真装一次才能测）
- [ ] ❌ **测 worker 访问 DB / `$HOME` / 管理端口 / WAN 全部被拒**
  - 仅 `QProcess` 或配置文本**不算通过**，须 OS 层实测
- [ ] ❌ **确认热点不转发 WAN，宿主自身网络仍可用**
  - `configure-hotspot.sh:35` 已改为 `ipv4.method manual` + `never-default`，但本机无 `nmcli`，无法预验

### B-06：PIN / KEK 参数

- [x] ✅ **KDF 已由 F 裁定**：PBKDF2-HMAC-SHA256，迭代 ≥600,000，salt 16B，输出 32B（§0.2）
  - 用 `QPasswordDigestor::deriveKeyPbkdf2`，零新依赖；不选 Argon2id 的理由也已在 §0.2
- [x] ✅ **锁定状态持久化已实现**：`AuthService` 落 `auth_locks.json`，重启后按 `locked_until_ms` 继续生效
  - 实测见 `auth_service_test`；Pi 上重启场景仍需实机确认（归入下条）
- [ ] 📋 **测 Pi 上 KDF 延迟与内存**（600k 迭代在 Pi 3B 上预计 0.5～1.5 秒，须实测）
  - 若实测超过 2 秒，提 PR 附数据；**不得自行降低迭代数**
  - 本机实测：Qt `QPasswordDigestor` 760ms vs 裸 OpenSSL 90ms（同进程对照），Pi 3B 推算 4.3～7.1 秒
  - **这条已超出 2 秒预算**，见 `07-mr-z-risk-register.md` R-01（方案 A/B/C 待 F 裁决）
- [x] ✅ **首次管理员初始化方案已实现**：`UserService.cpp:131` 仅在 `actorUserId` 为空时允许首次初始化，无默认 PIN、无万能账号
  - 本地受控路径；Pi 上首次开机的实际流程仍归 T-16 旅程签字

---

## 2. D1 — 解除 D 的阻塞（最高优先）

> 这是**别人在等你的唯一硬阻塞**。D 已用固定测试密钥 stub 自测，你交付后他切真实现。
> 签名冻结见 `F-REVIEW-2.md` §5.3 / DR-14。不属于 T-04/T-10/T-11 任一张任务卡。

- [x] ✅ **新建 `pi_terminal/src/security/uid_index_key.h`**
  ```cpp
  bool uidIndexKey(QByteArray *out32);   // 成功 true；密钥不可用时 false
  ```
- [x] ✅ **密钥存放位置已由 F 裁决**：整机单钥，`AppConfigLocation/uid_index.key`，0600（§0.6；**不是** `AppLocalDataLocation`，那是模块可读目录）
  - 并行要求：D 的 `deriveUidIndex()` 在密钥缺失时返回空 `QByteArray` → `INVALID_MESSAGE`
- [x] ✅ **实现密钥生成**，32 字节 `QRandomGenerator::system()`
- [x] ✅ **实现密钥存储**，0600 文件（`QFile::ReadOwner | QFile::WriteOwner`）；D 不接触、不缓存
- [x] ✅ **实现密钥轮换路径**：`rotateUidIndexKey()`；轮换后所有旧 UID 索引失效，须重新绑卡，**未做静默迁移**
- [x] ✅ **密钥缺失时返回 false**，RFID 路径失败关闭，不退回明文 UID 比较
  - `protocol/uid_index.cpp:15` 的 `deriveUidIndex()` 在密钥不可用时返回空 `QByteArray`，**不是**退回明文比较
- [x] ✅ **写测试**：`uid_index_key_test` / `uid_index_test` / `uid_index_default_key_test` 三个文件
  - 覆盖存在/缺失/轮换后旧索引不再匹配/文件权限无 group+other 位

> D1 已交付，D 不再被 Z 阻塞。调用点 `deriveUidIndex()` 由 D 实现（`src/protocol/uid_index.cpp`），
> 签名与 §0.6 一致。

---

## 3. Z 主责任务（按依赖排序）

### D4 — T-04 用户、PIN 和会话服务（🔴 audit）

> 依赖 **T-01**（已解除 T-03，见 §0.3）· 追溯 UT-002、IT-004 · 测试先行
> **Day 2-3 闸门要求「PIN/session 骨架」**，后续 T-10、T-12、T-15 全部依赖它
> **你现在就能开工**：PIN 认证与桥板身份在代码上无交集，不必等 D 的 T-03。

- [x] ✅ **新建 `pi_terminal/src/security/`**
  - `UserService.{h,cpp}`、`AuthService.{h,cpp}`、`PinPolicy.{h,cpp}`、`SessionManager.{h,cpp}`、`TaskCapability.{h,cpp}`
- [x] ✅ **接入 `pi_terminal/CMakeLists.txt` 与 `src/app/TerminalApplication.{h,cpp}`**
- [x] ✅ **先交骨架**（闸门 Day 2-3）：接口定义 + 实现 + UT-002 测试可编译
- [x] ✅ **用户创建/停用，PIN 设置/验证/修改**，至少 6 位 ASCII 数字（`PinPolicy::kMinDigits`）
- [x] ✅ **KDF 按 §0.2 实现**：PBKDF2-HMAC-SHA256，迭代 ≥600,000 硬编码，salt 16B 随机，输出 32B
  - `PinPolicy::kProductionIterations = 600000`；`effectiveIterations()` 只升不降——
    落盘记录里的迭代数若被改小，会被抬回 600000（`users.json` 可被同 OS 用户改写）
  - **不得**由配置/环境变量降低迭代数；`auth_service_test` 断言生产常量 ≥600000
- [x] ✅ **错误限速与临时锁定**：每账号连错 5 次锁 60 秒；终端级 1 次/秒
  - `PinPolicy.h:19-21`：`kMaxAccountFailures=5`、`kAccountLockMs=60*1000`、`kTerminalMinIntervalMs=1000`
  - 未知账号同样计入；错误提示不区分"账号不存在"与"PIN 错误"
  - 重启不清除有效锁定（落 `auth_locks.json`）
- [x] ✅ **单一 `activeUserId`**，退出/切换立即销毁旧会话能力（`SessionManager` + `ConnectionManager::setActiveUserId`）
- [x] ✅ **前台会话与任务能力分离**
  - 锁屏撤销前台历史读取，`TaskCapability` 仍可向原用户受限追加（`auth_service_test:316` 有断言）
  - 测量完成/显式取消/明确停止后才能切用户（`login()` 返回 `BUSY`）
  - 迟到事件不得写入新用户（`TaskCapability` 绑定 userId + deviceId）
- [x] ✅ **管理员与普通用户区分**，管理员不得读他人健康记录
  - 结构上：每用户独立 DB，`UserDatabase::openForeign()` 拒绝跨库；`user_database_test:108` 有断言
  - `SecurityService::requireAdmin()` 非管理员一律 `NOT_AUTHORIZED` 并记审计
  - ⚠️ 见下方遗留项：**"管理员请求他人数据被拒"这条审计没有埋点**
- [x] ✅ **PIN 服务在可信 C++ 层**，QML 不持有密钥（QML 只见 `SessionManager` 的 login/lockNow/signOut）
- [x] ✅ **补全 UT-002**：`auth_service_test`（正确/错误 PIN、限速阈值、锁定超时、并发登录、切换、退出）+ `user_database_test`
- [x] ✅ **服务端执行会话撤销**，不是只隐藏 UI 控件
  - `ConnectionManager::setActiveUserId()` 遍历所有 session 调 `LaneGate::invalidateAll()`，
    切用户/退出时业务数据通道立即作废
- [ ] ⚠️ **遗留：管理员读他人健康数据被拒的审计埋点缺失**
  - 现状：拒绝**是**真的（跨库结构 + `openForeign` 拒绝 + `requireAdmin`），但没有一条
    `AuditAction` 用于"管理员尝试读他人数据"。`SecurityService` 里没有任何按 userId 参数
    读历史记录的接口，所以目前**没有可被拒绝的入口**，也就没有可记的审计。
  - 处置：等真有"管理员代管他人数据"的界面入口时一并埋点；现在硬加属于给不存在的功能写代码。
  - 验收原文「管理员请求成员健康数据被拒并留审计记录」前半句成立、后半句**没有触发路径**。

### D8 — T-10 每用户 SQLCipher 最小存储（🔴 audit）

> 依赖 T-04（D4）· 追溯 UT-005、IT-012 · 测试先行
> **Day 2-3 闸门要求「SQLCipher schema 骨架」**，Day 4-6 要求每用户 DB 完成

- [x] ✅ **B-02 结论已定**：**不走 Qt SQL 插件**，直接调 SQLCipher C API（§0.1）
  - Qt SDK 无 SQLCipher 驱动、无 `qsql_sqlite_p.h`、本机无 autoconf，重编 QSQLITE 不可行
  - 本机已实测 `libsqlcipher-dev` 可免 sudo 解包使用（§0.1），**你不必等 Pi**
- [x] ✅ **新建 `SqlCipherStore.{h,cpp}`、`UserDatabase.{h,cpp}`**（用 `sqlite3_*`，不是 `QSqlDatabase`）
- [x] ✅ **CMake 加 `find_library(SQLCIPHER_LIB sqlcipher)` + 头文件路径**，找不到 `FATAL_ERROR`
  - 加 `-DSQLITE_HAS_CODEC`；**没有**加 `Qt6::Sql`
  - 另加**大版本校验**：3.x 与 4.x 的默认 KDF 参数不同，DB 不跨大版本互换
- [x] ✅ **错误 key 必须返回 `SQLITE_NOTADB`(26) 并映射 `STORAGE_UNAVAILABLE`**，禁止 fallback 明文
  - `SqlCipherStore.cpp:62` 有注释明确；`user_database_test` 覆盖错误 key 打开
- [x] ✅ **先交 schema 骨架**（闸门 Day 2-3）：表结构 + 迁移框架
- [x] ✅ **每用户独立 SQLCipher DB**，密钥只由可信 service 管理
- [x] ✅ **每用户随机 DB key；PIN 经 KDF 派生 KEK 只用于封装/解封 DB key**
  - PIN 本身不作 SQLCipher key；`KeyWrap::open/seal` 封装 32 字节 DB key
- [x] ✅ **登录后 key 只在 trusted service 短期内存态**（`UserDatabase::openForSession()`）
- [x] ✅ **改 PIN 时验证旧 PIN 后重新封装 DB key，不重加密整库**
  - `AuthService::changePin()`：`KeyWrap::open(oldKek, ...)` → 新 salt + 新 KEK → 重封装
- [x] ✅ **最小用户数据 API + 跨用户拒绝**
  - `UserDatabase::openForeign()`：已打开 A 时切 B 直接返回 false；`user_database_test:108` 断言
- [x] ✅ **模块 worker / QML 拿不到 DB 文件、密钥或任意 SQL**
  - DB 在 `AppDataLocation`；worker 的 `ProtectHome=read-only` + `PrivateTmp=yes`
  - QML 只见 `SecurityService` 的 `readings()` / `addManualReading()`，无 SQL 接口
- [x] ✅ **盆栽默认不再写明文 JSON**（T-11/T-13 联动）
  - `History.h:26` 默认关闭落盘，只有 `ORANGE_PI_PLANTER_LEGACY_JSON=1` 时才写（仅供迁移旧数据）
  - 模块只发原始观测（`emit_event("sample", ...)`），不再自己落盘
  - 宿主侧 `ConnectionManager.cpp:88-92` 收 `sample` → `observationSink` →
    `SessionManager::recordObservation()` → 当前用户的 SQLCipher 库
  - 拿不到 DB 就不写，**不伪造归属**
- [ ] ⚠️ **旧明文记录的迁移标记没接线**
  - `UserDatabase::legacyHistoryMigrated()` / `markLegacyHistoryMigrated()` 两个接口已实现，
    **但 `markLegacyHistoryMigrated()` 零调用**——即界面无法区分"还没导入"与"没有历史"
  - 文档要求的是「不自动导入首个用户、不自动删除，另留迁移记录」：前两条已做到，
    第三条（留记录）缺一个写入点
  - 这不依赖硬件，属小项；但**不要**顺手做自动导入——原文明确禁止
- [x] ✅ **补全 UT-005**：`user_database_test`（A/B 各写标记、A 读 B 被拒、错误 key 打开、退出后重用旧 session）
- [ ] ❓ **测 DB / WAL / log / temp 无 PIN、raw UID、明文健康值** — 需 Pi 上真跑并检查文件
- [ ] 📋 **记录 Pi 3B 上 SQLCipher 链接（`cipher_version`）、DB 打开、关键 PIN/查询延迟**
  - **不测 Qt 驱动加载**——路线已定为 C API（§0.1）；若 Pi 上是 4.x，须重跑 §0.1 验证
  - 本机 4.13.0，ARM64 包 4.5.6，**Pi 预计 3.4.1**——三者不跨大版本互换，代码已强制校验

**验收**：每用户 DB 加密 ✅；DB key 随机且异于 PIN ✅；A 无法查改 B ✅；锁屏后旧前台 API 拒访 ✅；日志无敏感值 ❓（需 Pi 实测）。

### D11 — T-11 基础 worker 进程隔离与默认拒网（🔴 audit）

> 依赖 T-03、T-04（D4）、T-10（D8）· 追溯 IT-013 · 拒绝性集成测试先行
> **Day 4-6 闸门要求「worker 最小隔离」**

- [x] ✅ **模块代码移出宿主进程**
  - `ModuleManager` 已改为经 `ModuleWorkerClient` 调用独立 worker 进程，不再 `QLibrary` 同进程加载
  - 保留 `ORANGE_PI_MODULE_INPROCESS=1` 回退开关，**仅供既有测试**（同步回调语义），
    代码注释写明「回退路径不是 T-11 验收证据」
- [x] ✅ **新建 `src/core/ModuleWorkerLauncher.{h,cpp}` + IPC 协议 + 最小 worker 入口**
  - `ModuleWorkerClient.{h,cpp}`（宿主侧）、`module_worker_main.cpp`（worker 入口）、`WorkerIpc.{h,cpp}`（协议）
  - 传输：Unix socket（`RuntimeDirectory` 下的 socket，见 unit `RuntimeDirectory=orange-pi-worker-%i`）
- [x] ✅ **worker 使用非特权 UID**（按 §0.4：`DynamicUser=yes`，不建持久账号）
  - ⚠️ unit 已写，但本机无 systemd，`ModuleWorkerClient::degraded() == true`；
    **Pi 上必须确认 `degraded() == false`**
- [x] ✅ **新增 systemd unit**
  - `pi_terminal/packaging/orange-pi-worker@.service`，含 `DynamicUser=yes` / `PrivateNetwork=yes` /
    `PrivateTmp=yes` / `ProtectHome=read-only` / `ProtectSystem=strict` / `NoNewPrivileges=yes` /
    `RestrictAddressFamilies=AF_UNIX` / `MemoryDenyWriteExecute=yes` 等
  - 另含 `RuntimeDirectory` / `StateDirectory`（按 §0.4 注意点：不手工 chown）
  - 安装期一次 sudo 装进 `/etc/systemd/system/`，运行期不提权
  - ⚠️ `systemd-analyze verify` 未在本机执行（无 systemd），归 Pi
- [x] ✅ **Pi 热点/WAN forwarding 默认拒绝**，worker 默认无公网 egress
  - `configure-hotspot.sh:35` 已改：不用 `ipv4.method shared`（那会开 NAT/转发），
    改 `ipv4.method manual` + `ipv4.never-default yes` + `ipv4.gateway ""`
  - ⚠️ 本机无 `nmcli`，脚本未实际执行过
- [x] ✅ **ESP 网络配置归 L**（本轮未改固件，仅知其约束）
- [x] ✅ **IPC 限制消息长度、schema、命令能力、请求超时**
  - `WorkerIpc.h`：`kMaxMessageBytes`、`schema` 校验、op 白名单、`kTimeoutMs`
  - worker 侧对超长帧直接退出，不尝试继续解析
- [x] ✅ **盆栽 C ABI 业务迁至 worker/可信服务**，宿主只跑受控 UI
  - 模块在 worker 进程内加载，宿主不 dlopen 模块
- [ ] ❓ **worker 崩溃不带崩 Qt 主程序**，session/资源可回收
  - 代码已实现（`workerExited` 信号只报告不崩溃）；`module_worker_ipc_test` / `worker_isolation_test` /
    `module_worker_async_test` 在本机验证了 IPC 与进程隔离
  - **但沙箱一条都没真正生效过**（degraded 模式），实机证据仍需 Pi
- [x] ✅ **补锁屏任务能力测试**（与 T-13 联动）— `auth_service_test:316` 断言锁屏后原任务仍可追加
- [ ] ❓ **本机无法验证的三项**（B-03 那三条）：worker UID ≠ 宿主、拒网、拒读 `$HOME`/DB

**验收**（按 §0.4 判定，不要求固定 UID）：worker PID 的 UID ≠ 宿主 UID ❓；`systemd-analyze verify` 通过且 `systemctl show` 里 `DynamicUser=yes` ❓；读不到 DB/密钥/`$HOME` ❓；worker 内 `curl` 外网失败而宿主网络正常 ❓；kill worker 后宿主存活 ✅（本机 IPC 级）。
**注意**：`QProcess` 本身不是沙箱，必须证明 unit 实际生效，不能只交配置文件。**这是当前最大的未验收项。**

### D9 — T-09 Qt 登录、绑卡、锁屏、挂失（🟢 guarded）

> 原依赖 T-04、T-08、T-10 已拆为两段（§0.3）。仍是**一张任务卡**，不新增编号。
> 追溯 E2E-002 · Pi/Wayland/Qt 6.5.3 实机

#### D9a — PIN / 锁屏 / 切用户（**现在就能做**）

> 依赖 **T-04（D4）、T-10（D8）**。不需要 RFID。

- [x] ✅ **新增登录/锁屏/用户选择 QML 组件**
  - `qml/SessionGate.qml`（门禁 + 5 分钟自动锁）、`LoginPage.qml`、`LockScreen.qml`、
    `PinPad.qml`、`UserPicker.qml` —— 共 5 个，此前本文写的"组件数 = 0"已过期
- [x] ✅ **在 CMake 资源表登记**（`UI_QML_FILES`，`CMakeLists.txt:346-347`）
  - 同时登记 `AdminPairingPage.qml`、`TaskFinishDialog.qml`
- [x] ✅ **PIN 页面**、低敏用户选择、错误/离线状态
- [x] ✅ **锁屏**：无交互 5 分钟自动锁定（`SessionGate.qml:43 idleSeconds=300`）；隐藏敏感内容
  - 锁屏时卸载模块页（`modulePageHost.unload()` + 清空 `activeSession`），
    不只是盖遮罩——`visible:false` 数据仍在内存可读
  - 通知横幅 `visible:!sessionManager.locked`、桌面 `enabled` 加 `!sessionManager.locked`
  - 锁屏后必须重输 PIN，**不提供刷卡解锁**（锁屏态没有 `resolveCard` 入口）
- [x] ✅ **切用户前显示任务完成/取消/明确停止流程**
  - `TaskFinishDialog.qml`，由 `SessionGate.qml:75` 在有未完成任务时挂载
  - 服务端 `login()` 返回 `BUSY` 是真正拦截点，弹窗只是给用户可操作出口
- [x] ✅ **模块页改用独立 `QQmlEngine`**（§0.7）
  - `src/core/ModulePageHost.{h,cpp}` 独立引擎 + 独立 context，只注入
    `sessionModel` / `userContext`（只读 `activeUserId`/`locked`/`isAdmin`）/ 主题
  - `Main.qml` 已无 `setSource()`，改由 `ModulePageHost` 承载
- [x] ✅ **管理员配对/撤销页面**（F 裁决：页面归你，后端归 D 的 TrustStore）
  - `qml/AdminPairingPage.qml`，已接进 `Main.qml:1960`
  - 后端已就绪：`TrustStore` + `SecurityService::trustedBridges/pairBridge/revokeBridge/healthy`
  - 接线请求体为 `SecurityService::requestedCapabilities` / `grantCapability` / `revokeCapability` / `recentAudit`
  - ⚠️ **未实现的一点**：页面没有"操作前重验管理员 PIN"的二次确认。
    当前只靠 `sessionManager.isAdmin` 判定，管理员会话被劫持即可直接撤销。
    见文末「尚未完成清单」。
- [x] ✅ **QML 只调可信服务**，所有操作由服务端再次校验会话/权限
  - `SecurityService::requireAdmin()` / `requireUser()` 对每个动作独立校验并记审计，不信 QML 侧状态
- [ ] ❓ **Wayland 实机触摸 + 键盘完成闭环** — 需 Pi（本机 offscreen/xcb，非 Wayland）

#### D9b — 绑卡 / 挂失（等 RFID 链路）

> 依赖 **T-07、T-08（D 的）、D9a**。只有这一段真需要卡。

- [x] ✅ **显式绑卡后端**：`SecurityService::bindCard()` 要求已登录 + 库已打开，反查合法 UID 长度
  - 卡匹配与绑定走 `CardIndex` + `deriveUidIndex()`
  - `CardBindingWindow`（30 秒单次窗口，`CardIndex.h:42`）已实现并有测试
- [ ] ⚠️ **绑卡没有 UI，也没有接 30 秒窗口**
  - `qml/` 下无绑卡界面；`SecurityService::bindCard()` **不调 `CardBindingWindow`**，
    即当前只要已登录就能随时绑卡，**没有"重验 PIN + 30 秒单次窗口"约束**
  - 这是 D9b 真正的缺口，不是"等 RC522」——窗口逻辑不依赖硬件
- [x] ✅ **管理员挂失后端**：`revokeCard()` / `revokeAllCards()`
  - `CardIndex::revoke()` 撤销映射；不触碰 DB key，**撤销卡不会解锁 DB**（DB key 只由 PIN 派生的 KEK 解封）
- [ ] ⚠️ **挂失没有 UI，也没有重验管理员 PIN**
  - `revokeCard()` 只调 `requireUser()`（当前登录用户自己的卡），
    没有"管理员撤销成员卡"的入口，也没有 `requireAdmin()`
- [ ] ❓ **刷真卡完成绑定/挂失闭环**（需真 RC522 + 真桥板）

**验收**：D9a —— PIN 正确才登录 ✅、锁屏需重输 ✅、管理员能配对/撤销 ⚠️（缺二次 PIN 验证）；
D9b —— 刷卡不直接登录 ✅（`resolveCard` 只回 userId，`SessionGate.qml:104` 跳到 PIN 页）、
绑卡须短时窗口 ❌（窗口类存在但未接线）、挂失不解锁 DB ✅（结构上成立）。

### D2 — T-02 Z 部分：Qt 构建环境（⚪ audit，与 D 共担）

> 依赖 T-01 · 追溯 IT-002 · **别人所有 Pi 侧工作都依赖这个能构建**

- [ ] ❓ **在 Pi 3B / 64-bit Bookworm / Qt 6.5.3 / Wayland 上构建主项目**
- [ ] ❓ **记录内核、编译器、Qt plugin path、串口权限组**
- [x] ✅ **Pi 服务不得以 root 运行** — unit 用 `DynamicUser=yes`（临时 UID，非 root）；
  `ProtectSystem=strict` + `NoNewPrivileges=yes` 已写。实机核对归 Pi（见 D11）。

---

## 4. Z 协作项

> 主责在他人，你负责其中用户/会话/页面/授权部分。
> 修订后 D 只做 Pi 侧，L 等软件完成后才动固件——你的协作项基本都在 Pi 侧。

### D8b — T-08 RC522 事件闭环（Z 部分：密钥与匹配）

> 主责 D（PBR/事件链路）· 依赖 T-04（D4）、T-07（D 的）· **D 在等你的 `uidIndexKey`**
> **文件边界**：`PogoControlTransport.cpp`、`HostEventDetector.h`、`ConnectionManager.cpp` 都是 **D 的**，你不改；你只新增用户匹配逻辑与 `uidIndexKey()` 调用点。

- [x] ✅ **`uidIndexKey()` 已在 §2 D1 列出，已交付**
- [x] ✅ **UID 匹配回调**：`SecurityService::resolveCard()` 用 `deriveUidIndex()` 结果选用户
  - `deriveUidIndex()` 由 D 在 `src/protocol/uid_index.cpp` 实现，Z 只提供密钥
- [x] ✅ **选中用户后只显示 PIN 页**，不直接登录
  - `SessionGate.qml:104 onCardUserSelected` 只设 `preSelectedUserId` 并跳 PIN 输入态
- [x] ✅ **raw UID 只短暂驻留内存**，不进 DB / log / QSettings
  - `SecurityService::resolveCard()` 里派生索引后立即丢弃 raw；
    `card_flow_test::rawUidIsNotPersisted` 有断言
- [ ] ❓ **验证未配对/篡改/重复/过期事件全部拒绝** — 需真桥板发事件；本机无可发包的对端
- [ ] ❓ **确认 UID hex 大小写都接受**（校验时折叠，`deriveUidIndex` 收原始字节）
  - 现状：`QByteArray::fromHex()` 本身大小写都吃，但**没有显式折叠，也没有测试**
  - 但需要真的验证一次：`fromHex("0A")` 与 `fromHex("0a")` 得到的字节相同，索引自然一致
  - 补一个断言即可，属小项

> **`deriveUidIndex()` 归 D，已实现**，Z 侧无需再动。此前本文把这条记为"等 D"，现已解除。

### D12 — T-12 业务数据 TLS/BLE（Z 部分：**只是会话与授权**）

> **主责是 D，不是你。** 传输层（`BleTransport`、`HotspotLanTransport`）在 T-12 的 D 文件范围内。
> P0 要**同时实现 BLE 和 LAN**，参数见 **§0.8**。你在 T-12 里负责传输层之上的会话、授权与门控验证。
> ESP 侧（`sm_io_cap`、TLS server、NVS 密钥）归 **L**。

#### Z 负责的部分（现在就能写）

- [x] ✅ **会话绑定用户与能力**：`LaneGate::beginAttach(activeUserId_, instance)`
  - LAN/BLE 连接建立后，业务数据挂在当前会话的用户与已授权能力上
  - 没有会话就不接受业务数据；`activeUserId` 变更时 `invalidateAll()` 全部作废
- [x] ✅ **未认证/未授权时业务数据必须进不来**（§0.8.2）
  - `LaneGate::markAuthenticated()` 是唯一放行口；未标认证前 `acceptPayload()` 拒绝
  - LAN 指纹比对在 `ConnectionManager.cpp:324`、BLE 在 `:340` 标记
  - 单测：`lane_gate_test` 覆盖未认证拒发、认证后放行、失效后再拒
- [x] ✅ **撤权立即失效**：`LaneGate::invalidateAll(reason)` → `invalidated` 信号
  - `ConnectionManager::setActiveUserId()` 在切用户/退出时遍历所有 session 调用
  - 断连与密钥销毁由 D 执行（`HotspotLanTransport` / `BleTransport`）
- [x] ✅ **lane 热切换的会话规则**（§0.8.5）
  - `LaneGate.cpp:44` `switchCount_ >= kMaxSwitches`（3）后拒绝切换
  - 切换必须重新 `markAuthenticated()`，**不搬用旧会话密钥**（`beginAttach` 重置认证态）
  - 切换时丢弃未完成分片（由 `BleTransport` 内部按 20 字节切，对上层透明）
- [x] ✅ **profile 判定**：复用 `TransportPolicy`（`ConnectionManager.cpp:270/292`），未另写一套
  - `fixed_ble` + `hotspot_lan` 包并存是**合法**组合（§0.8.6），未被拒绝
- [x] ✅ **单条消息上限 512 字节**（§0.8.4）：`LaneGate::payloadAcceptable()` → `INVALID_MESSAGE`
  - `ConnectionManager.cpp:362` 在 `data` 分支先判，不截断不重试不降级
- [ ] ❓ **断线 / 切用户 / 撤权后旧 session 不可继续发数据**（与 D 联调）
  - 逻辑已实现并有单测，但需 D 的真实传输层联调确认
- [ ] 📋 **`TLS 会话 ≠ 设备已认证`、`BLE 配对 ≠ 模块已授权`** 写进会话代码注释
  - `LaneGate.h` 里表达了同样的约束（`markAuthenticated` 必须显式调用，不因连接建立自动成立），
    但没有照抄这句话。属文档小项，可在下次触碰该文件时补一句。

> **`LaneGate` 已真正接进 `ConnectionManager`**（不是孤立类）：`beginAttach` / `markAuthenticated` /
> `invalidateAll` / `switchRequired` 四个点都有调用。此前本文记为"未实现"已过期。

#### 交给 D 的部分（不要自己改这些文件）

- `BleTransport.{h,cpp}`：BlueZ 配对门控、挑战-响应、20 字节分片
- `HotspotLanTransport.{h,cpp}`：`QSslSocket` + 指纹比对 + 无明文回退

#### 交给 L 的部分

- ESP 侧 `sm_io_cap`/`BLE_SM_*`/加密属性、TLS server 与自签证书、**NVS 密钥存储**

### D13-D16 — 页面与会话权限（Z 部分）

> 修订后这几项主责明确拆为：L 固件、D Pi 侧接口/字段/路由、Z 页面/UI。**你不碰固件。**
> **验收收口人是 L**（T-13~T-16 的实机签字归 L）。你交的是 Pi 侧页面与用户归属，**不是整项验收的签字人**。

- [x] ✅ **T-13 用户归属/UI**：盆栽全部传感器数据按用户归属显示；退出后旧用户上下文不能读取
  - 模块页经 `moduleData`（`ModuleDataApi`）读写，底层是当前用户的 SQLCipher 库
  - `ModuleDataApi` 注入的是 `UserDatabase*`，切用户后旧 context 读不到新库
  - 测试：`module_data_api_test::switchingUserDoesNotLeakData` / `scopeIsIsolated` 断言不串档
  - **前置**（§0.7 独立引擎）已完成：`ModulePageHost` 独立 `QQmlEngine`
- [x] ✅ **T-14 页面/会话权限**：啸天受控状态 UI 复用安全业务 lane
  - `module-devkit/examples/xiaotian-fence/qml/ModulePage.qml` 走 `sessionModel` + `moduleData`
  - 不当作已有插件（模块页在独立引擎内，只看到 `sessionModel` / `userContext` / `moduleData` / 主题）
- [x] ✅ **T-15 两模块分别运行**：用户/device_id/slot_id 不丢失
  - `ModuleDataApi` 按 `moduleId + instance` 分作用域（`scopeIsIsolated` 测试）
  - `slot_router_test` 覆盖 slot 绑定与 stale detach 拒绝
- [x] ✅ **T-16 双槽同时运行**：两个 slot/device/event/user context 不串
  - slot router + `ModulePageHost` 每 session 一个实例；单槽拔出只 `unload` 该 session
- [ ] ❓ **补测离槽继续、旧槽换设备不继承授权、桥失联时本机 PIN 解锁查看本人任务**
  - 需真硬件拔插；本机无法模拟物理 pogo 接触

**验收收口人是 L**（T-13~T-16 的实机签字归 L）。Z 交的是 Pi 侧页面与用户归属，不是整项验收的签字人。
上述 ✅ 是**主机模拟层**的证据；实机四条仍为 ❓。

---

## 5. T-01 签认

> [IT-001](04-dev-tasks-f-day1.md#9-it-001-审查签认与验收顺序) 中 Z 行仍为「待本人确认」。
> F 文档审定 + 密码协议交付**不**替代你的技术签认。Day 1 闸门要求收集技术签认。
>
> **签认是否阻塞开工**：**不阻塞代码**。T-04/T-10/T-11 的接口与参数已由 §0 定死，你可以直接写实现；但 **T-17 与 P0 签字要求 Z 行签认**，且未签认前不能宣称「T-01 通过」。所以：**先签认（半天内），再开工**，别拖到最后。

- [ ] ❌ **签认确认范围**：PIN/session、SQLCipher、worker IPC/OS 隔离、UI 边界
  - 记录格式：日期 / commit / PR 或 Issue
  - **仍未签认**。注意：worker IPC/OS 隔离的实机证据未取得（见 D11），
    按本文 §5 自己写的原则「不预签不存在的能力」，**现在不宜签 D11 那一条**；
    其余三条（PIN/session、SQLCipher、UI 边界）证据充分，可先签。
- [ ] 📋 **确认权限矩阵**（F 审核稿第 2 节）与实现一致
  - 实现已存在：`src/security/PermissionMatrix.{h,cpp}` + `permission_matrix_test`
  - 待做的是人读一遍对齐，不是写代码
- [ ] 📋 **确认设备生命周期语义**（锁屏不停任务、切前收尾、迟到事件不串档）
  - 三条均已实现并有测试（`auth_service_test:316`、`SessionManager::login` 返回 `BUSY`、`TaskCapability` 绑定 userId+deviceId）
  - 待做的是本人书面确认
- [ ] 📋 **确认 `uidIndexKey` 密钥归属切分**（DR-14）你接受
  - 实现已按 DR-14 落地（Z 只提供 32 字节密钥，D 的 `deriveUidIndex` 自己算 HMAC）
  - 待签认
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

---

## 附录：2026-10-29 核对方法

本次同步**只改勾选与状态标注**，未改 §0 任何裁决参数。核对基于 `54c9bcd`，方式是逐项 grep 代码 + 看测试，不凭记忆。

### 核对过的硬证据

| 项 | 证据位置 |
|---|---|
| D1 密钥 | `security/uid_index_key.cpp`（`QRandomGenerator::system` + `ReadOwner\|WriteOwner` + `rotateUidIndexKey`） |
| D4 KDF/限速 | `security/PinPolicy.h`（`kProductionIterations=600000`、`kMaxAccountFailures=5`、`kAccountLockMs=60000`） |
| D4 会话撤销 | `core/ConnectionManager.cpp:453 setActiveUserId()` → `LaneGate::invalidateAll()` |
| D8 存储 | `security/SqlCipherStore.cpp:62`、`AuthService::changePin()` 重封装路径 |
| D8 跨库 | `UserDatabase::openForeign()` + `user_database_test:108` |
| D11 worker | `core/ModuleManager.cpp`（4 处 `ModuleWorkerClient`）、`packaging/orange-pi-worker@.service` |
| D9a QML | `qml/SessionGate.qml`（`idleSeconds=300`）、`AdminPairingPage.qml`、`TaskFinishDialog.qml` |
| §0.7 独立引擎 | `core/ModulePageHost.cpp:94-96`（只注入 `sessionModel`/`userContext`/`moduleData`/主题） |
| D12 门控 | `core/LaneGate.h`（`kMaxBusinessPayload=512`、`kMaxSwitches=3`）、`ConnectionManager.cpp:362` |
| D13-16 数据 | `ModuleDataApi` + `module_data_api_test::switchingUserDoesNotLeakData` |
| 盆栽明文 JSON | `History.h:26` 默认关闭 + `TerminalApplication.cpp:67 observationSink` → `SessionManager::recordObservation()` |

### 测试现状

`ctest --test-dir build` → **31/32 通过**（2026-10-28 记录）。唯一红是 `startup_splash_lifecycle`，
原因是字体像素尺寸断言 `< 0.5px`，x86_64 offscreen 下永不可能通过。**已知、不改**。

---

## 尚未完成清单（全部 29 项，按能不能现在做分类）

### A. 现在就能做完（3 项，不依赖硬件）

| 项 | 说明 | 位置 |
|---|---|---|
| **D9b 绑卡 UI + 接 `CardBindingWindow`** | 窗口类已实现有测试，但 `SecurityService::bindCard()` 没调它，qml 也没界面。**现在已登录就能随时绑卡，没有 30 秒窗口约束** | `SecurityService.cpp:257` |
| **D9b 挂失：管理员撤销成员卡** | `revokeCard()` 只允许撤自己的卡，无 `requireAdmin()` 入口，无界面 | `SecurityService.cpp:276` |
| **管理员配对/撤销前重验 PIN** | `AdminPairingPage.qml` 只靠 `sessionManager.isAdmin` 判定，无二次 PIN | `AdminPairingPage.qml` |
| **旧明文记录的迁移标记** | `markLegacyHistoryMigrated()` 零调用，界面无法区分"还没导入"与"没有历史"。**禁止顺手做自动导入** | `UserDatabase.h:154` |

> 另有一项写在 D9a 正文而非 checkbox 里：**管理员配对/撤销前重验 PIN**。
> `AdminPairingPage.qml` 目前只靠 `sessionManager.isAdmin` 判定，无二次 PIN 确认。

### B. 小的补测/补注释（3 项）

| 项 | 说明 |
|---|---|
| UID hex 大小写折叠断言 | `fromHex` 本身兼容，补个断言即可 |
| `TLS 会话 ≠ 设备已认证` 注释 | `LaneGate` 已表达语义，没照抄这句话 |
| 管理员读他人数据被拒的审计埋点 | 目前没有可被拒绝的入口，等有界面入口时一并做 |

### C. 等 Pi 实机（18 项）

B-02 ×3（Pi SQLCipher 耗时/OS/plugin path/SDK 哈希）、B-03 ×3（worker UID/拒网/热点不转发）、
B-06 ×1（KDF 延迟，**已超 2 秒预算，待 F 裁决**）、D2 ×2（Pi 构建/工具链记录）、
D8 ×2（DB·WAL·log 无敏感值/Pi 延迟）、D9 ×2（Wayland 触摸/真卡闭环）、
D11 ×2（崩溃恢复实机/单元生效性）、D8b ×1（事件拒绝）、D12 ×1（与 D 联调）、D13-16 ×1（拔插时序）

（D11 的第二条「本机无法验证的三项」与 B-03 三条内容重合，计入 B-03，不重复计数。）

### D. 等人：T-01 签认（5 项）

T-01 签认：证据已足（除 D11 实机那条），待本人书面确认。
