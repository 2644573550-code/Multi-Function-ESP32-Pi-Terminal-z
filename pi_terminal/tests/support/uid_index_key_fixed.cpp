#include "security/uid_index_key.h"

#include <QByteArray>

// 测试用密钥提供者：替换正式的 `uid_index_key_default.cpp`，只在测试
// 目标里编译。可开关，用来同时验证"有密钥"和"无密钥失败关闭"两条路径。

namespace {
QByteArray g_key;
}

void setTestUidIndexKey(const QByteArray &key)
{
    g_key = key;
}

void clearTestUidIndexKey()
{
    g_key.fill('\0');
    g_key.clear();
}

bool uidIndexKey(QByteArray *out32)
{
    if (g_key.size() != 32) {
        if (out32) out32->clear();
        return false;
    }
    if (out32) *out32 = g_key;
    return true;
}
