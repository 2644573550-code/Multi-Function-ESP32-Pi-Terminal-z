# F 第二轮审查：PR #28 复核 + D 后续开发任务书

| 项 | 内容 |
|---|---|
| 审查人 | Mr.F（用户授权，2026-10-28） |
| 被审对象 | PR #28 `SomaRia-Meta/docs/d-pbr-secure-v1-decisions`（已合入 main，`67ed9ae`）＋本人此前起草的 `pbr-secure-v1.md` 修订 1 |
| 结论 | **PR #28 内容通过，但其中的方案有 1 处协议级错误、5 处会导致两侧实现分叉的歧义。修订 1 规范已改到修订 2，本文件同时是 D 的完整开发任务书。** |
| 分工作废 | ESP 固件归 **L**。D 只做 Pi 侧软件 + 规范 + 向量 + 一致性工具，不碰 ESP 仓库。 |
| 本文件效力 | **D 按本文件即可完成 T-03/T-05/T-07/T-08 的软件部分，无需再向 F 确认。** 有异议按 §5 提，不要静默偏离。 |

---

## 1. PR #28 复核结论

**通过。** 9 条现状事实逐条回到源码核对，全部成立；代码地图修复后自洽；8 个文件的过时陈述修正属实；14 条决议覆盖了 f-day1 未定义的缺口，DR-10（Pi 身份）和 DR-12（诊断通道边界）尤其到位。

| 项 | 判定 |
|---|---|
| 现状事实 1～9 | 全部成立（含 `main.c:196` 的 kind!=2 守卫、`rfid_service.c:44-50` 明文 UID、Windows-only host 测试） |
| 代码地图生成器 | 缺 `module-devkit`/`projects`/`hardware` 三个 ROOT，修复前表头 1732、分组合计只有 1257（已用旧脚本在 `e72e191` 复现），缺陷成立，修复正确。本 PR 新增 2 个 `.py` 后重新生成为 1736 = 1736，仍自洽 |
| DR-01～DR-09 | 方案可审，六项交付口径接受 |
| DR-10～DR-14 | 缺口识别准确；DR-10 的带外窗口是硬需求，DR-14 的密钥归属切分正确 |
| 流程 | 无 PR 描述、GitHub 未留 review 痕迹。本次已在 §1.1 补记，下次按 `04-dev-tasks.md` §6.2 写全 |

### 1.1 流程缺陷（不追溯，只约束以后）

`574904e` 把 8 个文件的文档检修（含 751 行代码地图）并进了"安全决议"PR。内容都对，但 review 面被撑大 8 倍。后续要求：**一个 PR 一件事**；PR 描述必须含任务 ID、验收标准、测试命令、目标硬件、实机结果五项。

---

## 2. 必须改的 6 处（已全部改完，D 直接按修订 2 实现）

### 2.1 【协议级】修订 1 的握手是**单向认证**，桥板身份从未被证明

修订 1 里只有 Pi 在 `hello_finish` 里签名，桥板从头到尾没有证明过自己是谁。Pi 能确认的只有"对方算得出 ECDH 共享秘密"。

攻击者拿不到会话密钥，确实伪造不出 `hello_ok`。但它可以**冒用任意一块真桥板**：复制该板的 `bridge_key_id`（1 字节）、用自己的临时密钥、自己的 `boot_id4`。Pi 无从分辨这是哪块板，因为 Pi 唯一的身份依据就是那个 1 字节——而 §6.2 自己写了"1 字节只用于选钥，不作身份判定"。

后果：`IT-003`（未签发桥身份被拒绝）根本无法成立；DR-03/DR-10 承诺的"双向认证"只有一半。

**裁决（已写入修订 2 §3.3）**：`hello_resp` 增加 `sig` 字段，桥板用身份私钥对 `T2 = tag(hello_req)||msg1 || tag(hello_resp)||msg2` 签名。Pi 用 TrustStore 里的完整公钥验签，失败即断连。

- 桥板签名对象不含 `msg3`（它的时间点在 `hello_resp` 之后，签不进去）。
- 签名不进 `T2` 本身，否则自引用。
- Pi 必须先验桥板签名，再发 `hello_finish`。顺序反了等于把 Pi 的身份公钥先送出去。

### 2.2 【会导致必分叉】`session_id` 一名两用

修订 1 里 `session_id = transcript_hash[0..4)`（4 字节，喂 nonce），而 f-day1 §4.2 要求事件里的 `session_id` 是 **32 位 hex**（16 字节）。同一个名字、两种宽度、两种来源。两侧实现必然各取一个。

