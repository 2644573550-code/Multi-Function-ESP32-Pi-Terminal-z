# B-02 / B-03 / B-06 预检回执（Mr.Z）

> 格式按 `04-dev-tasks-f-day1.md` §8：
> `B编号 / owner / commit / 目标硬件与版本 / 命令与脱敏结果 / PASS或FAIL或NOT_RUN / 风险 / 修复动作 / 截止日 / PR链接`
>
> **本机 = Pop!_OS 22.04 / x86_64 / GCC 15.2 / Qt 6.5.3 / SQLCipher 4.13.0 / OpenSSL 3.5.5**
> **目标 = Pi 3B / ARM64 Bookworm / GCC 12.2 / Qt 6.5.3 / SQLCipher 3.4.1 / OpenSSL 3.0.x**
>
> 本机装了 **aarch64 交叉编译器 + qemu-aarch64**，因此部分 ARM64 行为已在本机验证，
> 不再全部是 `NOT_RUN`。凡标 `NOT_RUN` 的必须是真 Pi 才能验的项。

---

## B-02 / Z（驱动）/ `97441b6` / 本机 x86_64 + ARM64 qemu

### 命令与结果

**SQLCipher C API 可用性**（§0.1 路线，不用 Qt SQL 插件）

```bash
# 头文件与库位置（与文档写的不同，见"偏差"）
ls /usr/include/sqlcipher/sqlite3.h
ls /usr/lib/x86_64-linux-gnu/libsqlcipher.so

# 编译时必须加 -DSQLITE_HAS_CODEC，否则 sqlite3_key 没有声明
gcc t.c -DSQLITE_HAS_CODEC -I/usr/include/sqlcipher -lsqlcipher -o t && ./t
```

```
write ok                                    # 正确 key 写入成功
wrong key rc=26 (file is not a database)    # SQLITE_NOTADB
cipher_version=4.13.0 community
DB 文件头: 293c5429...（不是 "SQLite format 3")
```

**ARM64 交叉验证**（本机真跑，不是推断）

用 `ports.ubuntu.com` 的 arm64 包 `libsqlcipher-dev_4.5.6` + `libssl3t64_3.0.13`，
`aarch64-linux-gnu-gcc` 交叉编译，`qemu-aarch64` 执行：

```
cipher_version=4.5.6 community
wrong key rc=26 (file is not a database)
运行 exit=0
```

**结论：SQLCipher C API 这条路在 ARM64 上同样成立，错误 key 同样返回 26。**

**CMake 集成**（`pi_terminal/CMakeLists.txt`）

```
-- SQLCipher: /lib/x86_64-linux-gnu/libsqlcipher.so (headers /usr/include/sqlcipher)
```

找不到库时 `message(FATAL_ERROR)`，不静默降级成 SQLite。
用 `find_path(... PATH_SUFFIXES sqlcipher)` 处理 Debian 的头文件布局。

**单测**（`ctest -R sqlcipher_user_isolation`）

```
12/12 passed
QINFO: cipher_version: "4.13.0 community"
```

覆盖：错误 key 拒绝、文件头非明文、每用户隔离、跨用户 openForeign 拒绝、
缺失值落 NULL、分页上限 100、**跨 SQLCipher 大版本拒绝打开**、同大版本重开。

### 判定

| 项 | 结果 |
|---|---|
| C API 可链接、`sqlite3_key` 可用（x86_64） | **PASS** |
| C API 可链接、`sqlite3_key` 可用（**ARM64 qemu**） | **PASS** |
| 错误 key 返回 26（x86_64） | **PASS** |
| 错误 key 返回 26（**ARM64 qemu**） | **PASS** |
| `cipher_version` 非空（非普通 SQLite） | **PASS**（4.13.0 / ARM64 4.5.6） |
| DB 文件非明文 | **PASS** |
| 跨大版本拒绝打开 | **PASS**（代码 + 测试） |
| Pi 上打开耗时与 DB 大小 | **NOT_RUN** |
| Pi 上 SQLCipher 实际大版本 | **NOT_RUN**（预计 3.4.1，须确认） |

### 偏差（要提 PR 改文档）

1. **包名错了**：§0.1 写 `apt-get download libsqlcipher-dev libsqlcipher0`，
   Ubuntu 24+ 是 `libsqlcipher2`（noble 是 `libsqlcipher1`）。
2. **版本不符**：文档说本机验证用 bookworm 3.4.1，实际本机是 **4.13.0**。
   §0.1 要求「若 Pi 上是 4.x 而本机验证是 3.4.1，必须在 Pi 上重跑」——
   现在反过来：**本机 4.13、Pi 预计 3.4.1，同样必须重跑**。
3. **`-DSQLITE_HAS_CODEC` 的作用没写清**：它是**声明开关**，
   不加这个宏 `sqlite3_key` 连声明都没有（实测 `-Wimplicit-function-declaration`）。

### 风险

- **DB 不跨大版本互换**：本机 4.13、ARM64 包 4.5.6、Bookworm 3.4.1，三个都不兼容。
  代码已把 `cipher_version` 大版本写进 `meta` 表**并在打开时强制校验**，
  不符则拒绝打开（测试 `cipherMajorVersionIsRecordedAndEnforced` 覆盖）。
