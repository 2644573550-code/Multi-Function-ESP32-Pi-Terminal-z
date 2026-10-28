#pragma once

#include <QByteArray>

// UID 索引密钥的**唯一**入口（契约由 `F-REVIEW-2.md` §5.2 T-08 冻结）。
//
// 归属：**Z 实现**（T-04 / T-10 可信存储域）。D 只调用，不接触、不缓存、
// 不落盘密钥字节。
//
//   - 成功：写出 32 字节到 `out32` 并返回 true。
//   - 密钥不可用（未生成 / 损坏 / 权限不足）：返回 false，并且**不得**
//     写出部分数据、不得返回全零密钥。
//   - 位置由 Z 定：整机单钥，`AppConfigLocation/uid_index.key`，0600。
//     理由（Z-6）：刷卡的用途是选用户，发生在登录之前，不能放每用户 DB。
//
// 本头文件只声明契约。D 侧提供一个 fail-closed 的默认实现
// （`uid_index_key_default.cpp`）：始终返回 false。Z 交付真实实现时
// 替换该翻译单元即可，`deriveUidIndex()` 无需改动。
bool uidIndexKey(QByteArray *out32);
