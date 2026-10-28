# 更新日志与测试详情

> 本文是 `feat/p0-software-complete` 这一个 PR 的完整变更记录与测试证据。
>
> - **分支**：`feat/p0-software-complete`
> - **基线**：`origin/main` @ `8748c4f`（已含 flite 语音库 PR #40）
> - **提交数**：38
> - **文件改动**：177 个文件，+20692 / −254（不含 `third_party/` 二进制）
> - **日期**：2026-10-29

---

## 0. 证据层级（先说清楚哪个能信、哪个不能）

技术建议报告 §16 要求六层证据，**不能用其中一种替代其他层级**。

| 层级 | 状态 | 说明 |
|---|---|---|
| 1. 源码单测 | ✅ 完成 | 47 项 ctest（见 §4）+ 模块自带 17 项 |
| 2. 主机模拟 | ✅ 完成 | 双槽桥板 + 双模块 + 电池端到端演示（见 §5） |
| 3. 目标 Pi 构建 | ❌ 未做 | 只在 x86_64 主机编译过 |
| 4. Pi 实机 | ❌ 未做 | 无 Xorg/硬件 OpenGL 验证 |
| 5. ESP 板实机 | ❌ 未做 | 桥板与模块固件未接 |
| 6. 环境实测 | ❌ 未做 | 热点不转发 WAN、worker UID 均未实测 |

**只有前两层。** 因此 `docs/devdocs/04-dev-tasks.md` §7 的 19 条 P0 完成条件
**一条都不能勾选**，本 PR 也不代表 P0 验收通过。

---

## 1. 这个 PR 包含什么

三块内容，按合入顺序：

| 来源 | 内容 |
|---|---|
| 五个原有 PR | `pbr-p0-delivery`、`pbr-t03-truststore`、`pbr-secure-v1-rev3-tag-notation`、`pbr-t07-acceptance-tests`、`z-p0-security` |
| `feat/flite-voicebank` | 语音库（已在 main，本 PR 做了冲突合并） |
| 本轮新写 | 补齐功能缺口 + 模块页数据链路修复 + 演示工具 + 文档 |

原有 PR 的包含关系已逐条用 `git merge-base --is-ancestor` 验证，
详见 `docs/devdocs/10-pr-closure-notes.md`。

---

## 2. 功能更新

### 2.1 PBR 安全链路（报告 §6.1 / §9.1 / §9.2）

| 能力 | 实现 | 位置 |
|---|---|---|
| AEAD 信封 | ChaCha20-Poly1305，`env_ver ‖ key_id ‖ nonce ‖ ct ‖ tag` | `src/protocol/pbr_aead.cpp` |
| 握手 | X25519 + HKDF-SHA256，`info="pbr-secure-v1"`，输出 96 字节 | `src/protocol/pbr_handshake.cpp` |
| 重放保护 | `auth_seq` 严格递增；**先验 tag 再验序号** | `src/protocol/pbr_session.cpp` |
| 事件去重 | `event_id` 表，4096 条上限，满了重建会话而非静默丢弃 | 同上 |
| TTL | 必须恰好 3000ms | 同上 |
| 主机身份 | ECDSA P-256 + SHA-256，签名固定 `r‖s` 64 字节 | `src/security/HostIdentity.cpp` |
| 信任库 | 身份登记/吊销/损坏失败关闭 | `src/security/TrustStore.cpp` |
| 卡索引 | raw UID 只派生 HMAC 索引，不落盘 | `src/security/CardMatcher.cpp` |

**官方向量逐字节验证**：`docs/pbr-secure-v1/vectors.json` 的 `envelope` / `handshake` / `keys`
三组全部通过（`pbr_aead_envelope`、`pbr_handshake_derivation`）。

### 2.2 传输层

| 能力 | 实现 |
|---|---|
| LAN 业务面 | `QSslSocket` + `VerifyNone` 握手后比对 SHA-256 指纹，**无明文回退** |
| LAN 证书 CN | 比对 `CN == module_id`（规范 §0.8.1） |
| BLE 两层 | 链路配对 + 应用层 HMAC-SHA256 挑战-响应，未通过不发业务数据 |
| 传输组合 | 四种全合法（业务 lane 与包通道独立决策），含 `fixed_hotspot_lan + hotspot_lan` |
| lane 切换 | 每次 attach 最多 3 次；切换需重新认证；断连清认证 |

