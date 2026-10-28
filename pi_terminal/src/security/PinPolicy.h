#pragma once
#include <QByteArray>
#include <QString>

namespace security {


// PIN 与 KDF 策略。参数由 F-REVIEW-2 §5.3.2 Z-2 冻结，不得由配置降低。
namespace PinPolicy {

// 生产常量，硬编码。单测可传小值，但必须断言本值 ≥ 600000。
constexpr int kProductionIterations = 600000;
constexpr int kSaltBytes = 16;
constexpr int kKekBytes = 32;
constexpr int kMinDigits = 6;

// §2: 每账号连续失败 5 次锁 60 秒；另有终端级 1 次/秒。
constexpr int kMaxAccountFailures = 5;
constexpr int kAccountLockMs = 60 * 1000;
constexpr int kTerminalMinIntervalMs = 1000;

// 记录里的迭代数只能升不能降：users.json 可被同 OS 用户改写，
// 若直接采信降到 1 就等于关掉 KDF。§0.2 的"不得由配置降低"落在这里。
constexpr int effectiveIterations(int recorded) {
    return recorded < kProductionIterations ? kProductionIterations : recorded;
}

// 至少 6 位 ASCII 数字。返回 false 时不解释原因，调用方不得回显细节。
bool validPin(const QString &pin);

// 只接受 ASCII 数字；非数字一律拒绝，不做 Unicode 归一化。
QByteArray salt16();
QByteArray deriveKek(const QString &pin, const QByteArray &salt, int iterations = kProductionIterations);

// 落盘的验证值。必须是 verifyValue(KEK) 而不是 KEK 本身：注册表是明文文件，
// 若直接存 KEK，读文件即可解封 DB key、绕过 PIN。
QByteArray verifyValue(const QByteArray &kek);

// 常量时间比较，避免用比较耗时区分前缀匹配长度。
bool verifyMatches(const QByteArray &stored, const QByteArray &kek);

}  // namespace PinPolicy

}  // namespace security
