# 安全状态界面接点（给 Z 的接入说明 + 界面审计结果）

| 项 | 内容 |
|---|---|
| 对象 | `SecurityStatus`（`src/core/SecurityStatus.{h,cpp}`） |
| QML 名 | `securityStatus`（`TerminalApplication.cpp` 里的 `setContextProperty`） |
| 归属 | D 侧**只读接点**。T-09（登录/绑卡/锁屏/挂失）的页面属 Mr.Z，本文件不碰 |
| 预览 | `security_preview`（独立程序，只加载 `qml/SecurityStatusPage.qml`） |

## 0. 为什么是「接点」而不是「页面」

`04-dev-tasks-api-ui.md` 的 T-09 主责是 Mr.Z，文件范围含 `qml/Main.qml`、
`ModuleCardModel.*`、`PageProvider.*` 与新增的登录/锁屏/绑卡组件。
D 若直接写这些页面，两个人会改同一批文件。所以 D 只提供**数据接点**，
页面由 Z 按 T-09 接；`qml/SecurityStatusPage.qml` 是**参考实现 + 预览用**，
不是产品入口（它没有加进 `Main.qml` 的任何导航路径）。

## 1. 暴露的接口

### 只读属性

| 属性 | 类型 | 含义 |
|---|---|---|
| `trustRecords` | `QVariantList` | 信任记录，见 §1.1 |
| `trustStoreHealthy` | `bool` | 信任库能否解析。**false ≠ 没有配对过** |
| `trustStorePath` | `QString` | 文件路径，便于排查 |
| `identityFingerprint` | `QString` | 本机公钥指纹，8 字节 → 16 位小写 hex |
| `identityKeyId` | `int` | `SHA-256(pub)[0]`，0–255 |
| `identityState` | `QString` | `ready` / `missing` / `corrupt` / `unavailable` |
| `identityPath` | `QString` | 私钥文件路径 |
| `uidKeyReady` | `bool` | UID 索引密钥是否就绪 |

### 方法

| 方法 | 说明 |
|---|---|
| `slotSnapshot()` | 槽位快照，固定 2 条（槽 0/1）。**快照**，不是属性，页面自己在需要时调用 |
| `slotStats()` | 判定计数：`accepted` / `rejected` / `lastRejection` |
| `refresh()` | 手动重读（文件监听之外的兜底） |

### 1.1 `trustRecords` 每条字段

| 字段 | 类型 | 说明 |
|---|---|---|
| `bridgeId` | `QString` | 桥板标识 |
| `fingerprintHex` | `QString` | 16 位 hex |
| `keyId` | `int` | 0–255 |
| `keyIdText` | `QString` | 2 位小写 hex，与线上字段一致 |
| `enrolledAt` / `revokedAt` | `qint64` | 毫秒；`revokedAt == 0` 表示仍有效 |
| `active` | `bool` | `revokedAt == 0` |

### 1.2 `slotSnapshot()` 每条字段

| 字段 | 说明 |
|---|---|
| `slot` | 0 或 1 |
| `bound` | 是否绑定 |
| `bridgeId` / `deviceId` / `generation` / `instance` / `boundAt` | 仅在 `bound == true` 时存在 |
| `bridgeOnline` | 该桥板是否在线（未登记过的桥板视为在线） |

## 2. 语义约定（最容易做错的地方）

1. **`trustStoreHealthy == false` 必须报警**，不能显示成「还没有配对」。前者是数据损坏，
   所有桥板都会被拒；后者只是还没配过。
2. **`identityState == "corrupt"` ≠ `"missing"`**。前者要提示用户检查文件/重新生成，
   后者只是首次使用。
3. **`uidKeyReady == false` ≠ 「没有这个用户」**。密钥没下发时所有刷卡都匹配不上，
   但那是配置问题，不是用户不存在。
4. **槽位是位置，不是身份**。`bound == false` 不代表业务结束——触点拔出后
   认证/授权仍有效的无线任务会继续（T-05 语义）。
