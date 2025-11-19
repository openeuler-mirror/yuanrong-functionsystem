/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DOMAIN_SCHEDULER_DOMAIN_GROUP_CTRL_H
#define DOMAIN_SCHEDULER_DOMAIN_GROUP_CTRL_H

#include <async/future.hpp>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "litebus.hpp"

namespace functionsystem::domain_scheduler {
class DomainGroupCtrl {
public:
    explicit DomainGroupCtrl(const litebus::ActorReference &actor) : actor_(actor)
    {
    }
    virtual ~DomainGroupCtrl() = default;
    virtual void TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest);
    virtual litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>> GetRequests();

private:
    litebus::ActorReference actor_;
};
}  // namespace functionsystem::domain_scheduler
#endif  // DOMAIN_SCHEDULER_DOMAIN_GROUP_CTRL_H
