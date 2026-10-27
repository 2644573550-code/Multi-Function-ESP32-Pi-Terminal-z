# pbr-secure-v1 协议规范（修订 2）

| 项 | 内容 |
|---|---|
| 版本 | `pbr-secure-v1`，`proto_ver = 1`，`env_ver = 1`，本文件修订 2 |
| 状态 | **F 起草并裁决；D 技术签认后即由 D 拥有维护权，F 只审核变更。** |
| 上游 | `docs/devdocs/04-dev-tasks-f-day1.md` §4；`docs/pbr-secure-v1/00-decisions-for-f.md` §6；`F-REVIEW-2.md` |
| 向量 | `vectors.json`，由 `gen_vectors.py` 生成，可重跑 |
| 实现基线 | ESP-IDF 5.5.4（mbedtls 3.6.5）；Pi Qt 6.5.3 + libmbedtls |
| 修订 2 变更 | 握手加**桥板签名**（修订 1 是单向认证，见 §3.3）；`session_id` 与 `nonce_prefix` 拆分；`auth_seq` 单调规则按实际字段重写；`key_id` 统一 2 位 hex；诊断响应移出本规范 |

本文只定义字节与语义。不做的事：不新增 PBR1 帧头字段、不改 CRC16 的检错定位、不在认证前接受任何业务事件。

---

## 0. 范围与分工

**本文件只约束 Pi 终端软件。** ESP 固件不在本文件范围内，也不由本文件派活。

| 角色 | 范围 | 与本文的关系 |
|---|---|---|
| **D** | 本规范、向量、一致性工具、Pi 侧全部实现 | 签认后拥有本文；字节布局变更由 D 提、F 审 |
| **Z** | 用户 / PIN / 会话 / SQLCipher / worker 隔离 | 消费本文的身份与事件字段，不实现协议 |
| **L** | ESP 固件 | **等软件侧完成后再对接**。本文只写"桥板必须满足的行为"，不写固件实现方案，也不引用固件内部符号 |

**软件不为固件现状让步。** D 按本文件完整实现。固件侧尚未具备相应能力时，握手**就是会失败**——这是预期状态，不是软件的缺陷。失败必须可见、可诊断。

**禁止为"暂时连不上"加明文 fallback。** 那正是 f-day1 §4.1 明令禁止的降级。软件侧唯一的合法行为是：按规范实现，连不上就报错。

**固件侧需要满足什么，由 L 在软件完成后自行对照本文推导。** 本文在行为层面写清了要求（二进制载荷、桥板签名、单调时间基准、命令体积），够 L 开工，不需要本文替他设计实现。

---

## 1. 算法套件

| 用途 | 算法 | 参数 |
|---|---|---|
| 会话密钥交换 | X25519 (RFC 7748) | 双方各生成临时密钥对，每次连接销毁 |
| KDF | HKDF-SHA256 (RFC 5869) | `info = "pbr-secure-v1"`，输出 96 字节 |
| AEAD | ChaCha20-Poly1305 (RFC 8439) | 12 字节 nonce，16 字节 tag |
| 身份签名 | ECDSA P-256 + SHA-256 | 签名编码为固定的 `r‖s` 64 字节 |
| 哈希 | SHA-256 | transcript、`key_id`、`session_id` |

**为什么不是 Ed25519**：ESP-IDF 5.5.4 的 mbedtls 3.6.5 经典 API 不含 Ed25519（只有 PSA 层）；Qt 也不提供。ECDSA P-256 两端都有现成实现。

**必须显式打开的编译开关**（均 `default n`，且**存在依赖关系**）：

```text
CONFIG_MBEDTLS_CHACHA20_C=y        # 前置，否则下一行被静默丢弃
CONFIG_MBEDTLS_POLY1305_C=y        # 前置
CONFIG_MBEDTLS_CHACHAPOLY_C=y      # 依赖上面两个
CONFIG_MBEDTLS_HKDF_C=y
```

**实测教训**：只写 `CONFIG_MBEDTLS_CHACHAPOLY_C=y` 时，Espressif 的 Kconfig 会**静默丢弃**该项（不报错，`sdkconfig` 里根本不出现），固件照样编译成功，运行时 AEAD 不可用。必须四个一起写。