### 2.3 用户与存储（报告 §5 / §6 / §7）

| 能力 | 实现 |
|---|---|
| 每用户库 | 一用户一 SQLCipher 文件，PBKDF2-SHA256 派生 KEK |
| §6.3 加固 | `temp_store=MEMORY` / `journal_mode=DELETE` / `secure_delete=ON` |
| 离线恢复码 | 与 PIN 独立的第二条解封路径，容忍常见输入笔误 |
| 审计 | 只记动作不记值；白名单拒写纯数字与含空格文本 |
| 能力互斥 | 健康历史 × 任意联网，双向互斥 |
| 备份 | 个人备份/恢复 + 加密导出，不能跨用户恢复 |
| 任务归属 | 切用户前必须完成/取消任务；迟到事件按代次拒绝 |

### 2.4 模块（报告 §8）

| 能力 | 实现 |
|---|---|
| worker 隔离 | 独立进程 + Unix socket IPC；systemd `DynamicUser` + `PrivateNetwork` |
| 数据接口 | `ModuleDataApi`，语义接口（无 SQL、无 DB 句柄），作用域 = moduleId + instance + 用户 |
| 页面宿主 | 独立 `QQmlEngine` + import 白名单（禁 `Qt.labs.settings`/`WebEngine`/`LocalStorage`） |
| action 转发 | 经 IPC 到 worker，未知动作有界拒绝 |
| 联网门控 | 默认拒绝、有限重试、流量上限 |

### 2.5 新增模块

| 模块 | ID | 说明 |
|---|---|---|
| 啸天红外围界 | 82 | 双路去抖、告警保持、六态互斥、无硬件报未知 |
| 血压读数 | 83 | 边界校验、反序检测、来源区分、多指标一条记录 |

两个模块的 QML 页面**都在模块包内**，未移入宿主 `pi_terminal/qml/`。

---

## 3. 修复的缺陷（19 个）

前 13 个是原有 PR 合并时暴露的，后 6 个是本轮联调时发现的。

### 3.1 合并暴露（13 个）

| # | 缺陷 | 后果 |
|---|---|---|
| 1 | `uidIndexKey` 在 D 是全局符号、Z 在 `namespace security` | 合并后链接失败 |
| 2 | 两侧各建一次 `add_library(terminal_security)` | CMake target 重定义 |
| 3 | SQLCipher 3.4.1 基于 SQLite 3.15.2，用了 `ON CONFLICT DO UPDATE` | `bindCard()` 永远返回 false |
| 4 | `UserDatabase::~UserDatabase() = default` 不调 `close()` | `AuditLog` 悬空，必现 SIGSEGV |
| 5 | `fieldOf()` 解引用 `QHash::end()` | 模块写样本时崩溃 |
| 6 | `INSERT INTO samples` 只绑 9 列 | `source` 永远是默认值 |
| 7 | `ModulePageHost` 根对象没挂进场景 | 模块页空白 |
| 8 | `SessionGate` 在 `--ui-test` 下挡住审计 | GUI 集成测试退出码 3 |
| 9 | `sendData` 发的是 `readOwnRecords` op | worker 业务数据永远失败 |
| 10 | worker 迁移漏了 `send_data`/`send_control`/`data_directory` 三个回调 | 模块回传数据全部丢失 |
| 11 | `bridgeUnavailable` 是 signal，connect 到它等于空操作 | 桥板失联不禁刷卡 |
| 12 | `SlotRouter::setBridgeOnline()` 从未被调用 | `PositionUnknown` 分支永不触发 |
| 13 | `applyModuleConfig` 与 `fromProfile` 对同一组合判定相反 | 配置分叉 |

### 3.2 联调新发现（6 个）

