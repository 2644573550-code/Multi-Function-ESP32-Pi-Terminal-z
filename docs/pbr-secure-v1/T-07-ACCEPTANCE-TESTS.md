# T-07 验收测试规格（待实现，未点亮）

| 项 | 内容 |
|---|---|
| 状态 | **规格文档，不含实现。** 按 `F-REVIEW-2.md` §7.5，DR-01 未关闭前 T-07 不开工；本文只是把验收条件写成可执行的用例清单，**不改变该门禁** |
| 依据 | `F-REVIEW-2.md` §5.2 T-07 的 6 条验收 + §5.3 的预裁决 + `pbr-secure-v1.md` §5/§6 |
| 交付物 | `PbrSession`、`ReplayGuard`、`EventAckManager`（均**未实现**） |
| 生产 API | **本文不定义生产接口。** 下方 C++ 骨架里的 API 只是建议形态，T-07 落地时以实际实现为准 |
| 点亮方式 | 实现完成后新增 CMake target（骨架里给了片段），**默认不加入构建** |

## 0. 为什么写成文档而不是先提交测试代码

先提交引用未定义符号的测试会让构建红掉；用 `#if 0` 包起来又会变成没人维护的死代码。
所以这里只留**可判定的用例表** + 一段可直接复制的骨架，等实现落地再点亮。

## 1. 用例表

判定基准：`pbr-secure-v1` §6.3 的错误码。**每条的期望错误码是唯一值**，
不接受"任意拒绝"（F 的 DR-01 追加条件 ③）。

### A. AEAD 信封（F 验收 ①）

| ID | 操作 | 期望 | 判定点 |
|---|---|---|---|
| T07-01 | 改帧头 `kind` | tag 失败 | 不回应、不入审计队列 |
| T07-02 | 改帧头 `dir`（把 Pi→桥 的帧回放给 Pi） | tag 失败 | AAD 覆盖 dir |
| T07-03 | 改 `auth_seq` 低 32 位（不进 nonce） | tag 失败 | AAD 覆盖 auth_seq 低 32 位 |
| T07-04 | 帧头 `length` 与实际密文长度不符 | 拒绝 | 先按 length 定长，不按 NUL 判断 |
| T07-05 | 翻转密文中间 1 字节 | tag 失败 | — |
| T07-06 | 翻转 tag 末字节 | tag 失败 | — |
| T07-07 | 未知 `key_id` | 整帧丢弃、**不回应** | 不给攻击者 oracle（§6.2 第 3 步） |
| T07-08 | 未认证期收到任意信封 | Error 帧 `authentication required` | §6.1 |
| T07-09 | 明文上限 2018 字节 + 1 | 拒绝 | 不截断（§5.1） |
| T07-10 | 信封内 `env_ver = 2` | 整帧丢弃、不回应 | — |

### B. 重放与序号（F 验收 ②③）

| ID | 操作 | 期望 |
|---|---|---|
| T07-11 | 同一信封投两次 | 第二次 `REPLAY` |
| T07-12 | `auth_seq = 0` | `REPLAY` |
| T07-13 | `auth_seq` 小于上次 | `REPLAY` |
| T07-14 | `auth_seq` 到 `2^64-1` | 触发重握手；**禁止环绕** |
| T07-15 | **顺序断言**：伪造 `auth_seq = 2^63` 且 tag 无效 | `auth_seq` 窗口**不得推进**（后续合法帧仍接受） |
| T07-16 | pi→bridge 与 bridge→pi 交错递增 | 两个方向各自独立判定，互不影响 |

> T07-15 对应我在 `D-SIGNOFF.md` 提的 **R-1**：§6.2 要求 tag 先于 `auth_seq`，
> 写反就是纯未认证 DoS。这条测试的存在本身就是防回归。

### C. 身份（F 验收 ④⑤）