**裁决（修订 2 §4）**：
- `nonce_prefix = transcript_hash[16..20)`（4 字节），只用于 nonce。
- `session_id = transcript_hash[0..16)`（16 字节 = 32 hex），只用于事件字段与去重键。
- 两者都从同一个 `transcript_hash` 切，可由向量独立验证。

### 2.3 【会导致两侧字段不一致】`key_id` 三种宽度

DR-02/03/05 各写一种（1 字节 / 8 字节 / "1～64 字符"）。修订 2 §2.1 定死：线上 1 字节选钥、JSON 与事件字段 2 位小写 hex、TrustStore 用完整公钥 SHA-256 前 8 字节。**1 字节永不作身份判定。**

f-day1 §4.2 写的"1～64 字符"是字段类型上界，`pbr-secure-v1` 的取值以 2 位 hex 为准。

### 2.4 【矛盾】`auth_seq` 上限自相冲突

修订 1 正文说"到 2⁶⁴−1 强制重握手"，同文件 §6.2 的拒绝规则写"拒绝 ≥2⁶³ 的回退"。两条同时实现不了。

**裁决（修订 2 §5.2）**：`auth_seq` 每方向从 1 起，严格递增；`auth_seq == 0` 或 `<= last` 一律 `REPLAY`；到 `2⁶⁴−1` 前主动重握手，禁止环绕。因为禁止环绕，不需要滑动窗口。

### 2.5 【不可实现】TTL 没有时间基准

f-day1 §4.2 要求"用握手建立并验证的对端单调时间偏移校验 TTL"，但修订 1 的握手不带任何时间字段。T-07 拿不到 `offset`，只能把"刚收到"当"刚发生"——那正是 f-day1 明令禁止的。

**裁决（修订 2 §3.5）**：`hello_ok` 明文加 `uptime_ms`。Pi 只接受首条 `hello_ok` 的 `uptime_ms` 建立 `offset`，此后不得改写（否则伪造时间戳可延长事件寿命）。年龄 = `event.uptime_ms - (本地单调时间 + offset)`，越界 → `EVENT_EXPIRED`。

### 2.6 【实现前提】载荷是二进制，不是文本

规范要求认证后的 payload 是原始二进制信封，密文里必然出现 `0x00`。

**裁决（修订 2 §5.1）**：**D 在自己的实现里必须按长度处理字节**，不能因为对端现在发 JSON 就假设以后也是 JSON；接收侧不得把"含 NUL"当非法。

这条对两端都成立，但**只约束软件侧现在要做到**。远端目前按 C 字符串发送、命令入口检查 NUL——那是现状事实，不是本 PR 要修的东西；等软件完成后再对接。（事实引用：`pbr_send_frame` 只收 `const char *`，命令入口有 `memchr(payload, '\0', length)`。）

---

## 3. 已修的实现错误（在本仓库代码里，非 PR #28）

| # | 位置 | 错误 | 影响 |
|---|---|---|---|
| 1 | `control_event.cpp` | `rfid_card` 被归入 `moduleEvent`，强制要求 `module_id`/`module_instance` | 校验器会拒绝真桥板的 RFID 事件——`rfid_service.c` 根本不发这两个字段 |
| 2 | `control_event.cpp` | `pong`/`bridge_status` 在 `kinds` 里，但 `validate()` 要求 `event_id`/`ttl_ms`，而诊断响应只有 `request_sequence` | 永远校验不过；已移出事件集合，交信封层（修订 2 §6.5） |
| 3 | `control_event.cpp` | `key_id` 用 `label()` 校验，接受 `"key-1"` | 与 §2.1 冲突，已改 `^[0-9a-f]{2}$` |
| 4 | 测试 | 用一个不存在的字段组合当"有效 RFID" | 已改为按桥板真实形状测，并加了大写 UID 拒绝用例 |

第 1 条是**真桥板兼容性缺陷**，不是风格问题：校验器一旦按原样接入 RFID 路径，桥板的每一次刷卡都会被判 `INVALID_MESSAGE`。

**但要说准确**：`rfid_card` 目前在 Pi 侧**根本没有消费者**——`HostEventDetector::valid()` 只放行 `attach`/`detach`，`validate()` 也只被它调用。所以这条缺陷今天是"下一块多米诺骨牌"，不是"线上正在丢卡"。RFID 事件链路的接入属 T-08，届时若不带这个修复，会表现得像"刷卡没反应"，而日志里连一行都不会有（`valid()` 静默返回 false）。

---

## 4. 已验证的证据（可复现）