| # | 缺陷 | 后果 | 根因 |
|---|---|---|---|
| 14 | 数据接口名是 `data`，被 `QQuickItem` 自带的 `data`（子项列表）遮蔽 | 模块页 `appendSample`/`historySeries` 全部失效 | QML 属性遮蔽 |
| 15 | 三个模块页声明 `property var sessionModel: null` | 遮蔽注入，页面永远拿到 null | QML 属性遮蔽 |
| 16 | `ModulePageHost` 只取一次容器尺寸 | 容器初始 0 宽时页面停在 0 宽 | 缺持续绑定 |
| 17 | `HotspotLanTransport` 比对 CN 用 `bridge_id`，规范写 `module_id` | **实机必然握手失败** | 字段语义错用 |
| 18 | `FenceState.dataAgeMs` 返回负数 | 新鲜数据被判成"尚无数据" | 页面 clock 落后于心跳 |
| 19 | 围界 `arm`/`disarm` 只改本地状态，不下发固件 | 按了没反应 | 漏了 send_data |

**14 和 15 是同一个根因**，会影响**所有**模块页，不只是新加的两个。
两者各有一个回归测试锁死（`moduleDataApiIsNotShadowedByItemData`、
`sessionModelUpdateReachesPageBinding`）。

**17 是上实机前必须发现的问题**：Pi 侧与规范已经矛盾，实机联调必炸。

### 3.3 破坏性变更

**`data` → `moduleData`**。如果有人在模块页里写了 `data.appendSample(...)`，
需要改成 `moduleData.appendSample(...)`。目前已改：smart-planter、blood-pressure、
xiaotian-fence 三个示例。

---

## 4. 测试详情（主机层）

### 4.1 汇总

```
ctest -E "media_vlc|ui_responsive_touch|ui_highdpi_touch"
→ 100% tests passed out of 47
```

| 套件 | 结果 |
|---|---|
| 宿主 ctest | **47 / 47 通过** |
| 啸天围界模块 | **8 / 8 通过** |
| 血压模块 | **9 / 9 通过** |
| **合计** | **64 / 64** |

被排除的 5 项在 `main` 上同样失败，属环境性：

| 测试 | 原因 |
|---|---|
| `ui_responsive_touch` | xvfb 下 WebGL blocklisted |
| `ui_highdpi_touch` | 同上 |
| `media_vlc_functional` | 无音频/视频设备 |
| `media_vlc_performance` | 同上 |
| `media_vlc_interface` | 同上 |

### 4.2 逐项清单（47 项）

#### PBR 协议与安全（11 项）

| 测试名 | 覆盖 | 关键用例 |
|---|---|---|
| `pbr_aead_envelope` | AEAD 信封 | `envelopeMatchesOfficialVector`（官方向量逐字节）、`binaryPayloadWithNulBytesRoundTrips`、`tamperingFailsAuthentication`、`aadDirectionIsAuthenticated`、`plaintextLimitIsEnforced`、`authSeqZeroAndWrapAreRejected`、`zeroSharedSecretIsRejected`、`unknownEnvelopeVersionIsRejected` |
| `pbr_replay_and_dedup` | 重放/去重/TTL | `sequenceMustStrictlyIncrease`、**`invalidTagDoesNotAdvanceSequence`**（伪造大序号不推进窗口）、`hugeJumpIsRejected`、`rehandshakeBeforeWrapAround`、`duplicateEventIsDetectedOnce`、`fullTableReportsRebuildInsteadOfSilentDrop`、`ttlFieldMustBeExactly3000`、`expiredAndMissingClockAreDistinct` |
| `pbr_handshake_derivation` | 握手密钥派生 | `x25519MatchesOfficialVector`、`hkdfDerivationMatchesOfficialVector`、`lowOrderPointIsRejected`、`constantTimeEqualsRejectsMismatch` |
| `pibridge_protocol` | PBR1 帧格式 | 帧头/CRC16/长度边界 |
| `pbr_slot_router` | 双槽路由 | `basicBindRoute`、`interleavedSlots`、`slotReuseAndLateEvents`、`conflicts`、`bridgeOffline`、`retiredHistoryBound` |
| `pbr_trust_store` | 信任库 | `enrollAndPersist`、`keyIdCollisionReturnsAll`、`revokeSemantics`、**`corruptedStoreFailsClosed`** |
| `pbr_uid_index` | 卡索引 | `knownAnswers`、`lengthPrefixMatters`、`domainLabelAndRawBytes`、`determinismAndDistinctness` |
| `pbr_uid_index_fail_closed` | 无密钥失败关闭 | `withoutKey` 返回 false，不退回明文比较 |
| `uid_index_key_lifecycle` | 索引密钥 | `generatedKeyIs32BytesAnd0600`、`rotationInvalidatesOldIndex`、`nullOutReturnsFalse` |
| `pbr_host_identity` | 主机身份 | `signVerifySuite`、`corruptFiles`、`derConversion` |
| `pbr_security_status` | 安全状态只读接点 | `slotsAreAlwaysTwo`、`trustRecordsFollowFile`、`corruptIdentityIsReported` |

