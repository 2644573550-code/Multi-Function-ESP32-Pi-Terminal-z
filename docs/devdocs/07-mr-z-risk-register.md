# Mr.Z P0 交付 — 潜在风险清单

> 写给 Mr.F 和以后接手的人。
>
> 范围：`feat/z-p0-security` 分支相对 `upstream/main` 的 8 个提交，
> 46 个新增文件、4 个修改文件。
>
> **组织方式**：按「会不会让 P0 失败」排序，不按文件排序。
> 每条都写清楚：现象、证据、影响、建议动作、责任方。

---

## 0. 结论先说

| 分类 | 条数 | P0 会不会因此失败 |
|---|---|---|
| 🔴 **需要裁决** | 3 | 可能 |
| 🟠 **需要实机验证** | 7 | 不确定，取决于实测 |
| 🟡 **已知缺陷，接受** | 5 | 不会，但要知道 |
| ⚪ **文档缺陷** | 4 | 不会，但要提 PR |
| **合计** | **19** | — |

**最要紧的三条**：R-01（KDF 慢 8.5 倍）、R-02（刷卡选用户无解）、R-03（并发写注册表丢更新）。

---

# 🔴 需要裁决（3 条）

## R-01 · KDF 在 Pi 上可能 4~7 秒，超出文档 2 秒线

| 项 | 内容 |
|---|---|
| **现象** | `QPasswordDigestor::deriveKeyPbkdf2` 比裸 OpenSSL 慢 8.5 倍 |
| **位置** | `pi_terminal/src/security/PinPolicy.cpp` `deriveKek()` |
| **证据** | 同进程对照：763/759/762 ms vs 90/90/89 ms，输出 `same=1`（逐字节相同） |
| **安全影响** | **无**。RFC 6070 四组向量已验证 Qt 实现完全符合标准；安全性取决于迭代数，不取决于实现 |
| **可用性影响** | Pi 3B 推算 4~7 秒/次 PIN；`user_pin_session` 在 Pi 上要跑 65 秒 |
| **文档冲突** | §0.2 点名 `QPasswordDigestor`；同时「超过 2 秒要提 PR，不得自行降低迭代数」 |
| **建议** | 方案 A：改用 `PKCS5_PBKDF2_HMAC`（已依赖 OpenSSL::Crypto，零新依赖），Pi 上 0.5~0.9 秒 |
| **责任方** | **F 裁决**。代码目前保持方案 B（合规但慢），未擅自偏离 |
| **前置数据** | Pi 上跑 `./build/kdf_bench` 拿到真实数字 |

**为什么我不能自己决定**：§0.2 是 F 2026-10-28 冻结的参数，`05-mr-z-todo.md` §0 开头写明「这七条是决定，不是建议」。

---

## R-02 · 「刷卡选用户」在无人登录时无法工作

| 项 | 内容 |
|---|---|
| **现象** | 卡↔用户映射存在每用户 SQLCipher 库里，而库需要该用户的 PIN 解出的 DB key 才能打开 |
| **位置** | `pi_terminal/src/security/CardMatcher.cpp` `findOwner()` |
| **根因** | `05-mr-z-todo.md` §0.6 原文自相矛盾：<br>前半句用「未登录读不到每用户库」否定 per-user 存放<br>后半句又说「卡↔用户映射表在各用户库里仍隔离」 |
| **实测代价** | ARM64 上单次错误 key 试开约 1.6 秒（qemu）；N 个用户最坏 N 次 |
| **语义问题** | `26 (SQLITE_NOTADB)` 本应映射 `STORAGE_UNAVAILABLE`，在这条路径下变成每张卡必经的正常分支 |
| **影响** | 若 P0 演示要求「刷卡 → 弹 PIN 页 → 登录」，这条链路现在不通 |
| **建议** | 二选一：<br>A. 改成整机映射表 `AppConfigLocation/uid_map.json` 0600（只存 index→userId）<br>B. 接受「先手动选用户，再刷卡验证」的降级流程 |
| **责任方** | **F 裁决**（这是文档条款冲突，不是实现选择） |

**当前实现**：按 §0.6 字面做（`CardIndex::findOwner` 接收已解封的 dbKey 列表）。代码注释已标 `shortcut: 逐库试开以换取 §0.6 的映射隔离；用户数 >8 时改成整机映射表`。