X25519 走 `MBEDTLS_ECP_DP_CURVE25519_ENABLED` 与 `MBEDTLS_ECDH_C`；ECDSA P-256 走 `MBEDTLS_ECDSA_C` + `MBEDTLS_ECP_DP_SECP256R1_ENABLED`（均默认 y）。

**禁止协商降级**：`hello_req` 携带 `proto_ver` 与 `suite_id`，只接受本文定义的唯一组合。收到其他值回 `UNSUPPORTED_VERSION` 并断连，不尝试回退到明文或不加密路径。

---

## 2. 身份与密钥角色

| 角色 | 载体 | 生命周期 | 用途 |
|---|---|---|---|
| issuer | ECDSA P-256 私钥 | 仅在签发环境 | 签发桥板测试身份 |
| 桥板身份 | ECDSA P-256 密钥对 + `bridge_id` | 逐板，NVS | 证明桥板是谁（**签名，§3.3**） |
| Pi 身份 (`host_id`) | ECDSA P-256 密钥对 | Pi 首次启动生成，0600 文件 | 证明 Pi 是谁（**签名，§3.4**） |
| PBR 会话密钥 | X25519 临时密钥派生 | 每次连接 | 仅 PBR 双向 AEAD |
| TLS 传输密钥 | TLS 握手派生 | 每次连接 | 仅业务面 LAN |
| session credential | `CONFIG_APP_PI_DATA_TOKEN` | 产品配置 | 业务会话，**不是身份** |
| UID 索引密钥 | 本机秘密 + HMAC-SHA256 | Z 管理 | UID 索引派生 |

**私钥存储定级**：P0 阶段桥板私钥存 NVS 明文、Pi 私钥存 0600 文件，**均为测试级，不构成生产保障**。NVS 加密 / eFuse 直存列 P1。文档与代码注释必须使用同一措辞。

**Pi 私钥丢失没有恢复路径**（P0）。重新配对 = 桥板 NVS 里那条 Pi 公钥作废重写。

### 2.1 `key_id`

```text
key_id_byte = SHA-256(public_key_uncompressed_point)[0]      // 1 字节，线上传输
key_id_text = "%02x" % key_id_byte                           // 2 字符小写 hex，JSON 与事件字段
fingerprint = SHA-256(public_key_uncompressed_point)[0..8)   // 8 字节，TrustStore 索引
```

- 线上封装里 `key_id` 占 **1 字节**，标识**发送方**的身份公钥。
- JSON／事件字段里 `key_id` 是 **2 位小写 hex 字符串**（`^[0-9a-f]{2}$`）。
- `TrustStore` 存完整公钥（65 字节未压缩点），索引键为完整公钥 SHA-256 前 8 字节。
- **1 字节只用于选钥，不作身份判定。** 身份判定必须比对完整公钥；任何"比 1 字节就认身份"的实现都是错的，碰撞面 1/256。

**TrustStore 记录**（`bridge_id` 不在握手里出现，只能由配对时绑定）：

```text
record = { bridge_id, public_key(65B), fingerprint(8B), enrolled_at, revoked_at }
```

配对时由管理员确认写入 `bridge_id`；此后每个事件的 `bridge_id` 必须与该记录逐字节相等，否则 `IDENTITY_REJECTED`。**不得**从事件自报的 `bridge_id` 反查记录——那等于让攻击者自选身份。

> f-day1 §4.2 把 `key_id` 写成"1～64 字符"，那是字段类型上界；本节的 2 位 hex 是 `pbr-secure-v1` 的唯一取值规则，实施时以本节为准，不得两边各自解释。

---

## 3. 握手

四条消息，方向不对称，因为桥板只接受 `kind=2`：

| 消息 | 方向 | PBR1 `direction` | PBR1 `kind` | 载体 |
|---|---|---|---|---|
| `hello_req` | Pi → 桥板 | 0 | **2** (ControlCommand) | 明文 JSON |
| `hello_resp` | 桥板 → Pi | 1 | **1** (ControlEvent) | 明文 JSON |
| `hello_finish` | Pi → 桥板 | 0 | **2** (ControlCommand) | 明文 JSON |
| `hello_ok` | 桥板 → Pi | 1 | **1** (ControlEvent) | **AEAD 信封**（`k_handshake`） |

