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
#include "scheduler.h"

#include <async/async.hpp>
#include <litebus.hpp>

#include "common/schedule_decision/schedule_queue_actor.h"
#include "common/utils/collect_status.h"
#include "common/resource_view/resource_view_mgr.h"

namespace functionsystem::schedule_decision {

litebus::AID Scheduler::GetAID(const int32_t &type)
{
    if (type == static_cast<int32_t>(resource_view::ResourceType::VIRTUAL)) {
        return virtual_;
    }
    return primary_;
}

litebus::Future<ScheduleResult> Scheduler::ScheduleDecision(const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto type = resource_view::GetResourceType(req->instance());
    return litebus::Async(GetAID(static_cast<int32_t>(type)), &ScheduleQueueActor::ScheduleDecision, req,
                          litebus::Future<std::string>());
}

litebus::Future<ScheduleResult> Scheduler::ScheduleDecision(const std::shared_ptr<messages::ScheduleRequest> &req,
                                                            const litebus::Future<std::string> &cancelTag)
{
    auto type = resource_view::GetResourceType(req->instance());
    return litebus::Async(GetAID(static_cast<int32_t>(type)), &ScheduleQueueActor::ScheduleDecision, req, cancelTag);
}

litebus::Future<Status> Scheduler::ScheduleConfirm(const std::shared_ptr<messages::ScheduleResponse> &rsp,
                                                   const resource_view::InstanceInfo &ins,
                                                   const ScheduleResult & /* schedResult */)
{
    auto type = resource_view::GetResourceType(ins);
    return litebus::Async(GetAID(static_cast<int32_t>(type)), &ScheduleQueueActor::ScheduleConfirm, rsp, ins);
}

litebus::Future<Status> Scheduler::RegisterPolicy(const std::string &policyName)
{
    std::list<litebus::Future<Status>> futures;
    futures.emplace_back(litebus::Async(primary_, &ScheduleQueueActor::RegisterPolicy, policyName));
    futures.emplace_back(litebus::Async(virtual_, &ScheduleQueueActor::RegisterPolicy, policyName));
    return CollectStatus(futures, "");
}

litebus::Future<GroupScheduleResult> Scheduler::GroupScheduleDecision(const std::shared_ptr<GroupSpec> &spec)
{
    litebus::AID aid = primary_;
    if (!spec->requests.empty()) {
        auto type = resource_view::GetResourceType(spec->requests[0]->instance());
        aid = GetAID(static_cast<int32_t>(type));
    }
    return litebus::Async(aid, &ScheduleQueueActor::GroupScheduleDecision, spec);
}
}  // namespace functionsystem::domain_scheduler