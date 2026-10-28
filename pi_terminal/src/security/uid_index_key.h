#pragma once
#include <QByteArray>

namespace security {


// 整机单钥，供 D 的 deriveUidIndex() 取 HMAC 密钥。
// 位置：AppConfigLocation/uid_index.key，0600。
// 不放 AppLocalDataLocation —— 那是 ModuleManager::dataDirectory() 交给模块的目录。
// 签名由 F-REVIEW-2 §5.3.2 Z-6 / DR-14 冻结，两侧不得各自加参数。
bool uidIndexKey(QByteArray *out32);   // 成功 true；密钥不可用时 false

// 首次绑卡前调用：缺失时生成 32 字节并落盘。已存在则原样保留。
bool ensureUidIndexKey();

// 轮换会让所有旧 UID 索引失效，须重新绑卡；不静默迁移。
// 失败时不破坏现有密钥。
bool rotateUidIndexKey();

}  // namespace security