桥板 `pbr_poll_control` 硬性丢弃 `rx[4] != 0 || rx[5] != 2`，即它**只接收 Pi 发来的 ControlCommand**；桥板自己只用 `pbr_send_frame(1, ...)` 发 ControlEvent。Pi 侧 `PogoControlTransport::readAvailable` 同样只处理 `direction==1` 的帧。因此握手不能"全部用 kind=1"。

三条明文握手消息的 payload 是 JSON，`schema="pbr-handshake-v1"`：

```json
{"schema":"pbr-handshake-v1","msg":"hello_req","body":"<hex>"}
{"schema":"pbr-handshake-v1","msg":"hello_resp","body":"<hex>","sig":"<hex>"}
{"schema":"pbr-handshake-v1","msg":"hello_finish","body":"<hex>","sig":"<hex>"}
```

`body` 是定长字节的 hex（小写），`sig` 是 64 字节 `r‖s` 的 hex（128 字符）。JSON 只是承载——参与哈希的永远是 `body` 解码后的字节，不是 JSON 文本，所以字段顺序与空格不影响结果。

### 3.1 hello_req（Pi → 桥板）

```text
msg1_body = proto_ver(1) || suite_id(1) || pi_eph_pub(32) || client_nonce(16)    // 50 字节
```

- `suite_id = 1` 对应第 1 节的组合，唯一取值。
- `client_nonce` 用平台 CSPRNG，每次连接新生成。

### 3.2 hello_resp（桥板 → Pi）

```text
msg2_body = proto_ver(1) || suite_id(1) || bridge_eph_pub(32) || server_nonce(16)
          || boot_id4(4) || bridge_key_id(1)                                     // 55 字节
```

- `boot_id4` 是 16 字节 `boot_id` 的**前 4 字节，大端原样**。Pi 在 §3.5 用它核对事件里的 `boot_id`。
- `bridge_key_id` = `SHA-256(桥板身份公钥)[0]`。Pi 用它挑候选公钥，**然后必须用完整公钥验证 §3.3 的签名**，不能凭这 1 字节决定信任。

### 3.3 桥板签名（**修订 2 新增；修订 1 缺失，等于单向认证**）

```text
T2 = tag("pbr_hello_req")  || u16le(50) || msg1_body
   || tag("pbr_hello_resp") || u16le(55) || msg2_body
sig_bridge = ECDSA-P256-SHA256(bridge_identity_priv, T2)      // 64 字节 r‖s
```

`sig_bridge` 放在 `hello_resp` 的 `sig` 字段里，**不参与 `T2`**（否则自引用）。

Pi 的处理顺序（缺一不可）：

1. 用 `bridge_key_id` 在 `TrustStore` 里找候选公钥（可能 0 条或多条）。
2. 对每个候选完整公钥验证 `sig_bridge`。全部失败 → `IDENTITY_REJECTED`，断连。
3. 验证通过后，把该公钥记录为本会话的桥板身份。

**未配对**（TrustStore 无记录）时，只有 §6.7 的配对窗口内允许登记新公钥，并在 UI 上要求显式确认。

> **为什么必须有这一步**：没有 `sig_bridge` 时，Pi 唯一能确认的对端信息是"对方能算出 ECDH 共享秘密"。攻击者拿不到会话密钥确实无法伪造 `hello_ok`，但它可以**冒用任何一块真桥板**（复制其 `bridge_key_id` 一个字节、复用自己的临时密钥），Pi 无从区分是哪块板。那样 `IT-003` 的"未签发桥身份被拒绝"根本无法成立。

### 3.4 hello_finish（Pi → 桥板）

```text
msg3_body = pi_key_id(1) || pi_identity_pub(65)                  // 66 字节
T3        = T2 || tag("pbr_hello_finish") || u16le(66) || msg3_body
sig_pi    = ECDSA-P256-SHA256(pi_identity_priv, T3)              // 64 字节 r‖s，单独发送
```

签名不在 `msg3_body` 里：桥板必须先收到 `msg3_body` 才能算出完整 `T3`，签名本身不能参与 `T3`（否则自引用）。

桥板验签用**自己 NVS 里存的 Pi 公钥**做比对；`msg3_body` 里的 `pi_identity_pub` 只用于首次配对时登记。已配对后必须与 NVS 一致，否则 `IDENTITY_REJECTED`。

签名编码固定 `r‖s` 各 32 字节，**不用 DER**（DER 长度可变，两端解析边界容易不一致）。