#### 用户、PIN、存储（5 项）

| 测试名 | 覆盖 | 关键用例 |
|---|---|---|
| `user_pin_session` | PIN 与会话 | `pbkdf2MatchesSha256Kat`（SHA-256 KAT）、`productionKdfUsesSha256And32Bytes`、**`iterationCountCannotBeDowngraded`**、`registryNeverStoresPinOrKek`、`wrongPinDoesNotRevealExistence`、`accountLocksAfterFiveFailures`、`changePinRewrapsDbKeyWithoutReencrypting`、`sessionSwitchInvalidatesOldToken`、`lateEventWithOldGenerationIsRejected` |
| `sqlcipher_user_isolation` | 每用户库隔离 | `cipherVersionIsRealSqlCipher`、`wrongKeyIsRejectedWithNotADb`、`databaseFileIsNotPlainSqlite`、`perUserIsolation`、`crossUserOpenIsRefused`、`hardeningPragmasAreActive`、`auditRecordsActionsWithoutSecrets` |
| `login_lock_user_pages` | 锁屏与用户页 | 登录门禁、锁屏需 PIN 而非刷卡 |
| `recovery_code_path` | 离线恢复码 | `generatedCodeIsValidAndUnique`、`formatToleratesCommonTypos`、`recoveryCodeUnlocksTheSameDatabase`、`wrongRecoveryCodeFails` |
| `backup_restore_roundtrip` | 备份恢复 | `createAndRestoreRoundTrips`、`wrongPasswordIsRejected`、**`backupCannotBeRestoredIntoAnotherUser`**、`corruptFileIsRejected` |

#### 刷卡（2 项）

| 测试名 | 覆盖 | 关键用例 |
|---|---|---|
| `card_index_matching` | 卡匹配 | `missingKeyFailsClosed`、`revokedCardNoLongerMatches`、`hexCaseDoesNotMatterBecauseRawBytesAreUsed`、`cardBoundToOthersCannotBeRebound`、`mapFileIsOwnerOnly`、`bindingWindowIsSingleUse`、`bindingWindowExpiresAfter30s` |
| `card_selects_user_only` | §10.2 刷卡只选用户 | `unknownCardMatchesNobody`、`invalidUidLengthIsRejected`、`bridgeUnavailableBlocksCardReads`、**`matchedCardOnlySelectsUser`**、**`rawUidIsNotPersisted`** |

#### 模块与门控（9 项）

