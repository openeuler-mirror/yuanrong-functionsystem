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

#ifndef DOMAIN_SCHEDULER_PROXY_H
#define DOMAIN_SCHEDULER_PROXY_H

#include <async/future.hpp>
#include <litebus.hpp>

#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"
#include "common/schedule_decision/scheduler_common.h"

namespace functionsystem::schedule_decision {

class Scheduler {
public:
    Scheduler() = default;
    explicit Scheduler(const litebus::AID &primaryAid, const litebus::AID &virtualAid)
        : primary_(primaryAid), virtual_(virtualAid)
    {
    }

    virtual ~Scheduler() = default;

    virtual litebus::Future<ScheduleResult> ScheduleDecision(const std::shared_ptr<messages::ScheduleRequest> &req);

    // while cancelTag is set to failed, the future would not return
    // while cancelTag is set to a specified value, the future would be associated
    virtual litebus::Future<ScheduleResult> ScheduleDecision(const std::shared_ptr<messages::ScheduleRequest> &req,
                                                             const litebus::Future<std::string> &cancelTag);

    virtual litebus::Future<Status> ScheduleConfirm(
        const std::shared_ptr<messages::ScheduleResponse> &rsp, const resource_view::InstanceInfo &ins,
        const ScheduleResult &schedResult);

    // If range scheduling, the number of instances that are successfully scheduled is returned. If the
    // number of instances that are successfully scheduled is less than min, a failure message is returned.
    // When gang scheduling, a failure message is returned if one fails.
    virtual litebus::Future<GroupScheduleResult> GroupScheduleDecision(const std::shared_ptr<GroupSpec> &spec);

    virtual litebus::Future<Status> RegisterPolicy(const std::string &policyName);

private:
    litebus::AID GetAID(const int32_t &type);
    litebus::AID primary_;
    litebus::AID virtual_;
};
}  // namespace functionsystem::domain_scheduler

#endif  // DOMAIN_SCHEDULER_PROXY_H
