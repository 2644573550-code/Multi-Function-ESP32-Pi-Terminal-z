#pragma once

#include <QByteArray>

// UID 索引密钥的**唯一**入口。契约由 `F-REVIEW-2.md` §5.3.2 DR-14 冻结，
// 签名**不带 namespace**（原样：`bool uidIndexKey(QByteArray *out32);`）。
//
// 归属：**Z 实现**（T-04 / T-10 可信存储域）。D 的 `deriveUidIndex()` 只调用，
// 不接触、不缓存、不落盘密钥字节。
//
//   - 成功：写出 32 字节到 `out32` 并返回 true。
//   - 密钥不可用（未生成 / 损坏 / 权限不足）：返回 false，并且**不得**
//     写出部分数据、不得返回全零密钥。
//   - 位置：整机单钥，`AppConfigLocation/uid_index.key`，0600。
//     理由（§0.6）：刷卡的用途是选用户，发生在登录之前，不能放每用户 DB。
bool uidIndexKey(QByteArray *out32);

// 首次绑卡前调用：缺失时生成 32 字节并落盘。已存在则原样保留。
bool ensureUidIndexKey();

// 轮换会让所有旧 UID 索引失效，须重新绑卡；不静默迁移。
// 失败时不破坏现有密钥。
bool rotateUidIndexKey();
