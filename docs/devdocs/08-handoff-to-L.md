# 交给 L：ESP 固件与硬件任务清单

> **本文只列 ESP 固件与硬件部分。** Pi 终端软件已到「代码完成、主机测试 47/47 全绿」，
> 但**未做任何 Pi 实机或 ESP 板实机验证**。L 的工作是把软件契约定下来的接口在固件里
> 实现，并补齐实机证据。
>
> 依据：`docs/TECHNICAL-ADVISORY-REPORT.docx` v4.1、`docs/RaspberryPi_ESP32_Modular_Terminal_Technical_Spec.docx`、
> `docs/devdocs/04-dev-tasks.md`、`docs/pbr-secure-v1/pbr-secure-v1.md`（修订 3）。
>
> 软件基线：`feat/p0-software-complete`（含 flite 语音库）。

---

## 0. 边界：哪些不是 L 的活

以下已在 Pi 侧完成、有测试，**L 不需要重做**：

| 已完成的 Pi 侧实现 | 位置 |
|---|---|
| 用户/PIN/锁屏/切用户 | `src/security/{UserService,AuthService,SessionManager,PinPolicy}.cpp` |
| 每用户 SQLCipher + §6.3 明文面加固 | `src/security/{SqlCipherStore,UserDatabase}.cpp` |
| 离线恢复码（丢 PIN 可救） | `src/security/RecoveryCode.cpp` |
| 审计（只记动作不记值） | `src/security/AuditLog.cpp` |
| 能力互斥（健康×联网） | `src/security/PermissionMatrix.cpp` |
| 卡索引（整机 `uid_map.json`） | `src/security/CardMatcher.cpp` |
| 备份/恢复/加密导出 | `src/security/Backup.cpp` |
| PBR AEAD / 重放 / 去重 / TTL | `src/protocol/{pbr_aead,pbr_session}.cpp` |
| PBR 握手密钥派生（X25519+HKDF） | `src/protocol/pbr_handshake.cpp` |
| LAN TLS + 指纹固定 | `src/transport/HotspotLanTransport.cpp` |
| BLE 两层认证（配对+挑战响应） | `src/transport/BleTransport.cpp` |
| worker 隔离（Unix socket IPC） | `src/core/{ModuleWorkerClient,module_worker_main}.cpp` |
| 模块数据接口（模块 QML 读写用户库） | `src/core/ModuleDataApi.cpp` |
| 模块页面独立引擎 + import 白名单 | `src/core/ModulePageHost.cpp` |
| 联网总开关 / 有限重试 / 流量上限 | `src/core/{NetworkGate,FetchBudget}.cpp` |
| 供电状态语义（未知即未知） | `src/core/PowerStatus.cpp` |

**已完成的模块示例**（业务逻辑有测试，硬件未接）：

| 模块 | ID | 测试 | 硬件依赖 |
|---|---|---|---|
| 智能盆栽 | 80 | 历史/ABI | 已有实机模块 |
| 啸天红外围界 | 82 | 8/8 业务逻辑 | 双探头未采购 |
| 血压读数 | 83 | 9/9 业务逻辑 | 血压计未选定 |

---

## 1. PBR1 控制面 AEAD（T-07）— **最高优先级**

**为什么最关键**：Pi 侧已能验 AEAD 并用官方向量逐字节验证过，但桥板现在发的仍是
CRC-only 明文帧。**桥板不发 AEAD，UID 就不可信，整条刷卡链路悬空。**

### 1.1 算法（不留选择余地）

| 用途 | 算法 | 参数 |
|---|---|---|
| 密钥交换 | X25519 (RFC 7748) | 每次连接新建临时密钥对，用完销毁 |
| KDF | HKDF-SHA256 (RFC 5869) | `info = "pbr-secure-v1"`，输出 96 字节 |
| AEAD | ChaCha20-Poly1305 (RFC 8439) | 12 字节 nonce，16 字节 tag |
| 身份签名 | ECDSA P-256 + SHA-256 | 签名编码固定 `r‖s` 64 字节 |
| 哈希 | SHA-256 | transcript / key_id / session_id |

### 1.2 mbedtls 配置（**踩过的坑，必须照做**）

ESP-IDF 5.5.4 的 mbedtls 3.6.5 经典 API 不含 Ed25519，所以用 ECDSA P-256。必须显式打开四项：

```
MBEDTLS_CHACHA20_C          # 前置
MBEDTLS_POLY1305_C          # 前置
MBEDTLS_CHACHAPOLY_C        # 依赖上面两个
MBEDTLS_HKDF_C
```