### 3.5 hello_ok（桥板 → Pi，加密）

`hello_ok` 是**第一条加密封装帧**，证明桥板真的算出了会话密钥，同时给出 TTL 判龄所需的单调时间基准：

```text
key     = k_handshake        // §4
nonce   = nonce_prefix || u64le(1)
AAD     = env_ver(=1) || key_id(=bridge_key_id) || kind(=1) || dir(=1) || u32le(1) || u16le(len)
plaintext = {"schema":"pbr-handshake-v1","msg":"hello_ok","uptime_ms":<桥板单调毫秒>}
```

Pi 验证：tag 通过 → `msg == "hello_ok"` → `boot_id` 一致性检查：

```text
hex(boot_id4) == boot_id_text[0..8]      // 32 hex 的前 8 字符
```

不一致 → `IDENTITY_REJECTED` 并断连。这一步把 §3.2 的 4 字节值与事件里的 32 hex 绑定，防止两侧对同一个 `boot_id` 各取各的。

**`uptime_ms` 是 TTL 的唯一时间基准**（f-day1 §4.2 要求"用握手建立并验证的对端单调时间偏移校验"，但修订 1 的握手不带任何时间字段，T-07 无法实现）：

```text
offset        = hello_ok.uptime_ms - (Pi 收到 hello_ok 时的本地单调时间)
时间偏移在会话内固定，不随事件更新
事件年龄 age  = event.uptime_ms - (本地当前单调时间 + offset)
age < 0 或 age > 3000 → EVENT_EXPIRED
```

`offset` 只接受一次（首条 `hello_ok`），后续帧不得改写，否则攻击者可借伪造时间戳延长事件寿命。

**`hello_ok` 是唯一使用 `k_handshake` 的帧。** 之后的业务事件用 `k_pi_to_bridge` / `k_bridge_to_pi`。

### 3.6 握手消息的体积

三条明文消息是定长体 + hex，落地成 JSON 后的实际字节数：

| 消息 | 定长体 | JSON 实测 | 说明 |
|---|---|---|---|
| `hello_req` | 50 | 157 | |
| `hello_resp` | 55 + 64 签名 | 305 | 含桥板签名 |
| `hello_finish` | 66 + 64 签名 | **329** | |

**桥板的命令接收路径必须能容纳 329 字节（含 JSON 开销），并以帧头 `length` 为准判断合法性。** 若桥板对命令方向另有小于 329 字节的上限，`hello_finish` 会被当成坏帧丢弃，握手停在第 2 步，且不会有任何错误帧——这类失败最难定位，因为两侧日志都"看起来正常"。

本规范不规定桥板用什么常量，只要求：**任何合法长度的 PBR1 帧（≤2048）都不得被静默丢弃**；确需拒绝时必须回错误帧。

### 3.7 时序、超时与重传

| 阶段 | 谁在等 | 超时 | 重试 | 超时后 |
|---|---|---|---|---|
| `hello_req` | Pi | 500 ms | 最多 5 次 | 判定桥板不可用，关闭串口会话 |
| `hello_finish` | Pi | 500 ms | 最多 5 次 | 同上 |
| `hello_ok` | Pi | 500 ms | **不重传**（桥板侧幂等，见下） | 判定握手失败，关闭会话 |
| `hello_finish` | 桥板 | 5000 ms | — | 丢弃全部握手状态，回到未认证期 |

- 桥板在握手中间态收到**重复的 `hello_req`**（同 `client_nonce`）时，重发相同的 `hello_resp`（含同一份 `sig_bridge`），不重置临时密钥。收到不同 `client_nonce` 视为新会话，丢弃旧状态。
- 桥板在 `hello_finish` 验签通过后生成新 `session_id`，并立即允许 `hello_ok` 的发送。
- 任一侧超时都要销毁临时私钥与已派生密钥。**不得复用同一份临时密钥重试。**

---

## 4. 密钥派生

```text
shared        = X25519(pi_eph_priv, bridge_eph_pub)                // 32 字节
              = X25519(bridge_eph_priv, pi_eph_pub)                // 必须相等
transcript_hash = SHA-256(T3)                                       // 32 字节
session_id    = transcript_hash[0..16)                              // 16 字节 = 32 hex，事件字段用
nonce_prefix  = transcript_hash[16..20)                             // 4 字节，非 nonce 前缀
ks            = HKDF-SHA256(salt = transcript_hash, ikm = shared,
                            info = "pbr-secure-v1", L = 96)
k_handshake    = ks[0..32)
k_pi_to_bridge = ks[32..64)
k_bridge_to_pi = ks[64..96)
```

