# D 技术签认：`pbr-secure-v1` 修订 3

| 项 | 内容 |
|---|---|
| 签认人 | Mr.D（规范与 Pi 侧实现主责） |
| 日期 | 2026-10-28 |
| 签认对象 | [`pbr-secure-v1.md`](pbr-secure-v1.md) **修订 3**（= 修订 2 的字节布局 + §3.0 记法补全） |
| 关联 | `00-decisions-for-f.md` §6 的 DR-01 条件项、回执 **B-01**、T-07 前置 |
| 依赖 | 本签认以 PR「define tag/elem notation」先合入为前提；修订 3 之前不存在 |
| **结论** | **技术签认成立。DR-01 的「D 技术签认」条件可关闭，B-01 可关闭，T-07 可开工。** |

## 1. 签认的范围

我逐节读了 §0～§9，并对**字节层**做了独立复现（§2）。规范里凡是"必须逐字节一致"的量，都不是靠阅读判断的，而是跑出来的。

期间发现并修正了 1 处会导致两侧必分叉的缺口：修订 2 使用 `tag("...")` 却从未定义其编码（见 §4 R-0）。该修正只动文字，不动字节——`vectors.json` 与 `mbedtls_check.c` 无需改动。

## 2. 证据（全部可复现）

| # | 检查 | 命令 | 结果 |
|---|---|---|---|
| 1 | 向量可重现 | `python3 gen_vectors.py` ×2，再与已提交版本比 | 两次一致；与仓库 `vectors.json` **零 diff** |
| 2 | mbedtls **2.28.8** | `cc -I<2.28 头> mbedtls_check.c /lib/…/libmbedcrypto.so.7` | **7/7 PASS** |
| 3 | mbedtls **3.6.5**（IDF 5.5.4 源码，主机编译） | `./build_mbedtls_host.sh` → `cc -I<idf mbedtls 头> … libmbedcrypto.a` | **7/7 PASS** |
| 4 | Pi 侧事件校验器 | 独立编译 `pibridge_protocol.cpp + control_event.cpp + tests/protocol_test.cpp`（仅 Qt6::Core） | `protocol tests passed` |

第 2、3 项覆盖：X25519 共享、`nonce_prefix` 切片、HKDF-96、`hello_ok` 用 `k_handshake` 解密、信封用 `k_bridge_to_pi` 解密、Pi 与桥板身份签名的 `r‖s` 验签。

第 3 项此前只能照抄 `PR-NOTES.md` 的"主机编译"结论——方法没进仓库，别人无法复做。本次把它固化为 `build_mbedtls_host.sh`（断言版本必须是 3.6.5，输出库哈希）。

## 3. 我明确**未**验证的部分（不得据此宣称通过）

| 未验项 | 原因 |
|---|---|
| 整仓 `cmake` 配置与全量 `ctest` | 本机未齐备 WebEngine/Bluetooth/SerialPort/TextToSpeech/StateMachine 等 Qt 模块；第 4 项用**独立编译**替代，只覆盖 Qt6::Core 相关的协议测试 |
| 真实 Pi 3B / ARM64 交叉编译 | 无该环境 |
| 任何实机（串口、控制桥、RC522、模块） | 无硬件，一律 `NOT_RUN` |
| `PbrSession` / `ReplayGuard` / `EventAckManager` | T-07 尚未实现，签认的是**规范可实现性**，不是实现质量 |
| 状态机时序（500ms 超时、5 次重试、重连、零化路径） | 规范已定义，未实现即未验证 |
| mbedtls 在 IDF 目标工具链（xtensa/riscv）下的编译 | 主机编译 ≠ 目标编译 |

**特别说明**：本签认不等于"双向认证可用"。按 DR-08 的条件，任何"认证可用"的结论必须等实机 IT-003/IT-007。

## 4. 对规范的保留意见

以下均**不阻塞签认**，建议在 T-07 实现时补测试或在下一修订处理。

**R-0（已在修订 3 修掉）** `tag()`/`elem()` 未定义。修订 2 的 §3.3/§3.4 写 `tag("pbr_hello_req")`，全文无定义。按裸 ASCII 实现则 `T2` 每元素少 2 字节，桥板签名永不通过——而两侧日志都正常，属最难定位的一类。修订 3 补 §3.0 并给出 140/226 字节自检表。

**R-1 §6.2 的拒绝顺序需要一条"顺序断言"测试。** 规范写对了（tag 先于 `auth_seq`，避免未认证 DoS），但实现极易写反。建议 T-07 加一个用例：构造 `auth_seq = 2^63` 的**伪造**帧，断言接收方**不推进**序号窗口。这条测试的存在本身就是防止将来回归。

**R-2 §5.3 的 AAD 只取 `auth_seq` 低 32 位。** 跨过 2^32 后 AAD 低 32 位会重复，但 nonce 用的是完整 64 位，nonce 仍唯一，AEAD 安全性不依赖 AAD 唯一性——**结论可接受**。建议在规范里加一句注明"这是有意的"，否则实现者会怀疑写错而擅自改成 64 位 AAD，造成分叉。

**R-3 §6.4 的大小写不对称是有意的，建议写明。** `uid` 大小写均可（兼容 `rc522.c` 的 `"0123456789ABCDEF"` 大写输出），而 `key_id` 必须小写 hex（自定字段，与信封里的 1 字节对应）。两处规则不同但没有矛盾；不注明的话，实现者容易"统一"成一种。

**R-4 `hello_finish` 体积 329 字节已写进 §3.6，但命令上限 512 这个数值本身没写进规范。** 规范只要求"任何合法长度的 PBR1 帧（≤2048）不得被静默丢弃"，这是对的（不越界规定固件常量）。仅提示：将来 L 对接时若仍用 256，会表现为"握手停在第 2 步且无错误帧"。

## 5. 签认后的下一步

按 `F-REVIEW-2.md` §5.3.1 的顺序：

1. **T-03**：`TrustStore` / `HostIdentity`（无前置）
2. **T-07**：`PbrSession` / `ReplayGuard` / `EventAckManager` + §2 的六条验收用例
3. **T-08 前半段**：解密 → `validate()` → `deriveUidIndex()`（密钥等 Z）

不碰 ESP 仓库；实机项保持 `NOT_RUN`。
