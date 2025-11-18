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

#ifndef DOMAIN_DECISION_SCHEDULE_QUEUE_H
#define DOMAIN_DECISION_SCHEDULE_QUEUE_H

#include <litebus.hpp>

#include "async/future.hpp"
#include "timer/timertools.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_view.h"
#include "common/schedule_decision/performer/schedule_performer.h"
#include "common/schedule_decision/scheduler/priority_scheduler.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/framework/framework_impl.h"

namespace functionsystem::schedule_decision {

struct QueueStateTransition {
    bool isRunningQueueEmpty;
    bool isPendingQueueEmpty;
    QueueStatus newStatus;
    bool needRequestConsumer;
};

const std::string SCHEDULE_QUEUE_ACTOR_NAME_POSTFIX = "-ScheduleQueueActor";
class ScheduleQueueActor : public litebus::ActorBase {
public:
    explicit ScheduleQueueActor(const std::string &name);
    ~ScheduleQueueActor() override = default;

    void ScheduleOnResourceUpdate();

    litebus::Future<ScheduleResult> ScheduleDecision(const std::shared_ptr<messages::ScheduleRequest> &req,
                                                     const litebus::Future<std::string> &cancelTag);

    litebus::Future<GroupScheduleResult> GroupScheduleDecision(const std::shared_ptr<GroupSpec> &spec);

    litebus::Future<Status> ScheduleConfirm(const std::shared_ptr<messages::ScheduleResponse> &rsp,
                                            const resource_view::InstanceInfo &ins);

    void RegisterResourceView(const std::shared_ptr<resource_view::ResourceView> &resourceView);

    void RegisterScheduler(const std::shared_ptr<ScheduleStrategy> &scheduler);

    litebus::Future<Status> RegisterPolicy(const std::string &policyName);

    inline void SetAllocateType(const AllocateType &type)
    {
        type_ = type;
    }

    [[maybe_unused]] QueueStatus GetQueueState() const
    {
        return status_;
    }

    [[maybe_unused]] void SetNewResourceAvailable()
    {
        isNewResourceAvailable_ = true;
    }

private:
    void RequestConsumer();
    void OnCancelInstanceSchedule(const litebus::Future<std::string> &cancelReason,
                                  const std::shared_ptr<litebus::Promise<ScheduleResult>> &promise);
    void OnCancelGroupSchedule(const litebus::Future<std::string> &cancelReason,
                               const std::shared_ptr<litebus::Promise<GroupScheduleResult>> &promise);
    void DoConsumeWithLatestInfo(const litebus::Future<resource_view::ResourceViewInfo> &resourceFuture);
    void DoConsumeWithCurrentInfo();
    void UpdateResourceInfo(const litebus::Future<resource_view::ResourceViewInfo> &resourceFuture);
    void HandlePendingRequests();
    void TransitionSchedulerQueueState();

    std::shared_ptr<resource_view::ResourceView> resourceView_;
    std::shared_ptr<ScheduleStrategy> scheduleStrategy_;
    AllocateType type_ = AllocateType::PRE_ALLOCATION;
    bool isNewResourceAvailable_ = true;
    QueueStatus status_{ QueueStatus::WAITING };
    litebus::Timer idleTimer_;
};
}  // namespace functionsystem::schedule_decision
#endif  // DOMAIN_DECISION_SCHEDULE_QUEUE_H
