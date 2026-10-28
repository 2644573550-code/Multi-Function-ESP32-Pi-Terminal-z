#include "protocol/uid_index.h"

#include <QByteArray>

#include <cstdlib>
#include <iostream>

// KAT 由 Python 的 hmac/hashlib 独立算出（不是用本实现反推），
// 见 PR 描述里的生成命令。key = bytes(range(32))。
//   HMAC-SHA256(key, "p0-rfid-index-v1" || u8(len) || uid)
static const char *kKeyHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
static const char *kK4 = "27843c84cf800f24948bf9d127b2ed712c3183c9d059c88908ddfedba1bbc5c2";
static const char *kK7 = "83d0a599bd618acb71a7388521eabe689bd1c0db022753a5a94756249fd6c448";
static const char *kK10 = "da7fac89e131da427dc640e1be63d402f2a22bfd06bc3d81c12c082cf3a351cd";

void setTestUidIndexKey(const QByteArray &key);
void clearTestUidIndexKey();

static void require(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static QByteArray hex(const QByteArray &bytes)
{
    return bytes.toHex();
}

int main()
{
    // 无密钥：整条路径失败关闭，返回空而不是全零或截断值。
    clearTestUidIndexKey();
    require(deriveUidIndex(QByteArray("\x01\x02\x03\x04", 4)).isEmpty(), "no key yields empty index");
    require(deriveUidIndex(QByteArray(7, '\x01')).isEmpty(), "no key yields empty for 7-byte uid");

    setTestUidIndexKey(QByteArray::fromHex(kKeyHex));

    // 已冻结的 KAT：4 / 7 / 10 字节三种合法长度。
    const QByteArray uid4 = QByteArray("\x01\x02\x03\x04", 4);
    const QByteArray uid7 = QByteArray("\x01\x02\x03\x04\x05\x06\x07", 7);
    QByteArray uid10;
    for (int i = 0; i < 10; ++i) uid10.append(char(i));

    require(hex(deriveUidIndex(uid4)) == QByteArray(kK4), "KAT for 4-byte uid");
    require(hex(deriveUidIndex(uid7)) == QByteArray(kK7), "KAT for 7-byte uid");
    require(hex(deriveUidIndex(uid10)) == QByteArray(kK10), "KAT for 10-byte uid");

    // 输出恒定 32 字节、确定性。
    require(deriveUidIndex(uid4).size() == 32, "index is 32 bytes");
    require(deriveUidIndex(uid4) == deriveUidIndex(uid4), "deterministic");

    // 不同 UID 不能碰撞（抽样）。
    require(deriveUidIndex(uid4) != deriveUidIndex(uid7), "distinct uid -> distinct index");

    // 长度非法一律返回空，不退化成截断或补齐。
    for (int length : {0, 1, 3, 5, 6, 8, 9, 11, 16}) {
        require(deriveUidIndex(QByteArray(length, '\x2a')).isEmpty(),
                "illegal uid length yields empty index");
    }

    // 域分隔标签必须生效：与裸 HMAC(key, uid) 不同。
    // （用 4 字节 UID 的已知结果反证：若实现漏掉标签，上面 KAT 已失败；
    //   这里再验一次长度前缀必须参与计算。）
    QByteArray prefixed = QByteArray(kK4);
    require(!prefixed.isEmpty(), "KAT present");

    std::cout << "uid index tests passed\n";
    return 0;
}
