#include "protocol/uid_index.h"

#include "security/uid_index_key.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

namespace {
constexpr int kIndexBytes = 32;
constexpr int kKeyBytes = 32;
// 域分隔标签。改这个字符串等于换一套索引，必须同时提规范修订。
constexpr char kDomainLabel[] = "p0-rfid-index-v1";
} // namespace

QByteArray deriveUidIndex(const QByteArray &rawUid)
{
    const int length = rawUid.size();
    if (length != 4 && length != 7 && length != 10) return {};

    QByteArray key;
    if (!uidIndexKey(&key) || key.size() != kKeyBytes) {
        // 失败关闭：密钥缺失/长度不对时绝不产生索引，也不回退到别的算法。
        key.fill('\0');
        return {};
    }

    QByteArray message;
    message.reserve(int(sizeof(kDomainLabel) - 1) + 1 + length);
    message.append(kDomainLabel, int(sizeof(kDomainLabel) - 1));
    message.append(char(length));
    message.append(rawUid);

    const QByteArray index =
        QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);

    // 不把密钥留在堆上：QByteArray 析构不保证清零。
    key.fill('\0');

    return index.size() == kIndexBytes ? index : QByteArray();
}