> **只写 `CHACHAPOLY` 一项会被静默丢弃** —— 不报错、`sdkconfig` 里不出现、固件照样编译成功，
> 运行时 AEAD 不可用。必须四项一起写。

### 1.3 信封格式（Pi 侧已实现，必须逐字节一致）

```
payload = env_ver(1) || key_id(1) || nonce(12) || ciphertext(N) || tag(16)
nonce   = nonce_prefix(4) || auth_seq_u64le(8)
AAD     = env_ver(1) || key_id(1) || kind(1) || dir(1)
        || auth_seq_u32le(4) || plaintext_len_u16le(2)      // 定长 10 字节
```

- `key_id` 是**发送方**身份公钥的 `SHA-256[0]`
- 明文上限 2018 字节，超限拒绝不截断
- **载荷是二进制**，必然含 `0x00`；任何按 C 字符串处理的实现都会把合法密文丢掉

### 1.4 拒绝顺序（不能调换）

```
1. 帧头合法性        → 丢帧
2. CRC16             → 丢帧
3. env_ver / key_id  → 整帧丢弃，不回应
4. tag               → 整帧丢弃，不回应
5. auth_seq 单调性   → 已认证对端才回 REPLAY
6. JSON + schema     → INVALID_MESSAGE
7. event_id 去重+TTL → REPLAY / EVENT_EXPIRED
8. 业务分派 + ACK
```

**第 4 步必须在第 5 步之前。** 反过来的话，攻击者无需密钥，发一条 `auth_seq = 2^63`
的伪造帧就能把接收方序号推高，后续合法帧全被判 REPLAY —— 纯未认证 DoS。
Pi 侧 `pbr_replay_and_dedup` 有专门用例锁这个顺序。

### 1.5 验收（必须逐条打勾）

- [ ] 用 `docs/pbr-secure-v1/vectors.json` 的 `envelope` 组做 KAT，逐字节对上
- [ ] `handshake` 组：X25519 双向共享密钥相等且等于 `x25519_shared`
- [ ] `keys` 组：HKDF 输出切出的三个方向密钥逐字节一致
- [ ] `t3` 长度 = 226、`t2` 长度 = 140（规范 §3.6 自检表）
- [ ] `auth_seq = 0` 与回退均回 REPLAY
- [ ] 伪造大序号 + 坏 tag → 序号窗口**不推进**
- [ ] `ttl_ms != 3000` → EVENT_EXPIRED
- [ ] 重复 `event_id` → `result=duplicate`，业务只执行一次
- [ ] 去重表满 4096 → 重建会话，不静默丢事件

---

## 2. RC522 读卡事件（T-08 D 侧）

Pi 侧 `SecurityService::resolveCard()` 期望的字段：

```json
{"schema":"p0-control-v1","event":"rfid_card",
 "bridge_id":"...","key_id":"a1","boot_id":"<32 hex>","session_id":"<32 hex>",
 "uptime_ms":12345,"event_id":7,
 "uid":"<hex>","uid_len":4}
```

- [ ] `uid_len` 只允许 4 / 7 / 10；其余丢弃
- [ ] `uid` 必须是纯 hex，大小写都接受（Pi 侧 `QByteArray::fromHex` 处理）
- [ ] raw UID **不落盘、不写日志**（Pi 侧只存 HMAC 索引）
- [ ] 桥板级事件，**不伪造** slot_id / device_id 上下文
- [ ] `event_id` 每个逻辑事件 +1，重传保持不变
- [ ] 重传时 `auth_seq` 必须换新（nonce 唯一性）
- [ ] 支持 ACK：`{"event":"ack","ack_event_id":N,"result":"accepted"}`

**Pi 侧已实现的对应逻辑**：刷卡只选用户，**不直接登录**，必须再输 PIN
（`card_selects_user_only` 测试锁死了这条）。

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
- [ ] 桥板失联时 Pi 侧禁止依赖读卡/物理位置的新操作
      （已实现 `handleBridgeUnavailable()`）

### 3.1 实机测试矩阵（报告 §10.4）

- [ ] 两个不同型号同时在线
- [ ] 相同型号同时在线
- [ ] 任意插入顺序
- [ ] 单槽拔出 / 接触抖动
- [ ] 另一槽继续工作
- [ ] 总线冲突
- [ ] 桥板重启
- [ ] 模块移槽
- [ ] 错误 / 重复事件
- [ ] 单槽短路 / 过流
- [ ] 两个模块共享热点时的网络路由