| 测试名 | 覆盖 | 关键用例 |
|---|---|---|
| `module_page_engine_isolation` | 模块页隔离 | `hostObjectsAreNotVisibleInModuleEngine`、`pageImportingSettingsFailsToLoad`、`dangerousImportsAreNotAllowed`、**`moduleDataApiIsNotShadowedByItemData`**（新）、**`sessionModelUpdateReachesPageBinding`**（新）、**`pageFollowsContainerResize`**（新）、`customModulePageLoadsAndBecomesVisible` |
| `module_data_api_scope` | 模块数据接口 | `unauthenticatedFailsClosed`、`samplesRoundTripWithoutFillingZeros`、`scopeIsIsolated`、`switchingUserDoesNotLeakData`、`measurementKeepsMultipleMetrics`、`emptyMeasurementIsRejected` |
| `worker_socket_ipc` | worker IPC | `workerLoadsModuleAndAcceptsBusinessData`、`missingLibraryFailsClosed`、`moduleActionReachesWorker` |
| `worker_ipc_contract` | worker 隔离契约 | `launcherUsesSystemctlNotRawProcess`、`unitFileDeclaresRequiredIsolation`（断言 unit 里真有 `DynamicUser=yes`/`PrivateNetwork=yes`/`ProtectSystem=strict` 等 7 条） |
| `module_worker_async` | worker 异步行为 | 并发请求、超时、错误传播 |
| `session_gate_flow` | 登录门禁 | `noUsersMeansCannotLogin`、`lockRequiresPinNotCard`、`loginRefusedWhileTasksUnfinished`、`disabledUserIsHiddenFromList` |
| `business_lane_gate` | 业务 lane | `noSessionRejectsBusinessData`、`switchRequiresReAuthentication`、`atMostThreeSwitchesPerAttach`、`payloadLimitIs512Bytes`、`fixedBleWithHotspotPackageIsValid` |
| `business_data_security` | 业务面认证 | `bleRejectsBusinessDataBeforeAuthentication`、`bleChallengeResponseComputesHmacSha256`、**`lanRefusesToConnectWithoutPinnedFingerprint`**、`lanRejectsWhenNoCertificatePresented`、`profileWithoutPinCannotStartLanSession` |
| `network_master_switch` | 联网总开关 | `defaultIsDisabled`、`denialExplainsWhy`、`disablingDropsInFlightRequests`、`requestCounterNeverGoesNegative`、`timeSyncDenialIsGraceful` |

#### 权限与供电（2 项）

| 测试名 | 覆盖 | 关键用例 |
|---|---|---|
| `permission_mutual_exclusion` | 能力互斥 | `healthAndInternetAreMutuallyExclusive`、`internetThenHealthIsAlsoRejected`、`exclusionIsPerModule`、`revokeAllClearsEveryModule`、`denialReasonDoesNotLeakTheOtherCapability` |
| `power_status_semantics` | 供电语义 | `defaultIsUnknown`、**`unknownPowerForbidsAutoShutdown`**、`invalidPercentIsDropped`、`lostTelemetryReturnsToUnknown` |

#### 其它（18 项）

| 测试名 | 覆盖 |
|---|---|
| `arkpets_local_interaction` | 桌宠本机交互 |
| `arkpets_manager_material_list_detail` | 桌宠素材列表 |
| `arkpets_worker_safety` | 桌宠 worker 安全 |
| `arkpets_sprite_packing` | 精灵图打包 |
| `text_editor_format_roundtrip` | 文本编辑器排版往返 |
| `music_playback_transactions` | 音乐播放事务 |
| `music_native_api_requests` | 音乐原生 API |
| `music_http_seek_clock` | HTTP seek 时钟 |
| `runtime_models_state_files` | 运行时模型与状态文件 |
| `startup_splash_lifecycle` | 启动遮罩生命周期 |
| `time_tools_page` | 时间工具页 |
| `media_video_player_interface` | 视频播放器接口 |
| `plugin_scale_geometry` | 插件缩放几何 |
| `shared_runtime_install` | 共享运行时安装 |
| `signed_session_integration` | 签名会话端到端（GUI） |
| `media_generate_fixture` | 媒体测试素材生成 |
| `media_browser_joystick_interface` | 浏览器摇杆接口 |
| `media_browser_context_functional` | 浏览器上下文功能 |

### 4.3 模块自带测试（17 项）

#### 啸天红外围界 8/8

```bash
cmake -S module-devkit/examples/xiaotian-fence -B build \
  -DMODULE_SDK_DIR=<repo>/module-devkit/sdk \
  -DCMAKE_PREFIX_PATH=<Qt6> -DMODULE_TESTING=ON
cmake --build build && cd build && ctest
```

覆盖：去抖（连续 2 帧同值才认可）、告警保持（3 秒窗口）、
触发计数（仅"布防中 + 未遮挡→遮挡"计入）、六态互斥、
心跳过期转未知、去抖未收敛时 `valid=false` 不冒充未遮挡。