| 项 | 命令 | 结果 |
|---|---|---|
| 向量稳定性 | `python3 docs/pbr-secure-v1/gen_vectors.py` ×2 | 完全一致 |
| mbedtls 2.28 | `cc -I<mbedtls-2.28>/include mbedtls_check.c -lmbedcrypto` | **7/7 PASS** |
| mbedtls 3.6.5（IDF 5.5.4 源码，主机编译） | 同上，指向 IDF 的 mbedtls | **7/7 PASS** |
| Pi 单元测试 | `ctest -R pibridge_protocol` | 通过（39 条断言，其中 PbrControl 29 条） |
| 全量构建 | `cmake --build` | 本次改动文件 0 警告 |

### 4.1 `mbedtls_check.c` 之前根本不能在 3.6.5 上编译

修订 1 的检查程序直接访问 `ecdh.d` / `ecdh.grp`。mbedtls 3.x 默认把 `mbedtls_ecdh_context` 换成新结构，这些字段只在 `MBEDTLS_ECDH_LEGACY_CONTEXT` 下存在，而 IDF 不定义它。实测：`error: 'mbedtls_ecdh_context' has no member named 'd'`。

所以"目标平台只验证了编译"那句话当时并不成立——它编译都过不去。已改为只用 2.28/3.6 公共 API（`mbedtls_ecp_mul` + `mbedtls_ecp_point_write_binary`），两个版本现在都真跑通。

**顺带发现的第 5 个陷阱**（已写入规范 §8）：mbedtls 3.x 对 `mbedtls_ecp_mul`／`mbedtls_ecdh_compute_shared` 传 `f_rng = NULL` 直接返回 `ECP_BAD_INPUT_DATA`，2.28 容忍。同一份代码在 2.28 能跑、在 IDF 上必失败。**真实实现必须传 CSPRNG**（检查程序里用的是固定种子，只为让向量可复现）。

---

## 5. D 的完整开发任务书（拿到即可开工，不必回问）

### 5.1 边界

| 你做 | 你不做 |
|---|---|
| `docs/pbr-secure-v1/` 下规范、向量、生成器、一致性检查 | ESP 仓库任何改动（一行都不碰） |
| `pi_terminal/src/protocol/`、`src/transport/`、`src/security/` 的 Pi 侧实现 | 固件侧 NVS、ESP 侧 AEAD 状态机、ESP TLS（L） |
| `pi_terminal/tests/` 的单元与主机级测试 | 实机验收（无硬件，一律 `NOT_RUN`） |
| 主机侧模拟对端（自测用） | 等固件改完才开工（顺序颠倒会让实现被固件现状牵着走） |
| 提 PR、写描述、贴命令与结果 | 改 `04-dev-tasks-f-day1.md`（F 的契约） |

### 5.2 每个任务的交付物与验收

**T-03（身份与密钥边界）**

- `pi_terminal/src/security/TrustStore.{h,cpp}`：`lookupIdentity` / `revokeIdentity` / `enrollIdentity`；索引键 = 完整公钥 SHA-256 前 8 字节；存完整 65 字节公钥。
- `pi_terminal/src/security/HostIdentity.{h,cpp}`：首次启动生成 ECDSA P-256 密钥对，0600，路径可注入（测试用临时目录）。**注释必须写"仅测试级，不构成生产保障"**，与规范 §2 同措辞。
- 验收：两把不同密钥的 `key_id` 冲突时（1/256 概率可构造），`lookupIdentity` 必须返回多条并由调用方逐个验签，不能取第一条。

**T-05（PBR 编解码与事件状态机）**

- 本仓库的 `PbrControl::validate()` / `payloadAcceptable()` 已由 F 写好，**你只需维护与扩展**，不要重写。
- 待补：`slot_id` 与 `binding_generation` 只在 `attach`/`status`/`detach` 上校验（已实现）；双槽路由与 `ConnectionManager` 的槽位复用用例属 T-05，按任务卡补测试。
- 验收：`ctest -R pibridge_protocol` 通过；新增用例必须能独立失败（故意改坏一行要挂）。

**T-07（AEAD、重放、ACK、去重）** ← 主要工作量

- `pi_terminal/src/protocol/PbrSession.{h,cpp}`：按修订 2 §3～§6 实现。必须有：
  - 主动方（Pi）与被叫方（桥板）两套构造参数，别写成一个类里 `if (isInitiator)`；两侧状态机不对称，混在一起必然出错。
  - `verifyBridgeSignature()` 先于 `sendHelloFinish()`。
  - `auth_seq` 每方向独立、严格递增、`0` 与回退都算 `REPLAY`。
  - `hello_ok.uptime_ms` 只接受一次。
  - 临时私钥在握手结束与失败路径都要清零（`mbedtls_platform_zeroize` 或等效）。
