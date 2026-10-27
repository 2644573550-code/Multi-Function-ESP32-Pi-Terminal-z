#pragma once
#include <QByteArray>

// DB key 的封装/解封。AES-256-GCM，KEK 由 PIN 经 PBKDF2 派生。
// 不用 PIN 本身作 SQLCipher key（§0.1）；PIN 只经这里间接解出 DB key。
namespace KeyWrap {

constexpr int kKeyBytes = 32;
constexpr int kNonceBytes = 12;
constexpr int kTagBytes = 16;

// 输出 32B key + 12B nonce + 16B tag 拼在一起，格式由调用方原样保存。
QByteArray seal(const QByteArray &kek, const QByteArray &dbKey, QByteArray *nonceOut);

// 失败（tag 不符/长度不对）返回空，调用方不得回退明文或重试出别的 key。
QByteArray open(const QByteArray &kek, const QByteArray &sealed, const QByteArray &nonce);

}  // namespace KeyWrap