#### 血压读数 9/9

```bash
cmake -S module-devkit/examples/blood-pressure -B build \
  -DMODULE_SDK_DIR=<repo>/module-devkit/sdk \
  -DCMAKE_PREFIX_PATH=<Qt6> -DMODULE_TESTING=ON
cmake --build build && cd build && ctest
```

覆盖：收缩压 40–300 / 舒张压 20–200 / 脉搏 20–250 边界、
收缩压 ≤ 舒张压判无效**不交换数值**、空测量明确报 `no_measurement`
不造记录、`device.auto`/`device.manual`/`user.entry` 来源区分、
一次测量三指标存一条记录缺失写 NULL。

---

## 5. 主机模拟演示详情

### 5.1 怎么跑

```bash
# 先构建宿主与两个示例模块
python3 pi_terminal/tools/demo_dual_module.py \
  <build-dir> /tmp/demo-out \
  --fence-so <xiaotian-fence>/libmodule.so \
  --bp-so <blood-pressure>/libmodule.so
```

脚本会：建管理员 → 起模拟桥板（真 PTY，双槽）→ 起两个模块业务端点
（真 TLS，各自自签证书）→ 打包签名 → 起终端 → 自动登录 → 双槽插模块
→ 上报电池 → 刷卡 → 打开啸天页面 → 截图。

### 5.2 演示覆盖的真实链路

| 环节 | 是否真实 | 说明 |
|---|---|---|
| PBR1 帧 / PTY | ✅ | 真 Linux 伪终端，不是内存桩 |
| 双槽事件 | ✅ | `slot_id` + `device_id` + `binding_generation` 四元组 |
| 热点租约 | ⚠️ 注入 | xvfb 无 NetworkManager，`--demo` 注入虚拟租约 |
| TLS 握手 | ✅ | 真 `QSslSocket` + 自签证书 |
| 指纹固定 | ✅ | SHA-256(证书 DER) 精确比对 |
| 包下载 | ✅ | 真 HTTP + 分段传输 |
| 签名验证 | ✅ | ECDSA 验签 + 描述符兼容性校验 |
| 内置 manifest 比对 | ✅ | `.so` 内嵌 manifest 与包内等价 |
| action 下发 | ✅ | 经 IPC 到模块，再经 TLS 到固件模拟 |
| 数据回传 | ✅ | 386 字节 payload |
| 模块页渲染 | ✅ | 模块包自带 QML，独立引擎 |

### 5.3 实测日志

```
TLS handshake completed; protocol = 12
TLS peer CN = 82  fp = ecf87f1b...  expected fp = ecf87f1b...   ← 精确匹配
Session handshake selected: fence-slot0 hotspot_lan
82 fence-slot0 downloading_package → verifying_package → loading_plugin → ready
83 bp-slot1    downloading_package → verifying_package → loading_plugin → ready
lane switch required: fence-slot0 hotspot_lan
business lane switched to hotspot_lan count 1
Module data received: module_id= 82 instance= fence-slot0 payload_bytes= 386
```

### 5.4 界面实测结果

模块页显示（截图 `/tmp/opencode/demo-out/shot.png`）：

| 字段 | 值 |
|---|---|
| 主状态 | 已布防 |
| A 路 | 遮挡 |
| B 路 | 未遮挡 |
| 本次启动后触发 | 1 |
| 数据 | 新鲜 |
| 供电 | 外部供电 82% |

这些值全部来自模块回传的真实 payload，不是硬编码。

---

## 6. 新增工具

| 文件 | 用途 |
|---|---|
| `pi_terminal/tools/mock_bridge_multi.py` | 单桥板 + 双槽 + 电池遥测 + 刷卡的 PBR1 模拟 |
| `pi_terminal/tools/demo_dual_module.py` | 端到端演示脚本（打包、TLS、截图一条龙） |
| `pi_terminal/tools/mock_module.py` | 扩展：82/83 模块持续上报（去抖与心跳需要） |

### 模拟器踩过的坑（写给后面的人）

