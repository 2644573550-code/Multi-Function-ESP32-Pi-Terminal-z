#include "security/TaskCapability.h"

namespace security {


TaskCapability::TaskCapability(Kind kind, QString originUserId, QString taskId,
                               QString deviceId, qint64 generation)
    : kind_(kind)
    , originUserId_(std::move(originUserId))
    , taskId_(std::move(taskId))
    , deviceId_(std::move(deviceId))
    , generation_(generation) {}

}  // namespace security
