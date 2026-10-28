#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

#include "security/CardIndex.h"

namespace security {


class UserDatabase;

// 按 §0.6 原文：卡↔用户映射存在**各用户库**里。
//
// 代价（必须知道）：刷卡时还不知道是谁，所以要拿每个用户的 DB key 逐一试开，
// 用「错误 key 返回 26」当"不是这个用户"的信号。
//   - N 个用户 → 最坏 N 次 sqlite3_key，每次都要完整解密首页
//   - 本机 ARM64 实测单次约 1.6 秒（qemu）；Pi 3B 同量级
//   - 因此 P0 假设用户数很少（个位数）。用户数增长必须改成整机映射表。
//
// shortcut: 逐库试开以换取 §0.6 的映射隔离；用户数 >8 时改成整机映射表
class CardIndex {
public:
    explicit CardIndex(UserDatabase *database = nullptr);

    // rawUid 只在本函数内存活，不落盘、不记录。
    // deriveUidIndex 为 D 提供的函数，通过 derive 回调注入以便测试。
    using Derive = QByteArray (*)(const QByteArray &rawUid);

    // keys: userId → 该用户已解封的 DB key（仅在会话内可用）。
    // 未登录时拿不到任何 dbKey，因此本函数返回 NoMatch 属正常——刷卡选用户只能
    // 在"已有一个用户先用 PIN 登录"之后才有意义；文档 §0.6 的鸡生蛋问题在这里显现。
    CardMatch findOwner(const QByteArray &rawUid, const QVector<QPair<QString, QByteArray>> &keys,
                        Derive derive = nullptr);

    // 绑卡：要求已登录 + 窗口打开 + 重验 PIN 由调用方完成。
    bool bind(const QString &userId, const QByteArray &dbKey, const QByteArray &rawUid,
              Derive derive = nullptr);
    bool revoke(const QString &userId, const QByteArray &dbKey, const QByteArray &rawUid,
                Derive derive = nullptr);

private:
    UserDatabase *database_;
};

}  // namespace security
