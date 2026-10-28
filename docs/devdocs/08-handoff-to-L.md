# 交给 L：ESP 侧任务清单

> **本文只列 ESP 固件与硬件部分。** Pi 终端软件已完成到「代码可编译、主机测试全绿」，
> 但**未做任何 Pi 实机或 ESP 板实机验证**。L 的工作是把软件契约定下来的接口在
> 固件里实现，并补齐实机证据。
>
> 依据：`docs/TECHNICAL-ADVISORY-REPORT.docx` v4.1、`docs/devdocs/04-dev-tasks.md`、
> `docs/pbr-secure-v1/pbr-secure-v1.md`（修订 3）。
> 基线：Pi 端 `pi_terminal/` @ 本文提交时的 main；ESP 仓库 `gggitj/ESP-Modular-Terminal-Service`。

---

## 0. 边界：哪些不是 L 的活

以下已在 Pi 侧完成，**L 不需要重做**：

| 已完成 | 位置 |
|---|---|
| 用户/PIN/锁屏/切用户 | `src/security/{UserService,AuthService,SessionManager,PinPolicy}.cpp` |
| 每用户 SQLCipher（含 §6.3 明文面加固） | `src/security/{SqlCipherStore,UserDatabase}.cpp` |
| 审计表 | `src/security/AuditLog.cpp` |
| 能力互斥（健康×联网） | `src/security/PermissionMatrix.cpp` |
| 卡索引（整机 `uid_map.json`） | `src/security/CardMatcher.cpp` |
| 备份/恢复 | `src/security/Backup.cpp` |
| PBR AEAD / 重放 / 去重 / TTL | `src/protocol/{pbr_aead,pbr_session}.cpp` |
| PBR 握手密钥派生（X25519 + HKDF） | `src/protocol/pbr_handshake.cpp` |
| LAN TLS + 指纹固定 | `src/transport/HotspotLanTransport.cpp` |
| BLE 两层认证（配对 + 挑战响应） | `src/transport/BleTransport.cpp` |
| worker 隔离（Unix socket IPC） | `src/core/{ModuleWorkerClient,module_worker_main}.cpp` |

Pi 侧验收测试：`pbr_aead_envelope`、`pbr_replay_and_dedup`、`pbr_handshake_derivation`、
`business_data_security`、`worker_socket_ipc` 等，均对 `docs/pbr-secure-v1/vectors.json`
的官方向量逐字节验证。

---

## 1. PBR1 控制面：AEAD 与事件可靠性（T-07）

**这是最关键的一项。** Pi 侧已经能验 AEAD，但桥板现在还发 CRC-only 明文帧。

### 1.1 算法（不留选择余地）

| 用途 | 算法 | 参数 |
|---|---|---|
| 密钥交换 | X25519 (RFC 7748) | 每次连接新建临时密钥对，用完销毁 |
| KDF | HKDF-SHA256 (RFC 5869) | `info = "pbr-secure-v1"`，输出 96 字节 |
| AEAD | ChaCha20-Poly1305 (RFC 8439) | 12 字节 nonce，16 字节 tag |
| 身份签名 | ECDSA P-256 + SHA-256 | 签名编码固定 `r‖s` 64 字节 |
| 哈希 | SHA-256 | transcript / key_id / session_id |

### 1.2 mbedtls 配置（**踩过的坑**）

ESP-IDF 5.5.4 的 mbedtls 3.6.5 经典 API 不含 Ed25519，所以用 ECDSA P-256。必须显式打开：

```text
MBEDTLS_CHACHA20_C          # 前置
MBEDTLS_POLY1305_C          # 前置
MBEDTLS_CHACHAPOLY_C        # 依赖上面两个
MBEDTLS_HKDF_C
```

> **只写 `CHACHAPOLY` 一项会被静默丢弃** —— 不报错、`sdkconfig` 里不出现、固件照样编译成功，
> 运行时 AEAD 不可用。必须四项一起写。

### 1.3 信封格式（Pi 侧已实现，必须逐字节一致）

```text
payload = env_ver(1) || key_id(1) || nonce(12) || ciphertext(N) || tag(16)
nonce   = nonce_prefix(4) || auth_seq_u64le(8)
AAD     = env_ver(1) || key_id(1) || kind(1) || dir(1)
        || auth_seq_u32le(4) || plaintext_len_u16le(2)
```

- `key_id` 是**发送方**身份公钥的 `SHA-256[0]`
- 明文上限 2018 字节，超限拒绝不截断
- **载荷是二进制**，必然含 `0x00`；任何按 C 字符串处理的实现都会把合法密文丢掉

