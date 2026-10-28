#include "protocol/uid_index.h"

#include <QByteArray>
#include <QSet>

#include <cstdlib>
#include <iostream>

// KAT 由 Python 的 hmac/hashlib 独立算出（不是用本实现反推），
// 生成命令见 PR 描述。key = bytes(range(32))。
//   HMAC-SHA256(key, "p0-rfid-index-v1" || u8(len) || uid)
static const char *kKeyHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
static const char *kK4 = "27843c84cf800f24948bf9d127b2ed712c3183c9d059c88908ddfedba1bbc5c2";
static const char *kK7 = "83d0a599bd618acb71a7388521eabe689bd1c0db022753a5a94756249fd6c448";
static const char *kK10 = "da7fac89e131da427dc640e1be63d402f2a22bfd06bc3d81c12c082cf3a351cd";

void setTestUidIndexKey(const QByteArray &key);
void clearTestUidIndexKey();

static int g_checks = 0;

static void require(bool ok, const char *message)
{
    ++g_checks;
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static QByteArray bytesOfLength(int length, quint8 fill)
{
    return QByteArray(length, char(fill));
}

static QByteArray pseudoRandom(int length, quint32 seed)
{
    QByteArray out(length, '\0');
    quint32 state = seed * 2654435761u + 7u;
    for (int i = 0; i < length; ++i) {
        state = state * 1664525u + 1013904223u;
        out[i] = char((state >> 13) & 0xff);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 1. 密钥缺失 → 失败关闭
// ---------------------------------------------------------------------------
static void withoutKey()
{
    clearTestUidIndexKey();
    for (int length : {4, 7, 10}) {
        require(deriveUidIndex(bytesOfLength(length, 0x01)).isEmpty(),
                "no key: every legal length yields an empty index");
    }
    require(deriveUidIndex(QByteArray()).isEmpty(), "no key: empty uid also empty");
}

// ---------------------------------------------------------------------------
// 2. 冻结的 KAT
// ---------------------------------------------------------------------------
static void knownAnswers()
{
    setTestUidIndexKey(QByteArray::fromHex(kKeyHex));

    const QByteArray uid4 = QByteArray("\x01\x02\x03\x04", 4);
    const QByteArray uid7 = QByteArray("\x01\x02\x03\x04\x05\x06\x07", 7);
    QByteArray uid10;
    for (int i = 0; i < 10; ++i) uid10.append(char(i));

    require(deriveUidIndex(uid4).toHex() == QByteArray(kK4), "KAT for 4-byte uid");
    require(deriveUidIndex(uid7).toHex() == QByteArray(kK7), "KAT for 7-byte uid");
    require(deriveUidIndex(uid10).toHex() == QByteArray(kK10), "KAT for 10-byte uid");

    require(deriveUidIndex(uid4).size() == 32, "index is 32 bytes");
    require(deriveUidIndex(uid7).size() == 32, "index is 32 bytes (7)");
    require(deriveUidIndex(uid10).size() == 32, "index is 32 bytes (10)");
}

// ---------------------------------------------------------------------------
// 3. 密钥开关（Z 交付前后两种状态都要对）
// ---------------------------------------------------------------------------
static void keyToggle()
{
    clearTestUidIndexKey();
    const QByteArray uid = QByteArray("\x01\x02\x03\x04", 4);
    require(deriveUidIndex(uid).isEmpty(), "key cleared: empty");

    setTestUidIndexKey(QByteArray::fromHex(kKeyHex));
    require(deriveUidIndex(uid).toHex() == QByteArray(kK4), "key restored: KAT again");

    // 错误长度的密钥必须被拒绝，不能"凑合"。
    setTestUidIndexKey(QByteArray(31, '\x01'));
    require(deriveUidIndex(uid).isEmpty(), "31-byte key rejected");
    setTestUidIndexKey(QByteArray(33, '\x01'));
    require(deriveUidIndex(uid).isEmpty(), "33-byte key rejected");
    setTestUidIndexKey(QByteArray());
    require(deriveUidIndex(uid).isEmpty(), "empty key rejected");
    setTestUidIndexKey(QByteArray(32, '\0'));
    require(deriveUidIndex(uid).size() == 32, "all-zero 32-byte key still usable (key material is caller's problem)");

    setTestUidIndexKey(QByteArray::fromHex(kKeyHex));
}

// ---------------------------------------------------------------------------
// 4. 长度规则：只接受 4 / 7 / 10
// ---------------------------------------------------------------------------
static void illegalLengths()
{
    setTestUidIndexKey(QByteArray::fromHex(kKeyHex));
    for (int length = 0; length <= 16; ++length) {
        const bool legal = length == 4 || length == 7 || length == 10;
        const QByteArray index = deriveUidIndex(bytesOfLength(length, 0x2a));
        if (legal) require(index.size() == 32, "legal length produces 32 bytes");
        else require(index.isEmpty(), "illegal length produces empty");
    }
}

// ---------------------------------------------------------------------------
// 5. 长度前缀参与计算：同字节、不同长度 → 不同索引
// ---------------------------------------------------------------------------
static void lengthPrefixMatters()
{
    setTestUidIndexKey(QByteArray::fromHex(kKeyHex));
    const QByteArray four = QByteArray("\x01\x02\x03\x04", 4);
    const QByteArray seven = QByteArray("\x01\x02\x03\x04\x05\x06\x07", 7);
    const QByteArray firstFourOfSeven = seven.left(4);
    require(firstFourOfSeven == four, "same leading bytes");
    require(deriveUidIndex(four) != deriveUidIndex(seven),
            "same leading bytes but different length produce different index");
}

// ---------------------------------------------------------------------------
// 6. 域分隔标签与字节级输入（不做 hex 文本处理）
// ---------------------------------------------------------------------------
static void domainLabelAndRawBytes()
{
    setTestUidIndexKey(QByteArray::fromHex(kKeyHex));
    // 0xAB 0xCD 0xEF 0x01 与 "abcd" 的 ASCII（0x61..0x64）必须是不同输入。
    const QByteArray highBytes("\xab\xcd\xef\x01", 4);
    const QByteArray asciiText("abcd", 4);
    require(highBytes != asciiText, "inputs differ");
    require(deriveUidIndex(highBytes) != deriveUidIndex(asciiText),
            "raw bytes are hashed, not their hex/text form");

    // 大小写"看起来一样"的 UID 是**不同字节**，因此必须得到不同索引；
    // 折叠只发生在校验器（PbrControl）里，不在 deriveUidIndex 里。
    const QByteArray upper = QByteArray("\xAB\xCD\xEF\x01", 4);
    const QByteArray lower = QByteArray("\xab\xcd\xef\x01", 4);
    require(upper == lower, "byte-wise they are identical");
    require(deriveUidIndex(upper) == deriveUidIndex(lower), "identical bytes give identical index");
}

// ---------------------------------------------------------------------------
// 7. 确定性与区分度（批量）
// ---------------------------------------------------------------------------
static void determinismAndDistinctness()
{
    setTestUidIndexKey(QByteArray::fromHex(kKeyHex));

    // 同一输入重复 1000 次必须完全一致。
    const QByteArray sample = pseudoRandom(7, 99);
    const QByteArray first = deriveUidIndex(sample);
    for (int i = 0; i < 1000; ++i) {
        require(deriveUidIndex(sample) == first, "deterministic across 1000 calls");
    }

    // 200 个不同 4 字节 UID → 200 个不同索引（32 字节空间，碰撞可忽略）。
    QSet<QByteArray> seen;
    for (quint32 seed = 0; seed < 200; ++seed) {
        const QByteArray uid = pseudoRandom(4, seed);
        const QByteArray index = deriveUidIndex(uid);
        require(index.size() == 32, "batch index size");
        require(!seen.contains(index), "no collision across 200 distinct 4-byte uids");
        seen.insert(index);
    }

    // 三种长度各 100 个，全部互不相同。
    QSet<QByteArray> mixed;
    for (int length : {4, 7, 10}) {
        for (quint32 seed = 0; seed < 100; ++seed) {
            const QByteArray index = deriveUidIndex(pseudoRandom(length, seed + quint32(length) * 1000));
            require(!mixed.contains(index), "no collision across mixed-length batch");
            mixed.insert(index);
        }
    }
    require(mixed.size() == 300, "300 distinct indices");
}

int main()
{
    withoutKey();
    knownAnswers();
    keyToggle();
    illegalLengths();
    lengthPrefixMatters();
    domainLabelAndRawBytes();
    determinismAndDistinctness();
    std::cout << "uid index tests passed (" << g_checks << " assertions)\n";
    return 0;
}