---

## 4. LAN 业务面 TLS（T-12 ESP 侧）

Pi 侧已实现：`QSslSocket` + `VerifyNone` 握手后比对 SHA-256 指纹，无明文回退。

- [ ] 模块作 TLS server（现状是 `listen`，**不改方向**）
- [ ] 自签证书，`CN = <module_id>` 与设备序列
- [ ] 支持 `TlsV1_2OrLater`
- [ ] 私钥存 NVS，**不进固件镜像、不进 sdkconfig**

> ⚠️ **开工前必须和 Pi 侧对齐**：`HotspotLanTransport` 现在比对 CN 用的字段是
> `bridge_id`（复用），而规范 §0.8.1 原文写 `CN=<module_id>`。
> **两边不一致会导致实机握手失败。** 请在 PR 里明确最终用哪个。

Pi 侧还要求 profile 里带 `pin_sha256`（32 字节 hex），没有指纹就直接拒绝连接。
登记流程：首次配对时由管理员确认后写入。

---

## 5. BLE 业务面两层认证（T-12 ESP 侧）— **硬阻塞**

Pi 侧已实现应用层挑战-响应：

```
Pi → 模块:  0xB1 || challenge(32B 随机)
模块 → Pi:  0xB1 || HMAC-SHA256(k, challenge || module_id || boot_id)
```

- [ ] 链路层：`sm_io_cap` 配置，要求 `pairingStatus == Paired` 才能写特征
- [ ] 应用层：32 字节对称密钥做 HMAC-SHA256，**通过前不接收业务数据**
- [ ] 3 次失败断开，不降级明文
- [ ] **NVS 密钥存储**（模块数据服务现在完全没有，**这是硬阻塞**）
- [ ] 密钥由 T-03 配对时经 LAN 下发，幂等：已下发则拒绝重复

Pi 侧单条业务 payload ≤512 字节，分片在 `BleTransport` 内部完成，对上层透明。

---

## 6. 电池遥测（报告 §13）

Pi 侧已实现语义层（`src/core/PowerStatus.cpp`）：默认 `unknown`，
未知时**禁止自动关机**；越界百分比直接丢弃。

需要 L 的上报：

- [ ] MAX17043 或等效电量计读数
- [ ] 字段：`battery_v` / `battery_pct` / `power_source`（external/battery/unknown）
- [ ] **无可靠检测接线时 `power_source` 必须保持 `unknown`，不得猜成 battery**
- [ ] 「USB/外部供电检测」与「电池电量计」是两个独立信号，不能互相替代

---

## 7. 啸天红外围界（T-06 / T-14）

Pi 侧模块代码**已完成**（`module-devkit/examples/xiaotian-fence/`），业务逻辑 8/8 测试通过：

| 已实现 | 说明 |
|---|---|
| 输入去抖 | 连续 2 次同值才认可 |
| 告警保持 | 3 秒窗口内 `alarm_active=true` |
| 触发计数 | 仅"布防中 + 未遮挡→遮挡"计数 |
| 六态 | 布防/未布防/告警/离线/故障/未知 |
| 未知不冒充 | 去抖未收敛时 `valid=false` |
| 供电未知 | 无硬件时 `power_known=false` |

固件需要做的：

- [ ] 采购两对红外探头
- [ ] 双路独立遮挡检测（`segments[]` 数组）
- [ ] 输入去抖（建议与 Pi 侧同步：连续 2 次）
- [ ] 心跳上报（Pi 侧超时 9 秒转未知）
- [ ] 上报字段：

```json
{"segments":[{"segment_id":"A","blocked":true,"sample_ms":1000,"valid":true}],
 "armed":true,"trigger_count":3,"trigger_ms":1480,"heartbeat_ms":2000,
 "sensor_online":true,"fault_code":"",
 "power_known":true,"power_source":"battery","has_battery":true,"battery_percent":82}
```

- [ ] 没有防拆/断线硬件时**不能声称已检测**（`sensor_online` 与 `fault_code` 是两种状态）

---

## 8. 血压计（T-12 / 报告 §12）

Pi 侧模块代码**已完成**（`module-devkit/examples/blood-pressure/`），业务逻辑 9/9 通过：