| ID | 操作 | 期望 |
|---|---|---|
| T07-17 | 伪造桥板签名 | `IDENTITY_REJECTED`，**且不发送 `hello_finish`**（用 spy 断言调用顺序） |
| T07-18 | `hello_ok.boot_id4` 与事件 `boot_id` 前 8 字符不符 | `IDENTITY_REJECTED` 并断连 |
| T07-19 | TrustStore 无记录且不在配对窗口 | `IDENTITY_REJECTED` + 计数 |
| T07-20 | 已配对但 `msg3_body` 公钥与 NVS 不逐字节相等 | `IDENTITY_REJECTED`（§6.7） |
| T07-21 | 测 `r‖s` 而非 DER：构造 r 最高位为 1 的签名 | 验签成功（两侧各一条） |
| T07-22 | 撤销配对（`revokeIdentity`） | **立即断连**、销毁派生密钥；旧会话不可再发数据 |
| T07-23 | `bridge_key_id` 命中多条候选（1 字节碰撞） | 逐个用完整公钥验签；全部失败才拒绝 |

### D. TTL 与时间基准（F 验收 ⑥）

| ID | 操作 | 期望 |
|---|---|---|
| T07-24 | `age > 3000`（按 offset 计算） | `EVENT_EXPIRED`，**不执行业务** |
| T07-25 | `age < 0` | `EVENT_EXPIRED` |
| T07-26 | `ttl_ms != 3000` | `EVENT_EXPIRED` |
| T07-27 | `hello_ok.uptime_ms` 之后被改写 | 拒绝（offset 只接受一次） |
| T07-28 | `hello_ok` 缺 `uptime_ms` | 拒绝建立 TTL 基准，握手失败 |

### E. 去重与 ACK

| ID | 操作 | 期望 |
|---|---|---|
| T07-29 | 同一 `event_id` 投两次（不同 `auth_seq`） | 第二次 `REPLAY`，**业务只执行一次** |
| T07-30 | 用旧 `auth_seq` 重传同一逻辑事件 | `REPLAY`（重传必须换新 auth_seq） |
| T07-31 | 去重表满 4096 | **重建会话**，不提前遗忘 |
| T07-32 | 500 ms 内未收到 ACK | 最多重传 3 次，且不超 TTL |
| T07-33 | 重传 | 保持原始事件时间，**不刷新 TTL** |
| T07-34 | 发送队列满 | 回 `BUSY`，**不得先 ACK 后丢弃** |
| T07-35 | ACK 帧本身 | 不要求被 ACK，也不进去重表 |

### F. 状态机与失败关闭

| ID | 操作 | 期望 |
|---|---|---|
| T07-36 | 握手失败后重试 | 必须新生成临时密钥，**不复用** |
| T07-37 | 任意一条失败路径 | 临时私钥与派生密钥被零化（用可注入的零化钩子断言） |
| T07-38 | X25519 `shared` 全零（低阶点） | 断连，且**不回错误细节** |
| T07-39 | 是否存在明文 fallback | 编译产物里没有任何可打开的降级开关（源码级断言/符号检查） |
| T07-40 | `pong` / `bridge_status` | 不进 `PbrControl::validate()`，信封层按 `request_sequence` 单独路由 |
| T07-41 | schema-less `attach`/`detach` | T-07 合入时**删除** `Verdict::Legacy` 分支与规范 §6.6 |

## 2. 覆盖矩阵（对 F 的 6 条验收）

| F 的验收条目 | 对应用例 |
|---|---|
| ① 改 kind/dir/auth_seq/len → tag 失败 | T07-01～T07-04 |
| ② 同一信封投两次 → REPLAY | T07-11 |
| ③ `auth_seq = 0` / 回退 → REPLAY | T07-12、T07-13 |
| ④ 伪造桥板签名 → IDENTITY_REJECTED 且不发 hello_finish | T07-17 |
| ⑤ boot_id4 与 boot_id 不符 → IDENTITY_REJECTED | T07-18 |
| ⑥ 过期事件 → EVENT_EXPIRED 不执行业务 | T07-24 |

## 3. 点亮用的骨架（复制到 `pi_terminal/tests/pbr_session_test.cpp`）