- `pi_terminal/src/protocol/ReplayGuard.{h,cpp}`：`auth_seq` + `event_id` 双轨；`event_id` 记录保留到会话结束，上限 4096，满则重建会话而不是提前遗忘。
- `pi_terminal/src/protocol/EventAckManager.{h,cpp}`：500ms 等 ACK，最多重传 3 次，不超 TTL，重传不刷新 TTL；队列满回 `BUSY`，**不得先 ACK 后丢弃**。
- 验收（每条都要有测试）：
  1. 改 `kind`/`dir`/`auth_seq`/`len` 任一 AAD 字段 → tag 失败。
  2. 同一信封投两次 → 第二次 `REPLAY`。
  3. `auth_seq = 0` → `REPLAY`；`auth_seq` 比上次小 → `REPLAY`。
  4. 伪造桥板签名 → `IDENTITY_REJECTED`，且不发 `hello_finish`。
  5. `hello_ok` 的 `boot_id4` 与事件 `boot_id` 前 8 字符不符 → `IDENTITY_REJECTED`。
  6. 过期事件（`age > 3000`）→ `EVENT_EXPIRED`，不执行业务。

**T-08（RC522 到 Pi 事件闭环）**

- `rfid_card` 经信封解密 → `validate()` → UID 索引派生 → 选用户。
- `deriveUidIndex()` **签名由本节冻结（DR-14 兑现）**，你只实现与调用，密钥生命周期归 Z：
  ```cpp
  // pbr/uid_index.h —— 本签名冻结，两侧不得各自加参数
  QByteArray deriveUidIndex(const QByteArray &rawUid);
  ```
  | 项 | 冻结值 |
  |---|---|
  | 输入 | **原始 UID 字节**，长度 4 / 7 / 10；不是 hex 文本，不做大小写折叠 |
  | 输出 | 32 字节 |
  | 算法 | `HMAC-SHA256(key, ASCII("p0-rfid-index-v1") + u8(uid_len) + rawUid)`（f-day1 §3 原文） |
  | 密钥 | Z 持有与轮换；D 不接触、不缓存、不落盘 |
  | 失败 | 长度非法返回**空** `QByteArray`，调用方按 `INVALID_MESSAGE` 处理；不得返回全零或截断值 |
  | 密钥入口 | **只有一处**：`deriveUidIndex()` 内部调 `uidIndexKey()` 取密钥，Z 不实现任何 HMAC，D 不接触密钥字节。`uidIndexKey()` 返回 false 时 `deriveUidIndex()` 直接返回空 |

  **密钥接口由 Z 提供**，签名冻结如下（Z 实现，D 只调用）：
  ```cpp
  // secure/uid_index_key.h —— Z 实现
  bool uidIndexKey(QByteArray *out32);   // 成功返回 true；密钥不可用时返回 false
  ```
  D 的职责：拿到 `deriveUidIndex` 的输入、调用它、把结果交给用户匹配。**密钥缺失时整条 RFID 路径必须失败关闭**（不匹配任何用户，不退回明文 UID 比较）。
- 验收：raw UID 不出现在日志、DB、QSettings；UID hex 大小写都接受、长度与字符集必须校验（已有用例）。

**T-12（模块业务数据安全传输）——不在本 PR 范围，列在此处只为划清边界**

- Pi 侧：TLS ≥1.2，指纹白名单（profile 为准、QSettings 为缓存），撤销立即断连并销毁派生密钥。
- **ESP 侧 TLS 不在你的范围**，你只做 Pi 侧对端与接口约定。固件侧等软件完成后由 L 对接。
- 验收：`TLS 会话 ≠ 设备已认证` 写进代码注释与文档；撤销后下一次连接必须失败。

### 5.2.1 第二轮问答已裁决的四项（2026-10-28）

| 问题 | 裁决 | 落点 |
|---|---|---|
| 已配对后再连接如何认定 Pi 合法 | **验签通过 且 `msg3_body` 公钥与 NVS 逐字节相等**，任一不满足 `IDENTITY_REJECTED` | 规范 §6.7 |
| NVS 已有记录时另一台 Pi 要求配对 | **直接拒绝，不覆盖**；换机须管理员显式 `revokeIdentity` 后重配 | 规范 §6.7 |
| 是否为固件未就绪留降级路径 | **不留。** 四项未完成时握手就是会失败，禁止加明文 fallback | 规范 §0 |
| UID hex 大小写 | **都接受**，校验时折叠；`deriveUidIndex` 收原始字节 | 规范 §6.4 |