- **`session_id` 与 `nonce_prefix` 是两个字段，不要混用**（修订 1 用同一个 4 字节值兼作事件里的 32 hex `session_id`，会造成两侧实现必然分叉）。
- 方向密钥**从不重用**。重连即重新生成临时密钥、重新派生。
- 静态身份密钥**只做签名**，不参与 ECDH。前向保密来自临时 X25519。
- `shared` 必须做全零检查：X25519 输出全零表示对方公钥是低阶点，直接断连，不回错误细节。

---

## 5. AEAD 封装

### 5.1 信封

PBR1 帧头不动（`PBR1 | dir | kind | seq_u32le | len_u16le | payload | crc16`）。认证后的 `payload` 是**原始二进制**，不是 JSON、不是 hex：

```text
payload = env_ver(1) || key_id(1) || nonce(12) || ciphertext(N) || tag(16)
```

- `env_ver = 1`。
- `key_id` 是**发送方**身份公钥的 `SHA-256[0]`。
- `ciphertext` 长度 = 明文长度，`tag` 固定 16 字节。
- 明文长度上限：`2048 - 1 - 1 - 12 - 16 = 2018` 字节。超限不截断，直接拒绝。

**认证前不解 JSON**：先验 `env_ver`、`key_id`、tag，全部通过后才解析 payload。

> **载荷是二进制，不是文本。** 信封里必然出现 `0x00` 字节。任何按 C 字符串处理、或以"含 NUL 即非法"为前提的实现，都会把合法密文当成坏帧丢掉。接收侧必须按帧头的 `length` 定长读取，发送侧必须能按长度发送任意字节。这条对两端都成立。

### 5.2 nonce

```text
nonce = nonce_prefix(4) || auth_seq_u64le(8)          // 共 12 字节
```

`auth_seq` 与 `event_id` 是两个独立概念：

| 字段 | 递增时机 | 用途 |
|---|---|---|
| `auth_seq` | **每次发帧都 +1**，包括重传 | nonce 唯一性 |
| `event_id` | 每个**逻辑事件** +1，重传保持不变 | 去重 |

即"重传同一逻辑事件使用新的认证序号/nonce"（f-day1 §4.2）。

- 每个方向各维护一个计数器，**从 1 开始**，覆盖该方向所有密钥（`k_handshake` 与方向密钥共用一个计数）。
- 计数器即将达到 `2^64 - 1` 时必须主动重新握手，**禁止环绕**。
- 接收侧：`auth_seq == 0` 或 `auth_seq <= last_auth_seq` 一律 `REPLAY`。因为禁止环绕，不需要窗口。

### 5.3 AAD

```text
AAD = env_ver(1) || key_id(1) || kind(1) || dir(1)
    || auth_seq_u32le(4) || plaintext_len_u16le(2)                     // 定长 10 字节
```

- `kind` 与 `dir` 取自 **PBR1 帧头**，不是信封。
- `auth_seq` 在 AAD 里只取低 32 位（高 32 位已由 nonce 覆盖），保证 AAD 定长。
- `boot_id`、`session_id` 由 transcript 与 nonce 覆盖，不重复进 AAD。`dir` 进 AAD 是为了防止把 Pi→桥 的帧原样回放给 Pi。

---

## 6. 状态、拒绝与错误码

### 6.1 未认证期

产品构建（`CONFIG_PBR_ISOLATION_BASELINE=n`）在认证完成前只接受 `hello_req`。其他一律回 Error 帧 `authentication required`。

`ping`/`status` 仅 `CONFIG_PBR_ISOLATION_BASELINE=y` 的隔离基线构建可用，**编译期**排除，不是运行时开关。

### 6.2 拒绝顺序