---

## R-03 · 用户注册表并发写会丢更新

| 项 | 内容 |
|---|---|
| **现象** | `UserService::save()` 是「读整个 JSON → 改 → 写回」，无锁 |
| **位置** | `pi_terminal/src/security/UserService.cpp` |
| **触发条件** | 两个进程/线程同时 `createUser` 或 `disableUser`，后写的覆盖先写的 |
| **影响** | 可能丢失刚创建的用户，或让刚停用的用户复活 |
| **当前风险** | **低**。P0 只有单一主进程，且创建用户是管理员低频操作 |
| **建议** | P0 可接受；P1 加 `QLockFile` 或改用 SQLite 存注册表 |
| **责任方** | Z（P1） |

**为什么列为 🔴**：这是**静默数据丢失**，不报错、不留痕迹。如果 P0 期间有多进程场景（比如 CLI 初始化与主程序同时跑），会真的发生。

---

# 🟠 需要实机验证（7 条）

## R-04 · worker 隔离完全没在真 systemd 上验过

| 项 | 内容 |
|---|---|
| **已验** | `systemd-analyze verify` 在本机 systemd 259 上通过；unit 文件七条指令齐全；IPC 契约 11/11 |
| **未验** | worker PID 的 UID 是否 ≠ 宿主；`systemctl show` 是否真的 `DynamicUser=yes`；worker 能否读 DB/`$HOME`；worker 内 `curl` 能否出网；kill worker 后宿主是否存活 |
| **风险** | §0.4 明说「`QProcess` 本身不是沙箱，必须证明 unit 实际生效」 |
| **责任方** | Z（Pi 实机） |

## R-05 · 热点改 `manual` 后模块可能拿不到网关

| 项 | 内容 |
|---|---|
| **改动** | `ipv4.method shared` → `manual` + `never-default yes` + 空 `gateway` |
| **目的** | 关掉 NAT，模块不能经热点上网（P0 默认拒网） |
| **风险** | 如果模块需要访问 Pi 之外的任何地址（比如 DNS），会直接失败 |
| **未验** | 本机无 `nmcli`，脚本一次都没跑过 |
| **责任方** | Z（Pi 实机） |

## R-06 · SQLCipher 大版本不兼容（三处不一致）

| 环境 | 版本 |
|---|---|
| 本机 Ubuntu 26.04 | **4.13.0** |
| ARM64 qemu 测试用（noble） | **4.5.6** |
| Pi Bookworm 主源（预计） | **3.4.1** |

| 项 | 内容 |
|---|---|
| **影响** | DB 文件**不跨大版本互换**。本机建的库拿到 Pi 上打不开 |
| **已做防护** | `UserDatabase::checkCipherMajor()` 强制校验，不符拒绝打开（测试 `cipherMajorVersionIsRecordedAndEnforced` 覆盖） |
| **风险** | 防护只能**阻止错误打开**，不能**迁移数据**。P0 全是新库所以无影响；一旦有真实数据就麻烦 |
| **待办** | Pi 上 `sqlcipher "PRAGMA cipher_version;"` 确认实际值 |
| **责任方** | Z（Pi 实机） |

## R-07 · `/home` 若不是 ext4，0600 权限位会静默失效

| 项 | 内容 |
|---|---|
| **本机** | ext4（`findmnt` 确认），权限位可靠，测试断言组/其他位为 0 |
| **风险** | Pi 上若 `/home` 是 FAT32/exFAT/NTFS，`QFile::setPermissions` 静默失败，注册表变成人人可读 |
| **检测** | `findmnt -no FSTYPE --target ~` |
| **责任方** | Z（Pi 实机） |

## R-08 · KDF 真实延迟未测（决定 R-01）

见 R-01。Pi 上 `./build/kdf_bench` 是唯一能定案的证据。

## R-09 · Wayland 触摸 + 键盘未验

| 项 | 内容 |
|---|---|
| **已验** | 4 个 QML 组件在 `QT_QPA_PLATFORM=offscreen` 下实例化成功、单例注册正确、零 QML 警告 |
| **未验** | 真屏触摸输入数字、真键盘、字体渲染、布局在 800×480 上是否合适 |
| **风险** | `PinPad` 的 `Theme.hit` 是 48dp 下限，实际触摸目标可能偏小 |
| **责任方** | Z（Pi 实机） |

