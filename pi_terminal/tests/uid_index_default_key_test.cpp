#include "protocol/uid_index.h"

#include <QByteArray>

#include <cstdlib>
#include <iostream>

// 链接仓库存的**正式默认实现** `uid_index_key_default.cpp`（Z 尚未交付），
// 证明在密钥缺失时整条 RFID 索引路径是失败关闭的：
// 任何合法长度都返回空，绝不产生全零索引、绝不回退到明文比较。

static void require(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    const QByteArray uid4 = QByteArray("\x01\x02\x03\x04", 4);
    const QByteArray uid7 = QByteArray("\x01\x02\x03\x04\x05\x06\x07", 7);
    QByteArray uid10;
    for (int i = 0; i < 10; ++i) uid10.append(char(i));

    require(deriveUidIndex(uid4).isEmpty(), "default key provider: no index for 4-byte uid");
    require(deriveUidIndex(uid7).isEmpty(), "default key provider: no index for 7-byte uid");
    require(deriveUidIndex(uid10).isEmpty(), "default key provider: no index for 10-byte uid");

    std::cout << "uid index fail-closed test passed\n";
    return 0;
}
