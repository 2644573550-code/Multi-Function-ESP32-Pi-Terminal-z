#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

#include "security/CardIndex.h"

namespace security {


class UserDatabase;

// 卡↔用户映射的存放位置。
//
// 原方案把映射放进各用户 SQLCipher 库（§0.6 原文后半句），结果是死锁：
// 开库要先有该用户的 DB key，而未登录时拿不到任何 key —— 于是"刷卡选用户"
// 在最需要它的场景（无人登录）下永远返回 NoMatch。
//
// 现方案：整机一张索引表，AppConfigLocation/uid_map.json，0600。
//   - 只存 HMAC 索引 → userId，不存 raw UID，也不存任何 key
//   - 索引不可反推 UID；代价与已接受的"整机单钥"同源（§0.6）
//   - 刷卡时只读这一张表，不再逐库试开（消掉 N×1.6 秒阻塞）
//   - uid_index_key 轮换后旧索引自然全部失配，需重新绑卡
//
// user_id 仍只由加密库里的数据决定归属；映射表只回答"这张卡登记给谁"。
class CardIndex {
public:
    // mapPath 为空时用 AppConfigLocation/uid_map.json。
    explicit CardIndex(QString mapPath = {});

    // rawUid 只在本函数内存活，不落盘、不记录。
    // deriveUidIndex 为 D 提供的函数，通过 derive 回调注入以便测试。
    using Derive = QByteArray (*)(const QByteArray &rawUid);

    // 未登录亦可调用：只读整机映射表，不需要任何 DB key。
    CardMatch findOwner(const QByteArray &rawUid, Derive derive = nullptr);

    // 绑卡：要求已登录 + 窗口打开 + 重验 PIN 由调用方完成。
    bool bind(const QString &userId, const QByteArray &rawUid, Derive derive = nullptr);
    bool revoke(const QString &userId, const QByteArray &rawUid, Derive derive = nullptr);

    // 挂失/停用该用户的全部卡。返回撤销条数。
    int revokeAll(const QString &userId);

    QString path() const { return mapPath_; }

private:
    struct Entry {
        QByteArray index;   // HMAC(uidIndexKey, "p0-rfid-index-v1" || len || uid)，32 字节
        QString userId;
        qint64 boundAtMs = 0;
        bool revoked = false;
    };

    static QString defaultMapPath();
    QVector<Entry> load() const;
    bool save(const QVector<Entry> &entries) const;

    QString mapPath_;
};

}  // namespace security