- Pi 上的真实大版本必须实测确认，否则不知道 Pi 建库后能否被别的机器读。

---

## B-03 / Z（与 D 协作）/ `97441b6` / 本机 x86_64

### 命令与结果

**worker IPC 契约**（`ctest -R worker_ipc_contract`）

```
11/11 passed
```

覆盖：编码往返、长度前缀不符拒绝、超 65536 拒绝、未知 schema 拒绝、深度 >8 拒绝、
op 白名单、unit 名过滤、**launcher 必须走 systemctl 而非直接 QProcess**、unit 七条沙箱指令。

**unit 文件静态验证**（本机 systemd 259）

```bash
systemd-analyze verify packaging/orange-pi-worker@.service
# 唯一输出：
# orange-pi-worker@test_instance.service: Command /usr/libexec/orange-pi-terminal/module_worker
# is not executable: 没有那个文件或目录
# exit=0
```

**`systemd-analyze verify` 通过**——唯一的提示是 worker 二进制尚未安装，属预期。

**热点转发**（`scripts/configure-hotspot.sh`）

```diff
- ipv4.method shared ipv4.addresses 192.168.50.1/24
+ ipv4.method manual ipv4.addresses 192.168.50.1/24 ipv4.never-default yes ipv4.gateway "" ipv4.route-metric 1000
```

`shared` 会开 NAT/转发，模块能经热点上网。改用 `manual` + `never-default` + 空网关。

### 判定

| 项 | 结果 |
|---|---|
| IPC 消息长度/schema/深度/op 白名单 | **PASS** |
| unit 文件声明齐全 | **PASS** |
| `systemd-analyze verify` 通过 | **PASS**（本机 systemd 259） |
| 热点不再 `ipv4.method shared` | **PASS**（脚本已改） |
| worker PID UID ≠ 宿主 UID | **NOT_RUN**（需 Pi） |
| `systemctl show` 里 `DynamicUser=yes` | **NOT_RUN**（需 Pi 实际启动 service） |
| worker 读不到 DB / `$HOME` | **NOT_RUN**（需 Pi） |
| worker 内 `curl` 外网失败 | **NOT_RUN**（需 Pi） |
| kill worker 后宿主存活 | **NOT_RUN**（需 Pi） |
| 热点实际不转发 | **NOT_RUN**（本机无 nmcli） |

**版本对照**：本机 systemd **259**，Bookworm 是 **252**。
`DynamicUser=` / `PrivateNetwork=` 自 **232** 起可用，252 满足；
`RestrictAddressFamilies=AF_UNIX` 同样早于 252。**版本前提成立。**

### 风险

- **`QProcess` 不是沙箱**：本类强制走 `systemctl start`；如果 unit 没装进
  `/etc/systemd/system/`，`systemctl` 会失败而不是降级 —— 有意 fail-closed，
  但部署时必须先跑安装步骤。
- `DynamicUser=yes` 下 worker UID 每次启动都变，**不能**用固定 UID 判定文件属主。
  已用 `StateDirectory`/`RuntimeDirectory` 让 systemd 分配。
- `RestrictAddressFamilies=AF_UNIX` 会**禁掉所有网络**，包括 `AF_INET`。
  如果将来要让 worker 访问 Pi 本地 TCP 端口，会被挡——需重新评估这条。

---

## B-06 / Z / `97441b6` / 本机 x86_64 + ARM64 qemu

### 命令与结果

**KDF 参数**（§0.2 冻结值）

```cpp
constexpr int kProductionIterations = 600000;   // 硬编码，不得由配置降低
constexpr int kSaltBytes = 16;
constexpr int kKekBytes = 32;
```

单测 `initTestCase` 断言 `kProductionIterations >= 600000`。

**KDF 延迟实测**（新增 `tools/kdf_bench.cpp`，可带到 Pi 上复跑）

```bash
./build/kdf_bench              # 600k 迭代，3 轮取中位数
round 1: 768 ms
round 2: 758 ms
round 3: 760 ms
iterations=600000 median=760 ms min=758 ms max=768 ms
OK: within 2000 ms
```

**ARM64 qemu 对照**（含模拟开销校正）

| 场景 | 耗时 |
|---|---|
| x86_64 原生（裸 OpenSSL） | 96 ms |
| x86_64 用 qemu 模拟（量化开销） | 1424 ms → **模拟慢 14.8 倍** |
| ARM64 用 qemu 模拟 | 2694 ms |
| **ARM64 原生推算** | **182 ms** |
| **Pi 3B 推算（×3~5 慢）** | **545 ~ 908 ms** |

**结论：Pi 3B 预计 0.5~0.9 秒，远低于文档 2 秒线，当前 600k 参数不需要改。**

> 这条结论推翻了「可能超 2 秒」的担心。但推算不是实测，Pi 上仍须跑一次
> `./build/kdf_bench` 记录真实数字。

**认证/限速单测**（`ctest -R user_pin_session`）