| 已实现 | 说明 |
|---|---|
| 边界值 | 收缩压 40–300、舒张压 20–200、脉搏 20–250 |
| 反序检测 | 收缩压 ≤ 舒张压判无效，**不交换数值** |
| 空测量 | 明确报 `no_measurement`，不造记录 |
| 来源区分 | `device.auto` / `device.manual` / `user.entry` |
| 多指标 | 一次测量三个值存成一条记录，缺失项写 NULL |

固件需要做的：

- [ ] 选定一款有公开协议/合法接口的血压计
- [ ] 优先考察标准 BLE Blood Pressure Service `0x1810`
- [ ] **不可假定候选产品实现标准或允许数据访问** —— 需实测
- [ ] 上报字段：

```json
{"systolic":120,"diastolic":80,"pulse":72,"unit":"mmHg",
 "source":"device.auto","measured_at_ms":1717000000000}
```

- [ ] 设备型号来自预置 profile（`device_model`），不接受模块自报

> 若 ESP 同时做模块业务 BLE 端点与血压计 BLE 主机不稳定，可评估 Pi 直接连接设备，
> 但不牺牲模块主链路（报告 §12.2）。

---

## 9. 盆栽固件（T-13）

Pi 侧已改造：模块历史不再写明文 JSON，改由宿主写入每用户 SQLCipher 库；
模块页通过 `moduleData.appendSample()` 落库。

- [ ] `emit_event("sample", ...)` 只发**原始观测**，不自己聚合/持久化
- [ ] 现有 `History.h` 的 `save()` 已被 Pi 侧默认关闭
      （仅 `ORANGE_PI_PLANTER_LEGACY_JSON=1` 时打开，供旧数据迁移）
- [ ] 确认实机模块固件版本与 Pi 侧 3.0.0 manifest 一致

---

## 10. 实机验证（Pi 侧代码齐了，但零实机证据）

报告 §16 要求六层证据，现在只有前两层（源码单测、主机模拟）。
以下必须由 L 在真机上补齐：

- [ ] **目标 Pi 构建**：Pi 3B / ARM64 Bookworm / Qt 6.5.3 / Wayland
- [ ] **Pi 实机**：真实 Xorg + 硬件 OpenGL
- [ ] **ESP 板实机**：桥板 + 模块
- [ ] **环境实测**：热点不转发 WAN、worker UID ≠ 宿主 UID

### 10.1 具体命令

```bash
# SQLCipher 版本与错误 key 行为（§0.1 必须在 Pi 上复测）
# 按 docs/devdocs/05-mr-z-todo.md §0.1 的命令跑一次

# worker 隔离（IT-013）
systemctl show orange-pi-worker@test -p DynamicUser
ps -o uid= -p $(systemctl show -p MainPID --value orange-pi-worker@test)
# 断言：worker UID ≠ 宿主 UID

# 热点不转发 WAN
# 模块侧 curl 外网应失败，宿主侧正常

# 模块页数据接口（确认 QML 能读写当前用户库）
# 登录 → 打开模块 → 界面应显示历史曲线
```

---

## 11. 优先级建议

| 顺序 | 任务 | 理由 |
|---|---|---|
| 1 | **PBR AEAD（§1）** | Pi 侧已能验，桥板不发 AEAD 则整条刷卡链路悬空 |
| 2 | **RC522 事件（§2）** | 依赖 §1，是 P0 演示第一条 |
| 3 | **NVS 密钥 + BLE 两层（§5）** | BLE 业务面的硬阻塞 |
| 4 | **LAN TLS CN 对齐（§4）** | 开工前先和 Pi 侧确认用 module_id 还是 bridge_id |
| 5 | **双槽电气（§3）** | T-16 的前置门 |
| 6 | **盆栽固件配合（§9）** | 改动小 |
| 7 | **啸天固件（§7）** | Pi 侧页面与逻辑已就绪，只等硬件 |
| 8 | **血压计适配（§8）** | Pi 侧页面与逻辑已就绪，等选型 |
| 9 | **电池遥测（§6）** | 不在 19 条 P0 硬门槛里 |
| 10 | **实机验证（§10）** | 不比代码容易，但它是"验收"与"写完"的分界 |

---

## 12. 本文不承诺的事

- Pi 侧代码**没有**在 Pi 上编译过，全部验证在 x86_64 主机完成
- 所有 `NOT_RUN` 的项不得写成 PASS
- 本文列出的接口是 Pi 侧**已实现且已测试**的，但两端联调未做
- 指纹固定用的 CN 字段与规范原文不一致，见 §4 的待对齐项
- 六层证据目前只完成两层，`04-dev-tasks.md` §7 的 19 条 P0 条件不得勾选