```cpp
// T-07 验收测试。实现落地后：
//   1. 复制本文件到 pi_terminal/tests/pbr_session_test.cpp
//   2. 在 CMakeLists.txt 的 BUILD_TESTING 段加入下面三行
//   3. 删掉本节的注释标记
//
//   add_executable(pbr_session_test tests/pbr_session_test.cpp)
//   target_link_libraries(pbr_session_test PRIVATE terminal_protocol terminal_security Qt6::Test)
//   add_test(NAME pbr_session PRIVATE pbr_session_test)

#include <QtTest>
#include "protocol/PbrSession.h"     // T-07 交付物
#include "protocol/ReplayGuard.h"
#include "protocol/EventAckManager.h"

class PbrSessionTests : public QObject {
    Q_OBJECT
private slots:
    // 每条用例一个 slot，命名与本文表格一一对应，便于对照。
    void t07_01_kindNotInAad()        { QSKIP("T-07 未实现"); }
    void t07_02_dirNotInAad()         { QSKIP("T-07 未实现"); }
    void t07_03_authSeqNotInAad()     { QSKIP("T-07 未实现"); }
    void t07_04_lengthMismatch()      { QSKIP("T-07 未实现"); }
    void t07_11_replaySameEnvelope()  { QSKIP("T-07 未实现"); }
    void t07_12_authSeqZero()         { QSKIP("T-07 未实现"); }
    void t07_13_authSeqDecreasing()   { QSKIP("T-07 未实现"); }
    void t07_15_tagBeforeAuthSeq()    { QSKIP("T-07 未实现"); }
    void t07_17_forgedBridgeSignature(){ QSKIP("T-07 未实现"); }
    void t07_18_bootIdMismatch()      { QSKIP("T-07 未实现"); }
    void t07_24_eventExpired()        { QSKIP("T-07 未实现"); }
    void t07_29_eventIdDedup()        { QSKIP("T-07 未实现"); }
    void t07_33_retransmitKeepsTtl()  { QSKIP("T-07 未实现"); }
    void t07_37_ephemeralZeroized()   { QSKIP("T-07 未实现"); }
};
QTEST_GUILESS_MAIN(PbrSessionTests)
#include "pbr_session_test.moc"
```

`QSKIP` 而不是空实现：任何一条被漏写都会显示为 skipped 而不是 pass，
不会被误读为"已通过"。

## 4. 与既有测试的边界

- **`HostIdentity` 的 `r‖s` 与 DER 往返**已在 `tests/host_identity_test.cpp` 覆盖（555 条断言，含 r 高位为 1 的分支）。T07-21 只需断言**会话层**确实用 64 字节 `r‖s`，不重复测底层。
- **`PbrControl::validate()` 的字段校验**已在 `tests/protocol_test.cpp` 覆盖（F 交付，39 条断言）。T-07 只补"解密后进入校验器"之前的时序。
- **`TrustStore` 的 key_id 碰撞**已在 `tests/trust_store_test.cpp` 覆盖。T07-23 只断言会话层**逐个验签**的行为。

---

---

## 合并说明（2026-10-29）

本文所在的 PR 已并入 **`feat/p0-software-complete`**，本 PR 可以安全关闭。

- **更新日志与测试详情**：[`docs/devdocs/11-changelog-and-tests.md`](../devdocs/11-changelog-and-tests.md)
- **PR 关闭说明与包含关系验证**：[`docs/devdocs/10-pr-closure-notes.md`](../devdocs/10-pr-closure-notes.md)
- **给 L 的硬件交接清单**：[`docs/devdocs/08-handoff-to-L.md`](../devdocs/08-handoff-to-L.md)
- **调试钩子清单**：[`docs/devdocs/09-debug-hooks.md`](../devdocs/09-debug-hooks.md)

> 关闭本 PR 不代表验收通过。P0 完成条件见
> [`docs/devdocs/04-dev-tasks.md` §7](../devdocs/04-dev-tasks.md)，
> 目前六层证据只完成前两层（源码单测、主机模拟），
> 目标 Pi 构建 / Pi 实机 / ESP 板实机 / 环境实测均为零。