```
20/20 passed（36 秒，600k 迭代真跑）
```

覆盖：PIN 格式、首次管理员只能建一次、注册表不含 PIN、正确 PIN 解出 32B DB key、
未知账号与错误 PIN 返回同一 code、终端级 1 次/秒、连错 5 次锁 60 秒、
**重启不清锁定**、改 PIN 重封装 DB key、锁屏保留任务追加、切用户需先收尾、迟到事件被拒。

**首次管理员初始化**（本地受控、无默认 PIN）

```bash
export XDG_CONFIG_HOME=/tmp/opencode/admintest
echo "246810" | ./build/orange_pi_terminal --init-admin "Mr.Z" --init-pin-stdin
# created administrator Mr.Z (60d96f80c802461d7832fe069af6f508)

echo "999999" | ./build/orange_pi_terminal --init-admin "evil" --init-pin-stdin
# init-admin: user registry already exists at .../users.json; refusing to create another administrator.

ls -l .../users.json     # -rw------- 1 ...
grep -c '246810' .../users.json   # 0
```

**文件系统语义**：本机 `$HOME` 是 **ext4**（`findmnt` 确认），0600 权限位可靠。
Pi 上若 `/home` 是 FAT32/exFAT，权限位会静默失效——须实测确认。

### 判定

| 项 | 结果 |
|---|---|
| KDF = PBKDF2-HMAC-SHA256、600k、salt 16B、输出 32B | **PASS** |
| 生产常量有断言保护 | **PASS** |
| 锁定状态跨重启 | **PASS** |
| 首次管理员本地受控、无默认 PIN | **PASS** |
| 注册表不含 PIN / 权限 0600 | **PASS**（ext4） |
| KDF 延迟（x86_64 原生） | **PASS**（760 ms） |
| KDF 延迟（ARM64 推算） | **PASS**（545~908 ms，非实测） |
| Pi 3B 上 600k 迭代**真机**延迟 | **NOT_RUN** |
| Pi 3B 上内存峰值 | **NOT_RUN** |
| Pi 上 `/home` 文件系统与权限位 | **NOT_RUN** |

### 风险

- 注册表 `users.json` 是明文：验证值是 `SHA256("p0-pin-verify-v1"‖KEK)` 而非 KEK 本身，
  能防「拿到文件解不开 DB key」，但**防不住「看到有几个用户、谁是谁」**。文档未要求加密这部分。
- 终端级 1 次/秒限速是**进程内状态**，重启清零。文档只要求账号锁定跨重启。

---

## 本机已排除的风险（上一版回执里误标为 NOT_RUN）

| 项 | 上一版 | 现在 | 依据 |
|---|---|---|---|
| ARM64 能否编译 | 未验证 | **可编译** | `aarch64-linux-gnu-g++` + qemu 跑通 |
| ARM64 上错误 key 是否返回 26 | 未验证 | **是** | qemu 实测 `rc=26` |
| SQLCipher 在 ARM64 是否可用 | 未验证 | **可用** | 4.5.6 arm64 包实测 |
| `systemd-analyze verify` | NOT_RUN | **PASS** | 本机 systemd 259 |
| KDF 会不会超 2 秒 | 担心可能 | **不会**（推算 0.5~0.9s） | qemu 开销校正 |
| 文件系统权限位 | 未知 | **ext4 可靠** | `findmnt` |
| systemd 版本是否支持 DynamicUser | 推断 | **确认**（232 起，Bookworm 是 252） | 版本资料 |

---

## 仍然只能在 Pi 上验的（7 项）

| 项 | 为什么本机不行 |
|---|---|
| worker PID UID ≠ 宿主 UID | 需要真的 systemd 启动 DynamicUser 服务 |
| worker 读 DB/`$HOME` 被拒 | 需要真 UID 与真文件系统权限 |
| worker 内 `curl` 外网失败 | 需要 `PrivateNetwork` 实际生效 |
| 热点不转发 WAN | 本机无 `nmcli` |
| Pi 上 SQLCipher 大版本 | 需查 Pi 的实际包 |
| Pi 上 KDF 真实延迟 | qemu 只能推算 |
| Wayland 触摸 + 键盘闭环 | 需真屏 |

**这 7 项做完才能把三份回执从 `NOT_RUN` 转成最终判定。**

---

## 上 Pi 的推荐顺序（最快暴露问题）

```bash
# 1. 依赖（5 分钟）—— 缺了直接 cmake FATAL_ERROR
sudo apt install libsqlcipher-dev libssl-dev

# 2. 确认大版本（10 秒）—— 决定 DB 兼容性
sqlcipher "PRAGMA cipher_version;"

# 3. KDF 真实延迟（3 秒）—— 唯一可能推翻参数的项
cd pi_terminal && source scripts/env.sh && ./build/kdf_bench

# 4. unit 生效性（1 分钟）
sudo cp packaging/orange-pi-worker@.service /etc/systemd/system/
sudo systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/orange-pi-worker@.service

# 5. 文件系统确认（1 秒）
findmnt -no FSTYPE --target ~
```


---

