#pragma once
#include <QByteArray>
#include <QString>

namespace security {


// 前台能力与任务能力分离（f-day1 §5）。
//
// 锁屏只撤销**前台**能力：任务仍可向**原用户的同一任务**追加样本，
// 但读不到历史、不能切用户、不能导出。
// 切用户/退出前必须先收尾任务，迟到事件不得挂到新用户。
class TaskCapability {
public:
    enum class Kind {
        Foreground,   // 前台：可读可写，随锁屏失效
        Task,         // 任务：只能追加绑定任务的新样本
    };

    // originUserId 在创建时固定，之后不可变更。
    // generation 由调用方传入：必须是该 taskId 当前已推进到的代次，
    // 否则同一 taskId 重建时旧代次的迟到回调会被误认成新的合法回调。
    TaskCapability(Kind kind, QString originUserId, QString taskId, QString deviceId,
                   qint64 generation = 0);

    Kind kind() const { return kind_; }
    QString originUserId() const { return originUserId_; }
    QString taskId() const { return taskId_; }
    QString deviceId() const { return deviceId_; }
    qint64 generation() const { return generation_; }

    // 撤销后所有调用被拒；撤销不可逆。
    void revoke() { revoked_ = true; }
    bool revoked() const { return revoked_; }

    // 锁屏/切用户会推进代次，旧代次的迟到回调一律拒绝。
    void bumpGeneration() { ++generation_; }

    bool canReadHistory() const {
        return !revoked_ && kind_ == Kind::Foreground;
    }

    // 只能向绑定任务追加；且 device 必须与绑定一致，防止串档。
    bool canAppend(const QString &deviceId, const QString &originUserId, qint64 generation) const {
        if (revoked_ || kind_ != Kind::Task) return false;
        return deviceId == deviceId_ && originUserId == originUserId_ && generation == generation_;
    }

private:
    Kind kind_;
    QString originUserId_;
    QString taskId_;
    QString deviceId_;
    qint64 generation_ = 0;
    bool revoked_ = false;
};

}  // namespace security