```text
1. 帧头合法性（length ≤ 2048、dir/kind 在枚举内）      → 丢帧，继续扫
2. CRC16                                                → 丢帧，继续扫
3. env_ver / key_id（未知 key_id 含在内）               → 整帧丢弃，不回应
4. tag（用信封里的 nonce 计算，先验完整性）             → 整帧丢弃，不回应
5. auth_seq 单调性（0 或 ≤ 上次 → REPLAY）              → 已认证对端才回 REPLAY
6. JSON 解析 + schema/字段校验                          → INVALID_MESSAGE
7. event_id 去重 + TTL                                  → REPLAY / EVENT_EXPIRED
8. 业务分派 + ACK
```

**tag 必须在 auth_seq 之前验。** 反过来的话，攻击者无需密钥，只要发一条 `auth_seq = 2^63` 的伪造帧，就能把接收方序号推高，导致后续合法帧全被判 `REPLAY`——纯粹的未认证 DoS。序号只有在 tag 通过后才可信。

第 3、4 步**不回应**：给攻击者任何反馈都等于提供 oracle。第 5～7 步可以对已认证的对端发错误帧。

**`IDENTITY_REJECTED` 的可见性**：信封内发现的身份不一致只写本地日志/审计，不上线；明文 `hello` 阶段（§3.3/§3.4 验签失败）与已认证会话内才回错误帧。`UNSUPPORTED_VERSION` 同理。

### 6.3 错误码

沿用 f-day1 §7。握手期新增或使用的：

| code | 触发 | 是否上线 |
|---|---|---|
| `UNSUPPORTED_VERSION` | `proto_ver`/`suite_id` 不匹配 | 明文 `hello` 阶段回；信封内不回 |
| `IDENTITY_REJECTED` | 验签失败、公钥与 NVS 不一致、`key_id` 未知 | 明文阶段回；信封内只记日志 |
| `REPLAY` | `auth_seq` 非递增，或 `event_id` 重复 | 回 |
| `EVENT_EXPIRED` | `ttl_ms != 3000` 或按对端时钟判定已过期 | 回 |
| `authentication required` | 未认证期收到非握手帧 | 回（Error 帧，纯文本） |

### 6.4 事件字段（解密后）

`schema = "p0-control-v1"`。旧 `terminal-rfid-v1` 不能因字段相似被信任。

| 字段 | 约束 | 适用 |
|---|---|---|
| `event` | `attach/status/detach/rfid_card/ack` | 全部；`pong`/`bridge_status` **不走本规范**（见 6.5） |
| `bridge_id` | 1～64 字符 `[A-Za-z0-9_-]`，须与已认证身份一致 | 全部 |
| `key_id` | **2 位小写 hex**，须等于发送方公钥 `SHA-256[0]` | 全部 |
| `boot_id` | 32 位小写 hex，每次桥板启动更新 | 全部 |
| `session_id` | 32 位小写 hex = `hex(transcript_hash[0..16))` | 全部 |
| `event_id` | 1～4294967295，重传保持不变 | attach/status/detach/rfid_card；`ack` 不填 |
| `uptime_ms` | 非负整数 | 全部 |
| `ttl_ms` | **必须等于 3000** | 同上，`ack` 除外 |
| `slot_id` | 0 或 1 | 仅 `attach`/`status`/`detach` |
| `binding_generation` | 32 位小写 hex | 仅 `attach`/`status`/`detach` |
| `module_id` | 1～255 | 仅 `attach`/`status`/`detach`（**不是密码学身份**） |
| `module_instance` | 1～64 字符 `[A-Za-z0-9_-]` | 同上 |
| `reader_id` | 1～64 字符标签 | 仅 `rfid_card` |
| `uid_len` | 4 / 7 / 10 | 仅 `rfid_card` |
| `uid` | 与 `uid_len` 对应的 hex，**大小写均可**（桥板实际发大写） | 仅 `rfid_card`，只在加密载荷内 |

**RC522 是桥板级事件，不要求也不伪造 `slot_id` / `module_id` / `module_instance` / `device_id`。** 桥板现有 `rfid_service.c` 发的就是这种形状（`event/schema/reader_id/boot_id/uid/uid_len/uptime_ms`），Pi 侧校验不得要求模块字段，否则真桥板发的 RFID 事件会被自己拒绝。

`uid_len` 不匹配、`uid` 非法 hex、`uid` 长度不等于 `uid_len*2` → `INVALID_MESSAGE`。**UID 的 hex 大小写不作为拒绝条件**：`rc522.c` 用 `"0123456789ABCDEF"` 输出大写，规范若只收小写等于拒绝整台真桥板。大小写只在校验时折叠，`deriveUidIndex` 收原始字节，不收这段文本。`ttl_ms != 3000` → `EVENT_EXPIRED`。