### 1.4 拒绝顺序（不能调换）

```text
1. 帧头合法性        → 丢帧
2. CRC16             → 丢帧
3. env_ver / key_id  → 整帧丢弃，不回应
4. tag               → 整帧丢弃，不回应
5. auth_seq 单调性   → 已认证对端才回 REPLAY
6. JSON + schema     → INVALID_MESSAGE
7. event_id 去重+TTL → REPLAY / EVENT_EXPIRED
8. 业务分派 + ACK
```

**第 4 步必须在第 5 步之前。** 反过来的话，攻击者无需密钥，发一条 `auth_seq = 2^63` 的伪造帧
就能把接收方序号推高，后续合法帧全被判 REPLAY —— 纯未认证 DoS。Pi 侧
`pbr_replay_and_dedup` 有一条用例专门锁这个顺序。

### 1.5 验收标准

- [ ] 用 `docs/pbr-secure-v1/vectors.json` 的 `envelope` 组做 KAT，逐字节对上
- [ ] 用 `handshake` 组的 X25519 双向共享密钥相等，且与 `x25519_shared` 一致
- [ ] 用 `keys` 组的 HKDF 输出切出三个方向密钥，逐字节一致
- [ ] `auth_seq = 0` 与回退均回 REPLAY
- [ ] 伪造大序号 + 坏 tag → 序号窗口不推进
- [ ] `ttl_ms != 3000` → EVENT_EXPIRED
- [ ] 重复 `event_id` → `result=duplicate`，业务只执行一次
- [ ] 去重表满 4096 → 重建会话，不静默丢事件

---

## 2. RC522 读卡事件（T-08 D 侧）

Pi 侧 `CardIndex` 期望收到的字段：

```json
{"schema":"p0-control-v1","event":"rfid_card",
 "bridge_id":"...","key_id":"a1","boot_id":"<32 hex>","session_id":"<32 hex>",
 "uptime_ms":12345,"event_id":7,
 "uid":"<hex>","uid_len":4}
```

要求：

- [ ] `uid_len` 只允许 4 / 7 / 10；其余丢弃
- [ ] raw UID **不落盘、不写日志**（Pi 侧只收 HMAC 索引）
- [ ] 桥板级事件，**不伪造** slot_id / device_id 上下文
- [ ] `event_id` 每个逻辑事件 +1，重传保持不变
- [ ] 重传时 `auth_seq` 必须换新（nonce 唯一性）
- [ ] 支持 ACK：收到 `{"event":"ack","ack_event_id":N,"result":"accepted"}`
- [ ] 有限重传（建议 3 次）+ TTL 3000ms

---

## 3. 双槽（T-05 / T-16）

Pi 侧 `SlotRouter` 期望的位置事件：

```json
{"bridge_id":"...","slot_id":0,"device_id":"...","binding_generation":"<32 hex>"}
```

四项**缺一不可**；缺任一字段时 Pi 保持旧行为、不路由（兼容旧桥板）。

- [ ] 双槽独立电气通道的**实测结论**（T-06 前置门）
- [ ] `binding_generation` 在换设备时递增
- [ ] 单槽拔出不影响另一槽（E2E-006）
- [ ] 桥板失联时的行为：Pi 侧禁止依赖读卡/物理位置的新操作

---

## 4. LAN 业务面 TLS（T-12 ESP 侧）

Pi 侧已实现：`QSslSocket` + `VerifyNone` 握手后比对 SHA-256 指纹，无明文回退。

需要 L 实现：

- [ ] 模块作 TLS server（现状是 `listen`，**不改方向**）
- [ ] 自签证书，`CN = <module_id>` 与设备序列
- [ ] 支持 `TlsV1_2OrLater`
- [ ] 证书私钥存 NVS，**不进固件镜像、不进 sdkconfig**

> **待对齐**：Pi 侧 `HotspotLanTransport` 目前比对 `CN` 用的字段是
> `bridge_id`（复用），而规范 §0.8.1 原文写 `CN=<module_id>`。
> **开工前请先确认 CN 到底用哪个**，否则实机会握手失败。

---

## 5. BLE 业务面两层认证（T-12 ESP 侧）

Pi 侧已实现应用层挑战-响应：

```text
Pi → 模块:  0xB1 || challenge(32B 随机)
模块 → Pi:  0xB1 || HMAC-SHA256(k, challenge || module_id || boot_id)
```