第一条之所以重要：撤销配对若只在"删记录"层面生效，已连上的旧 Pi 不会被踢掉，直到它主动重连。规范现在要求撤销**立即断开并销毁会话密钥**。

### 5.3 不需要问 F 的事（已预先裁决）

| 你可能想确认的 | 直接按此做 |
|---|---|
| `r‖s` 还是 DER | 一律 `r‖s` 64 字节。两侧各加一条 `r` 最高位为 1 的用例 |
| `auth_seq` 能不能环绕 | 不能。到上限主动重握手 |
| `hello_ok` 丢了要不要重传 | 不重传。Pi 侧超时即失败重连 |
| 握手失败后能不能复用临时密钥 | 不能。必须新生成 |
| `event_id` 去重表满了 | 重建会话，不要提前遗忘 |
| 诊断 `pong`/`bridge_status` 怎么处理 | 不进事件校验器，信封层按 `request_sequence` 单独路由 |
| `schema` 缺失的 `attach`/`detach` | 保持 `Legacy` 接受，**T-07 合入时删除该分支及规范 §6.6** |
| Pi 私钥放哪 | `~/.config/.../host_identity.pem`（或等价），0600，注释标测试级 |
| 发现规范写错了怎么办 | 提 PR 改规范 + 向量 + 检查程序三件套，F 审。**不要只改代码不改规范** |

### 5.3.1 D 的任务卡点一览（2026-10-28 核定）

你被分配 10 个任务。下表按**能不能现在开工**分组。`阻塞` 一列写的是真实前置，不是提醒。

#### A. 现在就能做完（无外部前置）

| 任务 | 做什么 | 卡点 | 你能独立完成的部分 |
|---|---|---|---|
| **T-05** | PBR 编解码、事件状态机 | **无。** 本轮已交付 `PbrControl::validate()` + `payloadAcceptable()` | 补齐双槽路由与 `ConnectionManager` 槽位复用用例；用 `pi_terminal/tools/mock_bridge.py` 做主机自测（注意路径：**不在** `tools/` 下） |
| **T-03** | 身份与密钥边界 | **无。** 规范 §2/§3 已定 | `TrustStore`、`HostIdentity` 全部可写。见下方"已存在可复用" |
| **T-02** | Pi/Qt/USB 串口基线 | **无。** 只做 Pi 侧 | Pi 侧 SDK 来源清单 + 串口基线；ESP 侧清单等 L |

#### B. 有前置，但前置是**你自己**

| 任务 | 做什么 | 卡点 | 顺序 |
|---|---|---|---|
| **T-07** | AEAD、重放、ACK、去重 | 依赖 T-03 的 `TrustStore`/`HostIdentity` + T-05 | **先 T-03 再 T-07。** `PbrSession` 需要 TrustStore 提供完整公钥才能验桥板签名 |
| **T-08** | RC522 → Pi 事件闭环 | 依赖 T-07（解密）+ **Z 的 `uidIndexKey`** | 先做前半段（解密 → `validate()` → 调 `deriveUidIndex`），密钥接口用 stub 测；Z 交付后切真实现 |

#### C. 有外部前置，**现在做不了**

| 任务 | 阻塞于 | 具体卡在哪 | 现在能做的 |
|---|---|---|---|
| **T-12** | Z 的 T-04（会话/权限） | Pi 侧 TLS 对端要挂到用户会话与授权上；T-04 未实现前只能写传输层 | 写 TLS 客户端 + 指纹白名单 + `撤销立即断连`，用假会话测 |
| **T-13** | L 的固件、Z 的页面 | 你只做 Pi 侧接口与字段定义 | 定义事件字段与接入接口，不写固件、不写页面 |
| **T-14** | L 的固件（啸天 `firmware/` 尚无实现） | 同上 | 同上：字段与接口 |
| **T-15** | T-08/T-09/T-10/T-11/T-12/T-13/T-14 **全部** | 实机联调，需要 L 的硬件与固件、Z 的页面 | 只做 Pi 侧路由与状态机，实机验收由 F 判 |
| **T-16** | T-15 | 同上 | 同上 |

#### D. 需要别人先交的东西（清单）