1. 模块 `.so` 内嵌 manifest 必须与包内 manifest **等价**，否则
   `embedded manifest/ABI differs`。改 manifest 要连带重建 `.so`。
2. 包的 `arch` 必须等于 `uname -m`，否则 `descriptor incompatible`。
3. LAN 业务面是 TLS，profile 必须带 `pin_sha256`，证书 `CN = <module_id>`。
4. 围界要去抖：**必须持续上报**，只回一帧会一直显示"状态未知"。
5. Pi 切 lane 会**重建连接**，action 不会重放；模拟固件要连上就上报。

---

## 7. 文档更新

| 文件 | 内容 |
|---|---|
| `docs/devdocs/08-handoff-to-L.md` | 给 L 的硬件任务清单（接口字段、验收标准、优先级） |
| `docs/devdocs/09-debug-hooks.md` | **调试钩子与自动化入口清单**（CLI 参数、环境变量、模拟器、模块页调试常见问题） |
| `docs/devdocs/10-pr-closure-notes.md` | 七个 PR 的包含关系验证与关闭说明 |
| `docs/devdocs/11-changelog-and-tests.md` | 本文 |
| `TECHNICAL-ADVISORY-REPORT.docx` | 19 项 P0 条件逐条核对 |
| 技术说明文档 | §5/§9 插件隔离改为 worker，新增 §10–§14 |

---

## 8. 未完成与风险

### 8.1 明确未做

- **全部实机验证（六层证据的后四层）**
- ESP 侧实现：PBR AEAD、RC522 事件、NVS 密钥、双槽电气、LAN TLS server
- 啸天双探头采购与固件
- 血压计选型与协议适配

### 8.2 已知风险

| 风险 | 说明 |
|---|---|
| `data` → `moduleData` 破坏性变更 | 已有模块页需同步改名 |
| LAN TLS CN 字段 | Pi 侧已改 `module_id`，**L 侧固件必须对齐**，否则实机握手失败 |
| SQLCipher 版本 | 3.4.1 基于 SQLite 3.15.2，不支持 UPSERT；新代码不要写 `ON CONFLICT DO UPDATE` |
| worker 降级路径 | 非 systemd 环境会降级为同进程 worker，隔离强度下降（有明确日志标记） |
| 演示依赖注入 | `--demo` 会注入虚拟热点与自动信任，**生产不得使用** |

### 8.3 上实机前必须做的对齐

1. L 侧实现 PBR AEAD 并用 `vectors.json` 做 KAT
2. LAN TLS 证书 `CN = <module_id>` 与 `pin_sha256` 登记流程
3. BLE NVS 密钥下发与两层认证
4. 双槽电气独立性实测（T-06 前置门）
5. 在 Pi 上复跑 SQLCipher 版本与错误 key 行为
6. 实测 worker UID ≠ 宿主 UID
7. 实测热点不转发 WAN

具体命令见 `docs/devdocs/08-handoff-to-L.md` §10。

---

## 9. 复现本文所有数字

```bash
# 1. 构建（参数见项目 README 或 08-handoff-to-L.md）
cmake -S pi_terminal -B build ...

# 2. 宿主全量测试
cd build && ctest -E "media_vlc|ui_responsive_touch|ui_highdpi_touch"
# 期望：100% tests passed out of 47

# 3. 模块测试
cmake -S module-devkit/examples/xiaotian-fence -B b1 \
  -DMODULE_SDK_DIR=$PWD/module-devkit/sdk -DCMAKE_PREFIX_PATH=$QT_ROOT -DMODULE_TESTING=ON
cmake --build b1 && (cd b1 && ctest)     # 期望：8/8

cmake -S module-devkit/examples/blood-pressure -B b2 \
  -DMODULE_SDK_DIR=$PWD/module-devkit/sdk -DCMAKE_PREFIX_PATH=$QT_ROOT -DMODULE_TESTING=ON
cmake --build b2 && (cd b2 && ctest)     # 期望：9/9

# 4. 端到端演示
python3 pi_terminal/tools/demo_dual_module.py build /tmp/demo-out \
  --fence-so b1/libmodule.so --bp-so b2/libmodule.so
```