`detach` 与 `attach`/`status` 一样必须带 `slot_id` 与 `binding_generation`：迟到事件靠这对字段判断属于哪一代绑定，缺了就无法拒绝串槽。**不要**把 `detach` 当成"没有位置信息的释放通知"。

### 6.5 诊断响应不属于本规范

`pong` / `bridge_status` 是 `kind=2` 命令的应答，字段是 `request_sequence`，**没有 `event_id`/`ttl_ms`/`schema`**，无法通过 6.4 的校验。

裁决：诊断响应只在已认证会话内、由信封层按 `request_sequence` 单独路由，**不进入 `PbrControl::validate()` 的事件集合**。把它们塞进事件校验会让实现被迫伪造字段。

### 6.5.1 ACK 字段

ACK 也走 `kind=1` 的信封，但**不带** `event_id`/`ttl_ms`/`slot_id`：

```json
{"schema":"p0-control-v1","event":"ack","bridge_id":"...","key_id":"a1",
 "boot_id":"<32 hex>","session_id":"<32 hex>","uptime_ms":12345,
 "ack_event_id":7,"result":"accepted"}
```

`result` ∈ `accepted/duplicate/expired/rejected`。ACK 本身不要求被 ACK，去重表不必记录它。

### 6.6 `event` 允许的 schema 缺失（**临时兼容，T-07 必须删除**）

桥板当前发的是 schema-less 的 `attach`/`detach`。`PbrControl::validate()` 对**没有 `schema` 字段的 `attach`/`detach`** 返回 `Verdict::Legacy`，不当作已认证事件；其他事件缺 `schema` 一律 `INVALID_MESSAGE`。

- 该分支是**未认证入口**，只允许在 `PBR_ISOLATION_BASELINE=y` 的过渡期存在。
- T-07 合入时必须删除该分支，并删掉本小节。
- 代码里已有到期标记，评审时按此核对。

### 6.7 配对与已配对判定

**窗口**：上电后 **180 秒**。`bridge_pins.h` 无按键，BOOT 需拆机，故用时间窗。

**NVS 无 Pi 公钥**（未配对）：

- 窗口内收到 `hello_finish` → 登记 `pi_identity_pub`，窗口立即关闭直到下次冷启动。
- 窗口外 → `IDENTITY_REJECTED`，并计数。

**NVS 已有 Pi 公钥**（已配对）——**判定条件是两条同时满足**：

```text
1. sig_pi 用 NVS 里的公钥验签通过
2. msg3_body 里的 pi_identity_pub 与 NVS 逐字节相等（65 字节全比）
```

任一不满足 → `IDENTITY_REJECTED`，断连。**不比 `pi_key_id`，只比完整公钥**（§2.1）。

**已有记录时另一台 Pi 来配对**：直接拒绝，**不覆盖**。换机必须先由管理员显式撤销（`revokeIdentity`）再重配。理由是覆盖窗口等于每次重启都开放一个 3 分钟的接管期，而"物理接触即可信"的窗口只能用于**首次**信任建立，不能用于替换已建立的信任。

**撤销后**：立即断开现有连接、销毁派生会话密钥，并拒绝后续 `hello_finish` 直到重新配对。不得等到下一次握手才生效。

**代价**：任何人在这 180 秒内插上自己的 Pi 即可完成首次配对。这是"物理接触即可信"的前提，P0 接受，P1 再评估按键确认。

---

## 7. 测试向量

`vectors.json` 由 `gen_vectors.py` 生成，输入全部固定（临时私钥为确定性序列），任何实现都能复现。

必须逐字节对齐的项：

| 键 | 含义 |
|---|---|
| `handshake.msg1_body` / `msg2_body` / `msg3_body` | 三条消息的定长体 |
| `handshake.t2` / `t3` | 两段 transcript（`T3` 的完整拼接） |
| `handshake.transcript_hash` | `SHA-256(T3)` |
| `handshake.session_id` | `transcript_hash[0..16)` |
| `handshake.nonce_prefix` | `transcript_hash[16..20)` |
| `handshake.x25519_shared` | ECDH 结果 |
| `keys.hkdf_96` / `k_pi_to_bridge` / `k_bridge_to_pi` / `k_handshake` | 96 字节与三把子钥 |
| `envelope.nonce` / `aad` / `ciphertext` / `tag` / `envelope_hex` | 桥板→Pi 的单帧完整结果 |
| `hello_ok.*` | `k_handshake` 封的 `hello_ok` |
| `identity_signature` | Pi 对 `T3` 的签名（`r‖s`） |
| `bridge_signature` | 桥板对 `T2` 的签名（`r‖s`） |

