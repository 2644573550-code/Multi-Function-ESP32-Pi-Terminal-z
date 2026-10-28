# B-02 / B-03 / B-06 预检回执（Mr.Z）

> 格式按 `04-dev-tasks-f-day1.md` §8：
> `B编号 / owner / commit / 目标硬件与版本 / 命令与脱敏结果 / PASS或FAIL或NOT_RUN / 风险 / 修复动作 / 截止日 / PR链接`
>
> **本机 ≠ 目标机**。下面所有结果来自 Pop!_OS 22.04 / x86_64 / GCC 15.2 / Qt 6.5.3，
> 而目标是 Pi 3B / ARM64 Bookworm / Qt 6.5.3 / Wayland。凡标 `NOT_RUN` 的必须是 Pi 实测。

---

## B-02 / Z（驱动）/ `97441b6` / 本机 x86_64

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

**CMake 集成**（`pi_terminal/CMakeLists.txt`）

```
-- SQLCipher: /lib/x86_64-linux-gnu/libsqlcipher.so (headers /usr/include/sqlcipher)
```

找不到库时 `message(FATAL_ERROR)`，不静默降级成 SQLite。已用 `find_path(... PATH_SUFFIXES sqlcipher)` 处理 Debian 的头文件布局。

**单测**（`ctest -R sqlcipher_user_isolation`）

```
10/10 passed
QINFO: cipher_version: "4.13.0 community"
```

覆盖：错误 key 拒绝、文件头非明文、每用户隔离、跨用户 openForeign 拒绝、缺失值落 NULL、分页上限 100。

### 判定

| 项 | 结果 |
|---|---|
| C API 可链接、`sqlite3_key` 可用 | **PASS**（本机） |
| 错误 key 返回 26 | **PASS**（本机） |
| `cipher_version` 非空（非普通 SQLite） | **PASS**（本机，4.13.0） |
| DB 文件非明文 | **PASS**（本机） |
| Pi 上打开耗时与 DB 大小 | **NOT_RUN** |
| Pi 上 SQLCipher 大版本 | **NOT_RUN** |

### 偏差（要提 PR 改文档）

1. **包名错了**：§0.1 写 `apt-get download libsqlcipher-dev libsqlcipher0`，
   Ubuntu 24+ 是 `libsqlcipher2`（本机 `apt-cache search sqlcipher` 确认）。
2. **版本不符**：文档说本机验证用 bookworm 3.4.1，实际本机 `libsqlcipher-dev` 已装且是 **4.13.0**。
   §0.1 明确要求「若 Pi 上是 4.x 而本机验证是 3.4.1，必须在 Pi 上重跑」——
   现在是**本机 4.13、Pi 待测**，同样要重跑，方向反过来。
3. **`-DSQLITE_HAS_CODEC` 的作用没写清**：文档写在「编译」注释里，但它不只是链接开关，
   **不加这个宏 `sqlite3_key` 连声明都没有**（实测 `-Wimplicit-function-declaration`）。

### 风险

- 4.x 与 3.x 的默认 KDF 参数不同，DB 不跨大版本互换。代码已把 `cipher_version` 写进 `meta` 表，
  但没有拒绝跨版本打开的强制检查——**P0 内够用，换库时需要补**。
- Pi 若装的是 3.4.1，本机 4.13 生成的测试库打不开（反过来也一样）。这不算缺陷，是要知道的约束。

### 截止日 / PR

PR: `feat/z-p0-security`（D8 提交 `f5354e0`）。Pi 实测待补，建议与 B-03 同批次上机。

---

## B-03 / Z（与 D 协作）/ `97441b6` / 本机 x86_64

### 命令与结果

**worker IPC 契约**（`ctest -R worker_ipc_contract`）

```
11/11 passed
```

覆盖：编码往返、长度前缀不符拒绝、超 65536 拒绝、未知 schema 拒绝、深度 >8 拒绝、
op 白名单（publishSample/readOwnRecords/reportState/stop）、unit 名过滤、
**launcher 必须走 systemctl 而非直接 QProcess**、unit 文件七条沙箱指令齐全。

**unit 文件** `pi_terminal/packaging/orange-pi-worker@.service`

```ini
DynamicUser=yes
PrivateNetwork=yes
PrivateTmp=yes
ProtectHome=yes
ProtectSystem=strict
NoNewPrivileges=yes
RestrictAddressFamilies=AF_UNIX
```

**热点转发**（`scripts/configure-hotspot.sh`）

```diff
- ipv4.method shared ipv4.addresses 192.168.50.1/24
+ ipv4.method manual ipv4.addresses 192.168.50.1/24 ipv4.never-default yes ipv4.gateway "" ipv4.route-metric 1000
```

`shared` 会开 NAT/转发，模块就能经热点上网。改用 `manual` + `never-default` + 空网关。

### 判定

| 项 | 结果 |
|---|---|
| IPC 消息长度/schema/深度/op 白名单 | **PASS**（本机） |
| unit 文件声明齐全 | **PASS**（本机，静态检查） |
| 热点不再 `ipv4.method shared` | **PASS**（本机，仅改脚本） |
| worker PID UID ≠ 宿主 UID | **NOT_RUN**（需 Pi） |
| `systemd-analyze verify` 通过 | **NOT_RUN**（需 Pi 或 systemd 环境） |
| `systemctl show` 里 `DynamicUser=yes` | **NOT_RUN**（需 Pi） |
| worker 读不到 DB / `$HOME` | **NOT_RUN**（需 Pi） |
| worker 内 `curl` 外网失败 | **NOT_RUN**（需 Pi） |
| kill worker 后宿主存活 | **NOT_RUN**（需 Pi） |
| 本机 nmcli 可用性 | **不可用**，热点脚本无法在本机预验 |