| 需要什么 | 谁给 | 影响哪些任务 | 没有它时的替代 |
|---|---|---|---|
| `uidIndexKey()` 实现 | **Z** | T-08 | 用固定测试密钥的 stub，接口不变 |
| T-04 会话与用户模型 | **Z** | T-12、T-15 | 用假会话对象，不给 QML 暴露 |
| 固件具备：命令容纳 329B / 承载二进制 / 桥板签名 / `hello_ok.uptime_ms` | **L**（等软件完成后） | T-07 联调、T-13/T-14/T-15/T-16 | 主机侧模拟对端自测；**不得**因此加明文 fallback |
| 双槽电气核验（T-06） | **L** | T-14、T-16 | 无替代，等 |

#### E. 已经存在、可以直接用的

避免重复造：

| 现有物 | 位置 | 说明 |
|---|---|---|
| 事件校验器 | `pi_terminal/src/protocol/control_event.{h,cpp}` | 本轮交付，直接扩展 |
| PBR 帧编解码 | `pi_terminal/src/protocol/pibridge_protocol.{h,cpp}` | 成熟，不改外层 |
| 主机侧假桥板 | `pi_terminal/tools/mock_bridge.py` | 自测用；**路径不在 `tools/`** |
| 信任确认 UI 与 `trustKey` | `pi_terminal/src/core/ConnectionManager.{h,cpp}:430-455` | **已有**一套 `QSettings("trusted/"+trustKey)` 的模块信任流程，但那是**模块包签名**，不是桥板身份。T-03 的 `TrustStore` 是另一套，不要混用，也不要改现有的 |
| 串口传输 | `pi_terminal/src/transport/PogoControlTransport.cpp:87` | 已接好 `payloadAcceptable()`；T-07 接入 AEAD 时必须先解信封再走这一步 |

#### F. 一句话版

**现在能开工：T-02、T-03、T-05。**
**接着做：T-07（依赖 T-03）、T-08 前半段（依赖 T-07）。**
**等 Z：T-08 后半段、T-12。**
**等 L：T-13/T-14/T-15/T-16 的实机部分——这几项你现在只做 Pi 侧接口与字段，不要等固件，也不要替固件做假设。**

### 5.3.2 Z 的任务书与卡点（2026-10-28 核定）

与 §5.3.1 对称。Z 拿到本节即可开工，不必回问。详细清单见 `docs/devdocs/05-mr-z-todo.md`，本节只定**接口与参数**。

#### A. 七项冻结参数（Z 按此实现，不得自行改）

| 编号 | 项 | 冻结值 |
|---|---|---|
| Z-1 | T-10 存储 | **直接调 SQLCipher C API**（`sqlite3_open` + `sqlite3_key`），**不用 `QSqlDatabase`**。理由：Qt 6.5.3 SDK 无 SQLCipher 驱动、无 `qsql_sqlite_p.h`、本机无 autoconf，重编 QSQLITE 不可行 |
| Z-2 | PIN 的 KDF | **PBKDF2-HMAC-SHA256，迭代 ≥600,000**，salt 16B 随机，输出 32B，用 `QPasswordDigestor::deriveKeyPbkdf2`。硬编码下限，不得由配置降低 |
| Z-3 | 依赖解耦 | **T-04 不再依赖 T-03**（PIN 认证与桥板身份无代码交集）；**T-09 拆 a/b**：`T-09a` PIN/锁屏/切用户 = T-04+T-10，`T-09b` 绑卡/挂失 = T-07+T-08+T-09a。仍是一张卡，不新增编号 |
| Z-4 | worker 隔离 | systemd `DynamicUser=yes` + `PrivateNetwork=yes`，不建持久账号；IT-013 按 **worker PID 的 UID ≠ 宿主 UID** 判定，不要求固定值 |
| Z-5 | BLE | **保留，两层都要**（2026-10-28 定稿）：① 链路层 `pairingStatus==Paired`（Just Works 可接受）；② **应用层挑战-响应** `HMAC-SHA256(k, challenge‖module_id‖boot_id)`，对称密钥 32B。未通过前不接受业务数据。单条业务 payload ≤512B，分片由 `BleTransport` 内部做。实现归 **D**，Z 只做会话门控 |
| Z-6 | `uidIndexKey` 位置 | **整机单钥**，`AppConfigLocation/uid_index.key`，0600（**不是** `AppLocalDataLocation`——那是 `ModuleManager::dataDirectory()` 交给模块的目录）。理由：刷卡的用途是选用户，发生在登录之前；放每用户 DB 会鸡生蛋 |
| Z-7 | 模块 QML 隔离 | 模块页改**独立 `QQmlEngine` + 独立 `QQmlContext`**，只注入 `sessionModel`/只读 `userContext`/主题。理由：现在 `Main.qml:667` 用同一引擎，模块页能直读 `connectionManager`，T-13 的"QML 只见选择结果"无法验收 |