5. **不要用 `keyId` 判定身份**。1 字节碰撞面 1/256，它只用于选钥；
   真正的身份判定必须用完整公钥（§2.1）。

## 3. QML 用法示例

```qml
Text {
    text: securityStatus.trustStoreHealthy
          ? qsTr("%1 个已配对桥板").arg(securityStatus.trustRecords.length)
          : qsTr("信任库损坏")
    color: securityStatus.trustStoreHealthy ? theme.text : theme.error
}

Repeater {
    model: securityStatus.slotSnapshot()
    delegate: Text {
        text: modelData.bound
              ? qsTr("槽 %1：%2").arg(modelData.slot).arg(modelData.deviceId)
              : qsTr("槽 %1：空闲").arg(modelData.slot)
    }
}
```

## 4. 界面审计结果（前面做完的四块）

逐个组件的公开状态 → 是否「用户/运维应当看到」→ 接点是否覆盖。

| 组件 | 状态 | 是否该显示 | 覆盖情况 |
|---|---|---|---|
| `TrustStore` | 记录列表（含已撤销） | 是 | ✅ `trustRecords` |
| `TrustStore` | 库损坏（`corrupted_`） | **是（关键）** | ✅ `trustStoreHealthy`——**审计发现此前无公开访问器，已补 `healthy()`** |
| `TrustStore` | 同 `key_id` 多条候选 | 是 | ✅ 列表本身即体现（不折叠） |
| `HostIdentity` | 未生成 / 就绪 / 损坏 | 是 | ✅ `identityState` 四态 |
| `HostIdentity` | 公钥指纹、`key_id` | 是 | ✅ |
| `HostIdentity` | 私钥内容 | **否** | ✅ 未暴露 |
| `SlotRouter` | 槽 0/1 绑定、代次、设备 | 是 | ✅ `slotSnapshot()` |
| `SlotRouter` | 桥板离线（位置 unknown） | 是 | ⚠️ 部分：仅已绑定时给 `bridgeOnline`，未绑定槽无桥板信息 |
| `SlotRouter` | 迟到/串槽事件被拒的次数 | 是（运维排查） | ✅ `slotStats()`（accepted / rejected / lastRejection） |
| `uid_index` | 密钥就绪 | 是 | ✅ `uidKeyReady` |
| `uid_index` | raw UID / 用户映射 | **否** | ✅ 未暴露 |

### 审计结论

- **已修 1 处**：`TrustStore` 的损坏状态此前没有公开 API，页面无法区分
  「空」与「损坏」，会把数据损坏显示成「还没配对」——已补 `healthy()`。
- **1 处部分覆盖**：未绑定槽的桥板在线状态。因为 `bridge_id` 要等 T-03 配对后
  才确定，未绑定时没有归属桥板可问。
- **已补 1 处缺口**：`SlotRouter` 拒绝迟到/串槽事件时原先只写日志、没有计数，
  运维遇到「刷卡没反应」无法判断是不是被当成迟到事件丢了。现已在
  `SlotRouter` 加 `acceptedCount()` / `rejectedCount()` / `lastRejection()`，
  经 `ConnectionManager::slotStats()` 与 `SecurityStatus::slotStats()` 暴露到界面。
  计数器是**纯加法**（不改变任何判定结果），对已提交的 T-05 无行为影响。

## 5. 明确不做的部分

- 登录 / 锁屏 / 绑卡 / 挂失的页面与流程（T-09，Z 主责）。
- 任何写操作（登记/撤销信任、生成身份）。接点是只读的；
  写入方是 Z 的 T-09b 或 T-07 的配对流程，他们写文件即可，
  本接点通过文件监听自动更新。
- raw UID、私钥、会话密钥、数据库路径：一律不暴露。

## 6. 怎么自己看效果

```bash
cmake -S pi_terminal -B build -DBUILD_TESTING=ON [-DVLC_ROOT=...]
cmake --build build --target security_preview
./build/security_preview      # 需要一个图形环境（Wayland/X11）
```

也可以用 `security_status_test` 在无头环境验证数据正确性。