### 风险

- **`QProcess` 不是沙箱**：本类强制走 `systemctl start`，但如果 unit 没装进
  `/etc/systemd/system/`，`systemctl` 会失败而不是降级 —— 这是有意的 fail-closed，
  但部署时必须先跑安装步骤，否则模块起不来。
- `DynamicUser=yes` 下 worker UID 每次启动都变，**不能**用固定 UID 判定文件属主。
  已用 `StateDirectory`/`RuntimeDirectory` 让 systemd 分配。
- 热点改用 `manual` 后，模块拿不到网关，**必须只访问 Pi 本身**。
  如果将来要让模块访问 Pi 上的某个端口，需确认防火墙放行而不是恢复 `shared`。

### 截止日 / PR

PR: `feat/z-p0-security`（D11 提交 `f5354e0`）。全部实机项待 Pi。

---

## B-06 / Z / `97441b6` / 本机 x86_64

### 命令与结果

**KDF 参数**（§0.2 冻结值）

```cpp
constexpr int kProductionIterations = 600000;   // 硬编码，不得由配置降低
constexpr int kSaltBytes = 16;
constexpr int kKekBytes = 32;
```

单测 `initTestCase` 断言 `kProductionIterations >= 600000`，防止未来有人改小。

**认证/限速单测**（`ctest -R user_pin_session`）

```
20/20 passed（36 秒，600k 迭代真跑）
```

覆盖：PIN 格式（≥6 位 ASCII 数字，拒全角）、首次管理员只能建一次、
注册表不含 PIN、正确 PIN 解出 32B DB key、未知账号与错误 PIN 返回同一 code、
终端级 1 次/秒、连错 5 次锁 60 秒、**重启不清锁定**、改 PIN 重封装 DB key（DB key 不变）、
锁屏保留任务追加但撤销前台、切用户需先收尾任务、迟到事件被拒。

**首次管理员初始化**（B-06 要求的本地受控入口，无默认 PIN）

```bash
export XDG_CONFIG_HOME=/tmp/opencode/admintest
echo "246810" | ./build/orange_pi_terminal --init-admin "Mr.Z" --init-pin-stdin
# created administrator Mr.Z (60d96f80c802461d7832fe069af6f508)

# 第二次必须拒绝
echo "999999" | ./build/orange_pi_terminal --init-admin "evil" --init-pin-stdin
# init-admin: user registry already exists at .../users.json; refusing to create another administrator.

ls -l .../users.json
# -rw------- 1 tokisakikurumi tokisakikurumi 363 ... users.json
grep -c '246810' .../users.json
# 0    （PIN 不落盘）
```

### 判定

| 项 | 结果 |
|---|---|
| KDF = PBKDF2-HMAC-SHA256、600k、salt 16B、输出 32B | **PASS**（本机） |
| 生产常量有断言保护 | **PASS**（本机） |
| 锁定状态跨重启 | **PASS**（本机） |
| 首次管理员本地受控、无默认 PIN | **PASS**（本机） |
| 注册表不含 PIN / 权限 0600 | **PASS**（本机） |
| Pi 3B 上 600k 迭代延迟 | **NOT_RUN** |
| Pi 3B 上内存峰值 | **NOT_RUN** |

### 风险

- **本机 PBKDF2 600k 在 x86 上约 1.8 秒/次**（20 个用例 36 秒，其中 13 次是完整 KDF）。
  Pi 3B 的 ARM Cortex-A53 大约慢 3~5 倍，**很可能超过文档 §0.2 预估的 1.5 秒上限**。
  如果实测 > 2 秒，按文档要提 PR 附数据，**不得自行降低迭代数**。
- 注册表是明文 `users.json`：能防「拿到文件也解不开 DB key」（验证值是 `SHA256("p0-pin-verify-v1"‖KEK)`，
  不是 KEK 本身），但**防不住「看到有几个用户、谁是谁」**。文档没要求加密这部分。
- 终端级 1 次/秒限速是**进程内状态**，重启清零。文档只要求账号锁定跨重启，未要求终端限速持久化。

### 截止日 / PR

PR: `feat/z-p0-security`（D4 提交 `bc5253e` + `97441b6`）。
**KDF 延迟是最可能推翻当前参数的一项，建议优先上机。**

---

## 三项共同的未决项

| 项 | 需要什么 | 谁来做 |
|---|---|---|
| Pi 3B 构建与运行 | 真机 | Z（B-02/B-06） |
| systemd unit 生效性 | 真机 + sudo 一次装 unit | Z（B-03） |
| 热点无转发验证 | 真机 + nmcli | Z（B-03） |
| worker UID/拒网 | 真机 | Z（B-03） |
| SQLCipher 大版本确认 | 真机 `PRAGMA cipher_version` | Z（B-02） |

**没有一项能靠本机证据替代。** 上表五项全部完成后才能把这三份回执从 `NOT_RUN` 改为最终判定。
