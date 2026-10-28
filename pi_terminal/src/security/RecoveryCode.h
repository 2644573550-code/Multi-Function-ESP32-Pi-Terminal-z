#pragma once

#include <QByteArray>
#include <QString>

namespace security {

// 离线恢复码（报告 §6.2 / §7.2）。
//
// 问题：DB key 只由 PIN 派生的 KEK 封装。用户丢 PIN = 数据永久不可读，
// 而报告明确要求「PIN 恢复保留数据须使用该用户离线恢复码」。
//
// 方案：建档/改 PIN 时同时用一条独立的恢复密钥再封装一次 DB key，
// 恢复密钥由高熵随机码派生，与 PIN 完全无关：
//
//   DB key --(KEK = PBKDF2(PIN))-->  wrapped_by_pin
//          --(REK = PBKDF2(recovery_code))-->  wrapped_by_recovery
//
// 两条路径互不影响：改 PIN 只重封装 by_pin，恢复码不变；
// 换恢复码只重封装 by_recovery，PIN 不变。
//
// 恢复码格式：24 个 Base32 字符（约 120 bit），分 6 组便于抄写。
// 只显示一次，不落盘明文；服务端只保存派生出的 REK 封装结果。
namespace RecoveryCode {

constexpr int kChars = 24;
constexpr int kGroupSize = 4;
constexpr int kGroups = 6;
constexpr int kSaltBytes = 16;
constexpr int kNonceBytes = 12;
constexpr int kKeyBytes = 32;

// 生成一条新恢复码（大写到 Base32 字母表，去掉易混淆的 0/O/1/I）。
QString generate();

// 校验格式（长度、字符集、分组）。不校验是否"正确"——那要试解封。
bool isValidFormat(const QString &code);

// 规范化：去空格/连字符，转大写。
QString normalize(const QString &code);

// 由恢复码派生恢复密钥（REK）。失败返回空。
QByteArray deriveKey(const QString &code, const QByteArray &salt);

// 用 REK 封装 DB key，返回密文（含 tag）。nonce 由调用方保存。
QByteArray wrap(const QByteArray &dbKey, const QByteArray &rek, QByteArray *nonce);

// 解封。失败返回空（密码错或密文坏）。
QByteArray unwrap(const QByteArray &wrapped, const QByteArray &rek, const QByteArray &nonce);

// 生成随机 salt。
QByteArray randomSalt();

}  // namespace RecoveryCode
}  // namespace security