## R-10 · `startup_splash_lifecycle` 在 x86 永红

| 项 | 内容 |
|---|---|
| **现象** | `qAbs(slot->y() + slot->height()/2 - text->y() - text->height()/2) < 0.5` 失败 4 条 |
| **位置** | `pi_terminal/tests/startup_splash_test.cpp:87` |
| **原因** | 要求文字垂直居中误差 <0.5 像素，本机字体度量与 Pi 不同 |
| **判断** | **不是我的改动导致**，接手前就红 |
| **建议** | 不改（可能是故意卡严格值抓 Pi 字体回归）。若确认无意义，放宽到 1px |
| **责任方** | F 确认是否保留 |

---

# 🟡 已知缺陷，接受（5 条）

## R-11 · 注册表是明文，能看出有几个用户、谁是谁

| 项 | 内容 |
|---|---|
| **已做防护** | 落盘存 `SHA256("p0-pin-verify-v1"‖KEK)` 而非 KEK —— 拿到文件解不开 DB key |
| **未做** | 文件名、显示名、管理员标志都是明文 |
| **影响** | 物理接触者能看到「有 3 个用户，其中一个叫 Mr.Z 且是管理员」 |
| **文档要求** | 未要求加密这部分 |
| **建议** | P0 接受；P1 若需要再看 |

## R-12 · 终端级限速不跨重启

| 项 | 内容 |
|---|---|
| **已做** | 账号连错 5 次锁 60 秒，**锁定状态落盘，重启不清除** |
| **未做** | 终端级「1 次/秒」是进程内变量，重启清零 |
| **文档要求** | §2 只要求账号锁定跨重启，未要求终端限速持久化 |
| **影响** | 重启后可以立刻再试一次（但账号锁定仍在） |

## R-13 · `SessionManager` 的 DB key 在内存里没有 mlock

| 项 | 内容 |
|---|---|
| **现状** | 解封的 DB key 存在 `QByteArray`，`clear()` 时用 `fill('\0')` 显式擦除 |
| **未做** | 未 `mlock` 防换页、未 `madvise(MADV_DONTDUMP)` 防 core dump |
| **影响** | 内存被换到磁盘或产生 core dump 时，key 可能落到文件 |
| **文档要求** | 未提及 |
| **建议** | P1 考虑；P0 威胁模型不含物理内存攻击 |

## R-14 · 服务基本没接进主程序（只有 CLI 初始化用到）

| 项 | 内容 |
|---|---|
| **已接** | `UserService` —— 仅在 `AdminInitializer::run()`（`--init-admin` 入口）里实例化，进程初始化完就退出 |
| **未接** | `AuthService`、`SessionManager`、`UserDatabase`、`CardIndex`、`LaneGate`、`ModulePageHost` —— **全部零实例化**（`grep` 确认在 `src/app/` 与 `src/main.cpp` 里 0 次出现） |
| **原因** | 按 2026-10-28 的决定推迟到 D9a 的 `Main.qml` 集成 |
| **影响** | 现在正常启动 `orange_pi_terminal` 仍然直接进桌面，登录/锁屏/用户切换功能不存在 |
| **风险** | 如果 F 以为「服务已交付」就等于「功能可用」，会误判进度。PR 描述里必须写明这一点 |
| **责任方** | Z（D9a 集成） |

## R-15 · `Main.qml` 未改，模块页仍在宿主引擎

| 项 | 内容 |
|---|---|
| **现状** | `ModulePageHost` 实现了独立引擎，测试证明宿主对象不可见 |
| **未做** | `Main.qml:675` 仍然用 `moduleLoader.setSource(...)` 走宿主引擎 |
| **影响** | §0.7 的隔离**代码已就绪但未生效**。T-13 的「QML 只见用户选择结果」目前不成立 |
| **责任方** | Z（D9a） |

---

# ⚪ 文档缺陷（4 条，建议提 PR）

## R-16 · SQLCipher 包名在 Ubuntu 24+ 变了

