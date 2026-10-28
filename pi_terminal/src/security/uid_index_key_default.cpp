#include "security/uid_index_key.h"

// D 侧提供的 fail-closed 默认实现。
//
// 真实密钥由 Z 实现（契约见 `uid_index_key.h`）。在 Z 交付之前，这里
// 永远返回 false —— 于是 `deriveUidIndex()` 永远返回空，RFID 用户匹配
// 永远不命中。这是**有意的**：宁可刷卡无反应，也不能在没有密钥的
// 情况下退化成明文 UID 比较。
//
// Z 替换本文件时：保持签名不变，不要在调用点加参数。
bool uidIndexKey(QByteArray *out32)
{
    if (out32) out32->clear();
    return false;
}