- [ ] 链路层：`sm_io_cap` 配置，要求 `pairingStatus == Paired` 才能写特征
- [ ] 应用层：32 字节对称密钥做 HMAC-SHA256，**通过前不接收业务数据**
- [ ] 3 次失败断开，不降级明文
- [ ] **NVS 密钥存储**（模块数据服务现在完全没有 NVS 密钥存储，这是硬阻塞）
- [ ] 密钥由 T-03 配对时经 LAN 下发，幂等：已下发则拒绝重复

---

## 6. 电池遥测（§13）

Pi 侧已实现语义层（`src/core/PowerStatus.cpp`）：默认 `unknown`，
未知时**禁止自动关机**。等 L 的上报接口：

- [ ] MAX17043 或等效电量计读数
- [ ] 上报字段：`battery_v` / `battery_pct` / `power_source`（external/battery/unknown）
- [ ] 无可靠检测接线时，`power_source` 必须保持 `unknown`，**不得猜成 battery**
- [ ] 明确「USB/外部供电检测」与「电池电量计」是两个独立信号

---

## 7. 啸天红外围界模块（T-06 / T-14）

**仓库里目前完全没有这个模块**（无目录、无代码、无文档）。

- [ ] 采购
- [ ] 双路真实输入
- [ ] 事件 schema
- [ ] 页面（Pi 侧 Z 负责，但需要火 firmware 先出数据）
- [ ] 测速 / 户外 / 闭合区域验证标为后续，不算 P0

---

## 8. 盆栽固件（T-13）

Pi 侧已改造：模块历史不再写明文 JSON，改由宿主写入每用户 SQLCipher 库。
盆栽固件需要配合：

- [ ] `emit_event("sample", ...)` 只发**原始观测**，不自己聚合/持久化
- [ ] 现有 `History.h` 的 `save()` 已被 Pi 侧默认关闭（仅 `ORANGE_PI_PLANTER_LEGACY_JSON=1` 时打开，供旧数据迁移）
- [ ] 确认现有实机模块的固件版本与 Pi 侧 3.0.0 manifest 一致

---

## 9. 实机验证（Pi 侧代码齐了，但零实机证据）

技术建议报告 §16 要求六层证据，现在只有前两层（源码单测、主机模拟）。
以下必须由 L 在真机上补齐：

- [ ] 目标 Pi 构建：Pi 3B / ARM64 Bookworm / Qt 6.5.3 / Wayland
- [ ] Pi 实机：真实 Xorg + 硬件 OpenGL
- [ ] ESP 板实机：桥板 + 模块
- [ ] 环境实测：热点不转发 WAN、worker UID ≠ 宿主 UID

### 9.1 具体命令（Pi 上执行）

```bash
# SQLCipher 版本与错误 key 行为（§0.1 必须在 Pi 上复测）
sqlite3 --version
# 或按 docs/devdocs/05-mr-z-todo.md §0.1 的命令跑一次

# worker 隔离（IT-013）
systemctl show orange-pi-worker@test -p DynamicUser
ps -o uid= -p $(systemctl show -p MainPID --value orange-pi-worker@test)
# 断言：worker UID ≠ 宿主 UID

# 热点不转发 WAN
# 在模块侧 curl 外网应失败，宿主侧正常
```

---

## 10. 优先级建议

| 顺序 | 任务 | 理由 |
|---|---|---|
| 1 | **PBR AEAD（§1）** | Pi 侧已能验，桥板不发 AEAD 则 UID 不可信，整条刷卡链路悬空 |
| 2 | **RC522 事件（§2）** | 依赖 §1，是 P0 演示第一条 |
| 3 | **NVS 密钥存储 + BLE 两层（§5）** | BLE 业务面的硬阻塞 |
| 4 | **LAN TLS CN 对齐（§4）** | 开工前先和 Pi 侧确认 CN 用 module_id 还是 bridge_id |
| 5 | **双槽电气（§3）** | T-16 的前置门 |
| 6 | **盆栽固件配合（§8）** | 改动小，配合 Pi 侧已完成的存储改造 |
| 7 | **实机验证（§9）** | 不比代码容易，但它是"验收"与"写完"的分界 |
| 8 | 电池遥测（§6） | 不在 19 条 P0 硬门槛里 |
| 9 | 啸天（§7） | 硬件未采购 |

---

## 11. 本文不承诺的事

- Pi 侧代码**没有**在 Pi 上编译过，全部验证在 x86_64 主机完成
- 所有 `NOT_RUN` 的项不得写成 PASS
- 本文列出的接口是 Pi 侧**已实现且已测试**的，但两端联调未做
- 指纹固定用的 CN 字段与规范原文不一致，见 §4 的待对齐项