#### B. 现在就能做完（无外部前置）

| 任务 | 你能独立完成的部分 | 卡点 |
|---|---|---|
| **D1** `uidIndexKey()` | 生成/存储/轮换/失败关闭 + 单测 | **无。** 位置已定（Z-6）。**这是 D 的唯一硬阻塞，优先交付** |
| **T-04** 用户/PIN/会话 | 全部服务 + UT-002 | **无。** 已解耦（Z-3），不必等 D 的 T-03 |
| **T-10** 每用户 SQLCipher | `SqlCipherStore`/`UserDatabase` + UT-005 | **无。** 本机可跑（Z-1，已验证解包方式） |
| **T-11** worker 隔离 | `ModuleWorkerLauncher` + IPC + unit 文件 | 本机可写；真 UID/拒网需 Pi |
| **T-09a** PIN/锁屏/切用户 + 配对页面 | 全部 QML + 接口 | **无。** 已解耦（Z-3）。配对页面归 Z、后端归 D 的 `TrustStore` |

#### C. 有前置

| 任务 | 阻塞于 | 现在能做的 |
|---|---|---|
| **T-09b** 绑卡/挂失 | D 的 T-07/T-08 | 先写 UI 与错误分支，用 stub |
| **T-12** TLS/BLE | T-04（已完成则无阻塞）；BLE 实机需真模块 | Pi 侧 TLS + BLE 门控全部可写 |
| **T-13** 用户归属 | Z-7 的引擎隔离（自己做） | 先做引擎隔离，再做归属 |
| **T-14/T-15/T-16** | L 的固件、实机 | 只做 Pi 侧页面与路由 |

#### D. 需要别人给的东西

| 需要什么 | 谁给 | 影响 |
|---|---|---|
| `TrustStore` 后端接口 | **D**（T-03） | T-09a 的配对页面；先按 §5.3.1 的签名写 UI |
| `deriveUidIndex()` 实现 | **D**（T-08） | T-09b；你只提供密钥 |
| ESP 侧 BLE 配对 / TLS | **L**（等软件完成） | 实机联调，不是设计前置 |
| 真 RC522、真 BLE 模块、Pi 3B | **L / 硬件** | 全部 ❓ 项 |

#### D2. BLE 与 LAN 的实现分工（2026-10-28 定稿）

P0 要**同时实现**两条业务 lane。参数已冻结，见 `05-mr-z-todo.md` §0.8。

> **依据**：`TECHNICAL-ADVISORY-REPORT.docx` v4.1 已同步修订（原第 9.3 节写「BLE profile 禁用并测拒绝」，与第 558 行「BLE 安全配对与应用认证」矛盾；2026-10-28 按 F 裁决统一为「BLE 要做」，第 558 行为准）。

| 项 | 定值 | 归属 |
|---|---|---|
| LAN 方向 | 模块 `listen`、Pi `connect`（现状，不改） | L（server）/ D（client） |
| LAN 证书 | 模块**自签**，Pi 固定证书 **SHA-256 指纹**，首次登记 | L 生成 / D 校验 / Z 会话 |
| LAN 失败 | 无明文回退 | D |
| BLE 链路层 | `pairingStatus==Paired`，Just Works 可接受 | D |
| BLE 应用层 | 挑战-响应 `HMAC-SHA256(k, challenge‖module_id‖boot_id)` | D 协议 / L 固件 / Z 门控 |
| 模块密钥 | 32B，**配对时经 LAN 下发写 NVS** | **T-03（D）** + L 实现 NVS |
| BLE 单帧 | ≤512B，超出 `INVALID_MESSAGE` | D |
| lane 热切换 | 允许，**必须重新认证**，最多 3 次 | Z 状态机 / D 传输 |
| `fixed_ble`+`hotspot_lan` | **合法**（业务走 BLE、包走 LAN） | 保持现状不改 |

**已验证（2026-10-28，本机）**：`QSslServer` + 自签证书 + 客户端 `VerifyNone` + 握手后比指纹可跑通（`pinned_match=1`、`isSelfSigned=1`）；`QSslSocket::supportsSsl()=1`，`OpenSSL 3.0.2`，TLS 1.2/1.3 均支持。

**关键约束**：ESP 固件现在**零处** NVS 密钥存储、BLE 无 `sm_io_cap`/`BLE_SM_*`、LAN 是裸明文 TCP。T-12 的 ESP 侧证据必须等 L，**Pi 侧证据不依赖 L**。