- 信封向量是**桥板 → Pi**（`dir=1`），用 `k_bridge_to_pi` 封印。用错方向密钥会"通过其它项但方向反了"，抓不住。
- 向量里的 `auth_seq` 各自从 1 起算，是**独立样本**；真实会话里 `hello_ok` 用 1，其后第一条事件是 2。
- ECDSA 的 `k` 随机，签名不可重算。`gen_vectors.py` 在已存在且验签通过时**复用**存储的签名，删除 `vectors.json` 后重跑会得到不同签名（仍能验签通过），其余字段不变。
- 反向向量 `negative[]` 每条写明预期错误码，实现必须返回**同一个** code，不能"任意拒绝即可"。

### 7.1 复现命令

```sh
python3 docs/pbr-secure-v1/gen_vectors.py
# 与仓库内 vectors.json 比对应为空 diff
```

---

## 8. mbedtls 互操作检查

`mbedtls_check.c` 用 mbedtls 独立复现 `vectors.json`（不依赖 Python），是两侧实现的**共同靶子**：

```sh
cc -I<prefix>/include docs/pbr-secure-v1/mbedtls_check.c \
   -L<prefix>/lib -lmbedcrypto -o mbedcheck
./mbedcheck docs/pbr-secure-v1/vectors.json
```

检查 5 项：X25519 共享、HKDF 96 字节、`k_bridge_to_pi` 解信封、`k_handshake` 解 `hello_ok`、两把身份公钥分别验 `r‖s` 签名。

**已验证的 mbedtls 版本**：2.28（Ubuntu 包）与 3.6.5（IDF 5.5.4 内置源码，主机编译）。**只支持 2.28 + 3.6 的公共 API**——`mbedtls_check.c` 不使用 `ecdh_context` 的内部字段（3.x 默认结构里没有 `.d`/`.grp`），也不使用 3.x 才有的 `mbedtls_ecdh_calc_secret`。

**四个实现陷阱**（第一次写都踩）：

| # | 陷阱 | 现象 |
|---|---|---|
| 1 | X25519 标量必须**小端**读入（`mbedtls_mpi_read_binary_le`） | 大端读入直接 `ECP_INVALID_KEY` |
| 2 | `mbedtls_ecdh_compute_shared`/`mbedtls_ecp_mul` **不做 RFC 7748 clamp** | 未 clamp 同样 `ECP_INVALID_KEY`（用 `mbedtls_ecp_read_key` 才会自动 clamp） |
| 3 | `mbedtls_ecdsa_verify` 收的是**摘要**，不是消息 | 传消息必然验签失败 |
| 4 | HKDF 的 `info` 长度必须显式给 14（`"pbr-secure-v1"`） | 传 `sizeof(str)` 会带上结尾 NUL，派生全错 |

第 1、2 条不会产生"看起来对但不一样"的结果，而是直接报错，容易被误判为"库不支持 X25519"。

---

## 9. 未覆盖与边界

**本文未覆盖、需另行定稿**：

1. **TLS 业务面**（T-12/DR-07）。PBR 的 AEAD 与 LAN TLS 是两条独立链路，本文不涉及。
2. **桥板双槽路由字段**（T-05 范围）。本文只定义安全封装，不改路由语义。
3. **重放窗口持久化**。当前只要求"会话内单调"，重启即清空；跨重启重放由 `session_id` 随机性覆盖。
4. **ACK 帧的具体封装**（T-07 实现时补）。ACK 走同一条信封，`kind=1`，明文 `{"event":"ack",...}`，字段见 f-day1 §4.2。

**已知限制**：

- P0 私钥明文存储，见 §2。
- ECDSA 的 `r‖s` 与 mbedtls 默认 DER 不同，实现必须显式转换。这一处最容易出错，两侧都要有一条针对 `r` 首字节高位为 1 的用例。
- 重放保护只在会话内有效。
