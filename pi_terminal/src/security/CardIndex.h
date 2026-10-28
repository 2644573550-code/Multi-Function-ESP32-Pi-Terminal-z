#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>

namespace security {


// T-08 的 Z 侧：从 RFID 事件到「选中哪个用户」的匹配逻辑。
//
// 文件边界（f-day1 / 05 §4 D8b）：
//   - PogoControlTransport.cpp / HostEventDetector.h / ConnectionManager.cpp 都是 D 的，不改
//   - 本文件只新增用户匹配与 uidIndexKey() 调用点
//
// 派生函数归 D，签名由 F-REVIEW-2 §5.3.2 DR-14 冻结，本文件只调用不实现：
//   QByteArray deriveUidIndex(const QByteArray &rawUid);
// D 交付前这条符号不存在，因此本类在测试里通过注入桩来验证。
class UserDatabase;

// 匹配结果。raw UID 不出现在任何返回值里。
struct CardMatch {
    enum class Verdict {
        Matched,      // 找到唯一归属用户，只可跳到 PIN 页
        NoMatch,      // 索引不属于任何用户（或全部挂失）
        IndexRejected // 派生失败：长度非法或密钥不可用，失败关闭
    };
    Verdict verdict = Verdict::IndexRejected;
    QString userId;
};

// 绑卡窗口：已登录 + 重验 PIN 后开放 30 秒单次（f-day1 §2）。
class CardBindingWindow {
public:
    void open(qint64 nowMs);
    bool isOpen(qint64 nowMs) const;
    // 单次：消费后立即关闭，不能连绑两张。
    bool consume(qint64 nowMs);
    void close() { openedAtMs_ = -1; }

private:
    qint64 openedAtMs_ = -1;
    static constexpr qint64 kWindowMs = 30 * 1000;
};

}  // namespace security