#### E. 一句话版

**Z 现在能做完 5 件事：`uidIndexKey`、T-04、T-10、T-11、T-09a。**
**做完前三件，D 的 T-08 后半段和 T-12 就解除了阻塞。**
**T-09b 和 T-13~T-16 的实机部分等 D 与 L，但 Pi 侧代码现在就能写。**

### 5.4 提 PR 的硬要求

1. 描述必须含：任务 ID、验收标准、测试命令、目标硬件、实机结果五项。
2. 无硬件结论一律 `NOT_RUN`，**不得**写进任何验收表。
3. 一个 PR 一件事。
4. 涉及字节布局的改动，必须同时附 `gen_vectors.py` 与 `vectors.json` 的 diff，并贴 `mbedtls_check` 在两个版本上的输出。
5. 跨仓依赖写清楚，例如"本 PR 假设 L 已实现二进制发送入口"。

---

## 6. 当前状态与遗留

### 6.1 分工边界（2026-10-28 确认）

**D 和 Z 只做软件，不涉及固件。L 等软件完成后才开始固件工作。**

本 PR 没有任何固件改动（23 个文件全在 `docs/` 与 `pi_terminal/`）。固件侧的待办**不由本 PR 派发**，也不作为本 PR 的合并条件。

### 6.2 软件侧当前状态

| 项 | 状态 |
|---|---|
| 规范 / 向量 / 一致性检查 | 完成，mbedtls 2.28 与 3.6.5 各 7/7 |
| T-05 Pi 侧事件校验 | 完成，39 条断言（PbrControl 相关 29 条） |
| `PbrSession` / `ReplayGuard` / `EventAckManager` | **未实现**，属 T-07，D 的任务 |
| `TrustStore` / `HostIdentity` | **未实现**，属 T-03，D 的任务 |
| `deriveUidIndex` 调用点 | **未实现**，属 T-08，D 实现、Z 管密钥 |

### 6.3 软件完成时必然暴露的固件缺口

这些是**行为要求**，不是派活。软件按完整规范实现后，若固件未具备下列能力，握手会失败——这是预期结果，用来证明软件侧做对了：

| 固件需要具备的行为 | 软件侧的预期观察 |
|---|---|
| 命令方向能容纳 329 字节（`hello_finish` 实测值） | 否则停在第 2 步，无错误帧 |
| 命令路径跳过"含 NUL 即非法"的判断 | 否则密文帧被当坏帧丢弃 |
| 桥板能对 `T2` 签名 | 否则 Pi 在 `hello_resp` 之后即断连 |
| `hello_ok` 带 `uptime_ms` | 否则 Pi 拒绝建立 TTL 基准 |

**这四项不需要固件现在就做。** 软件侧先把实现做完、测试写全，用主机侧模拟对端验证；固件跟上时再联调。顺序颠倒（先改固件）会让软件的实现被固件现状牵着走，正是要避免的。

### 6.4 其他遗留

| 项 | 归属 | 说明 |
|---|---|---|
| B-07 token 是否在役 | 用户 | 未确认；本轮不动 Git 历史。不阻塞软件 |
| 实机：Pi 3B / ESP32-S3 / RC522 / ARM64 | 全员 | 全部 `NOT_RUN` |

---

## 7. 本次审查的自我边界

**已验证**：向量在 Python 与 mbedtls 两个版本上一致；Pi 侧校验器 39 条断言；全量构建 0 新增警告。

**未验证，不得声称**：Pi 3B 实机、ESP32-S3 实机、串口时序、RC522、ARM64 交叉编译、`PbrSession` 本身（尚未实现）、`payloadAcceptable` 的模糊测试。

**第三轮发现（本轮修复）**：`rc522.c:256` 用 `"0123456789ABCDEF"` 输出**大写** UID，而我上一轮把校验器收紧成只收小写 hex。后果与 `rfid_card` 字段问题同类——真桥板的刷卡事件会被全部丢掉。已改为两种都收、校验时折叠，并加了 `A1B2C3D4` 必须通过的断言。**教训：两次都是从"我认为桥板发什么"出发，而不是从 `rfid_service.c` 的实际输出出发。**

**我仍不确定的**：`payloadAcceptable()` 是手写扫描器，本轮已修掉 `\u0061` 绕过，但代理对（`\uD83D\uDE00`）、`\/`、深度计数在字符串内的边界仍未穷举。T-07 若把解密后的解析统一走它，建议直接换严格解析器，别继续加补丁。
