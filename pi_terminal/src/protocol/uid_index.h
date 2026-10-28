#pragma once

#include <QByteArray>

// UID → 用户索引派生（签认冻结，见 `F-REVIEW-2.md` §5.2 T-08 与
// `pbr-secure-v1` 的上游 `04-dev-tasks-f-day1.md` §3）。
//
//     index = HMAC-SHA256(key, ASCII("p0-rfid-index-v1") || u8(uid_len) || rawUid)
//
// 契约（**签名已冻结，两侧不得各自加参数**）：
//   - 输入是**原始 UID 字节**，长度只能是 4 / 7 / 10；不是 hex 文本，
//     不做大小写折叠（大小写折叠在校验器里做，见 `PbrControl`）。
//   - 输出 32 字节。
//   - 长度非法返回**空** QByteArray；调用方按 `INVALID_MESSAGE` 处理。
//     不得返回全零、不得截断、不得退化成"直接用 UID"。
//   - 密钥唯一入口是 `uidIndexKey()`（由 Z 实现）。该函数返回 false 时
//     本函数同样返回空 —— **密钥缺失时整条 RFID 路径失败关闭**，
//     不匹配任何用户，绝不退回明文 UID 比较。
//   - raw UID 只在本次调用内存在，不落盘、不进日志、不缓存索引以外的形式。
QByteArray deriveUidIndex(const QByteArray &rawUid);