- §0.1 写 `apt-get download libsqlcipher-dev libsqlcipher0`
- Ubuntu 24.04+ 是 `libsqlcipher2`；noble 是 `libsqlcipher1`
- **影响**：照文档做会在 Ubuntu 24+ 上报「无法定位软件包」

## R-17 · §0.1 的版本前提与实际相反

- §0.1 写「本机验证用 bookworm 3.4.1」
- 实际本机是 **4.13.0**，而 Pi 预计 3.4.1
- §0.1 要求「若 Pi 上是 4.x 而本机是 3.4.1，必须在 Pi 上重跑」——方向反了，但**同样必须重跑**

## R-18 · `-DSQLITE_HAS_CODEC` 的作用被低估

- §0.1 把它写在「编译」注释里
- 实际它是**声明开关**：不加这个宏，`sqlite3_key` 连声明都没有（`-Wimplicit-function-declaration`）
- 不是「链接开关」，是「头文件里这个函数存不存在」

## R-19 · §0.6 自相矛盾（即 R-02 的根因）

原文：
> 密钥若存在每用户 DB 里，未登录时读不到，刷卡选用户就成了鸡生蛋。整机单钥的代价是……且**卡↔用户映射表在各用户库里仍隔离**——这个代价可接受。

前半句用「未登录读不到每用户库」否定 per-user 存放，后半句又把映射表放回 per-user 库。

**建议改法**：明确写成「映射表放整机 `AppConfigLocation/uid_map.json` 0600，只存 index→userId，不含 UID」，或者明确写成「P0 不支持无人登录时刷卡选用户」。

---

## 附：本机已排除的风险（曾经担心，已证明没问题）

| 项 | 结论 | 依据 |
|---|---|---|
| ARM64 能否编译 / 运行 | **能** | `aarch64-linux-gnu-g++` + `qemu-aarch64` 跑通 |
| ARM64 上错误 key 是否返回 26 | **是** | qemu 实测 `rc=26` |
| SQLCipher 在 ARM64 可用 | **可用** | 4.5.6 arm64 包实测 |
| `systemd-analyze verify` | **通过** | 本机 systemd 259 |
| systemd 版本是否支持 DynamicUser | **支持** | 232 起可用，Bookworm 是 252 |
| 文件系统权限位（本机） | **可靠** | ext4 |
| PBKDF2 实现是否正确 | **正确** | RFC 6070 四组向量全过 |
| 我的文件有没有编译警告 | **零** | 6 条诊断全在 `third_party/` |

---

## 附：上 Pi 的验证顺序（按风险暴露速度排）

```bash
# 1. KDF 真实延迟 —— 直接决定 R-01
./build/kdf_bench

# 2. SQLCipher 大版本 —— 决定 R-06
sqlcipher "PRAGMA cipher_version;"

# 3. 文件系统 —— 决定 R-07
findmnt -no FSTYPE --target ~

# 4. worker 隔离 5 项 —— 决定 R-04
sudo cp packaging/orange-pi-worker@.service /etc/systemd/system/
sudo systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/orange-pi-worker@.service
# 然后逐项验 UID / 文件拒绝 / 出网拒绝 / kill 恢复

# 5. 热点不转发 —— 决定 R-05
bash scripts/configure-hotspot.sh

# 6. Wayland 触摸闭环 —— 决定 R-09
```

---

## 附：交付规模（供核对）

| 项 | 数字 |
|---|---|
| 提交数 | 8 |
| 新增文件 | 46 |
| 修改文件 | 4（`CMakeLists.txt` / `configure-hotspot.sh` / `TerminalOptions.cpp` / `main.cpp`） |
| 删除文件 | 0 |
| 新增代码行 | ~4,100 |
| 新增测试 | 8 个可执行文件，75 条用例 |
| 全量测试 | 29/30（唯一红见 R-10） |
| 我的文件编译警告 | 0 |

**8 个测试可执行文件**：`uid_index_key_test`(5)、`auth_service_test`(21)、
`user_database_test`(10)、`card_matcher_test`(10)、`worker_isolation_test`(9)、
`lane_gate_test`(8)、`module_page_host_test`(5)、`login_qml_test`(7)。
（`auth_service_test` 含 4 条 RFC 6070 数据行，实际断言数 26。）
