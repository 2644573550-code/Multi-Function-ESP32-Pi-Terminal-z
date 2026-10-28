#pragma once

#include <QString>

namespace security {

class UserDatabase;

// 备份与恢复（报告 §7.1 / §7.2）。
//
// 两种备份：
//   - 个人备份：用户自己的密码加密**本人**数据。管理员也解不开。
//   - 整机备份：管理员备份账户/设备结构，用户资料仍是各自密文。
//
// P0 只实现个人备份（满足"用户能自己救回数据"这一条）；
// 整机备份需要跨用户遍历，涉及管理员不得解密成员数据，列 P1。
//
// 关键约束：
//   - 一致性快照：备份时先关库再读，或读副本，避免半写
//   - 失败不覆盖：写到临时文件，校验通过才替换
//   - 不把明文写进临时目录：全程在内存里加密后一次落盘
namespace Backup {

// 用 const char[] 而不是 const char*，否则 sizeof 得到的是指针大小。
constexpr char kFileMagic[] = "OPTBK001";
constexpr int kMagicLen = 8;
constexpr int kSaltBytes = 16;
constexpr int kNonceBytes = 12;
constexpr int kTagBytes = 16;
constexpr int kMinPassword = 8;
// 备份文件格式版本；不兼容时拒绝恢复而不是猜测。
constexpr int kFormatVersion = 1;

enum class Result {
    Ok,
    BadPassword,
    CorruptFile,          // 魔数/版本/长度不对
    WrongUser,            // 备份属于另一个用户
    StorageUnavailable,
    InvalidArgument,
};

QString describe(Result result);

// 备份当前打开的用户库到 path。password 由用户提供，不落盘。
Result create(const UserDatabase &database, const QString &path, const QString &password);

// 从 path 恢复进当前打开的用户库。
// 恢复前会校验备份里的 userId 与当前会话一致，防止串档。
Result restore(UserDatabase *database, const QString &path, const QString &password,
               const QString &expectedUserId);

}  // namespace Backup

}  // namespace security
