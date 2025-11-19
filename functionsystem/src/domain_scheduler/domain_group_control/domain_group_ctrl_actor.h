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
#ifndef DOMAIN_SCHEDULER_DOMAIN_GROUP_CTRL_ACTOR_H
#define DOMAIN_SCHEDULER_DOMAIN_GROUP_CTRL_ACTOR_H
#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/proto/pb/posix_pb.h"
#include "common/schedule_decision/scheduler.h"
#include "common/schedule_decision/schedule_recorder/schedule_recorder.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/resource_view.h"
#include "underlayer_scheduler_manager/underlayer_sched_mgr.h"
#include "common/explorer/explorer.h"

namespace functionsystem::domain_scheduler {
struct GroupScheduleContext {
    std::chrono::time_point<std::chrono::high_resolution_clock> beginTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> rangeScheduleLoopTime;
    int32_t retryTimes;
    std::shared_ptr<litebus::Promise<Status>> schedulePromise;
    std::shared_ptr<messages::GroupInfo> groupInfo;
    std::vector<std::shared_ptr<messages::ScheduleRequest>> requests;
    std::vector<schedule_decision::ScheduleResult> tryScheduleResults;
    std::list<std::shared_ptr<messages::ScheduleResponse>> responses;
    std::set<std::string> failedReserve;
    bool insRangeScheduler;
    std::shared_ptr<messages::ScheduleRequest> insRangeRequest;
    std::vector<std::shared_ptr<messages::ScheduleRequest>> rangeRequests;
    // The index of the last instance that is successfully reserved is marked to ensure that all subsequent instances
    // can be rolled back and the sequence of group scheduling is ensured.
    // -1 indicates that no instances have been reserved.
    int lastReservedInd {-1};
    litebus::Promise<std::string> cancelPromise;
};
class DomainGroupCtrlActor : public litebus::ActorBase {
public:
    explicit DomainGroupCtrlActor(const std::string &name) : ActorBase(name)
    {
    }
    ~DomainGroupCtrlActor() override = default;
    /* *
     * Receive the GroupSchedule request forwarded by local
     * @param from: Caller AID
     * @param name: Interface name
     * @param msg: Serialized ScheduleRequests
     */
    void ForwardGroupSchedule(const litebus::AID &from, std::string &&name, std::string &&msg);

    void TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest);

    void OnGroupScheduleDecision(const litebus::Future<schedule_decision::GroupScheduleResult> &future,
                                 const std::shared_ptr<GroupScheduleContext> &ctx);

    inline void BindUnderlayerMgr(const std::shared_ptr<UnderlayerSchedMgr> &underlayer)
    {
        ASSERT_IF_NULL(underlayer);
        underlayer_ = underlayer;
    }

    inline void BindScheduler(const std::shared_ptr<schedule_decision::Scheduler> &scheduler)
    {
        ASSERT_IF_NULL(scheduler);
        scheduler_ = scheduler;
    }

    std::vector<std::shared_ptr<messages::ScheduleRequest>> GetRequests();

    inline void BindScheduleRecorder(const std::shared_ptr<schedule_decision::ScheduleRecorder> &recorder)
    {
        ASSERT_IF_NULL(recorder);
        recorder_ = recorder;
    }
protected:
    void Init() override;

private:
    void UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo);
    void OnGroupScheduleDone(const litebus::AID &from, const litebus::Future<Status> &future,
                             const std::shared_ptr<GroupScheduleContext> &groupCtx);
    std::shared_ptr<GroupScheduleContext> NewGroupContext(const std::shared_ptr<messages::GroupInfo> &groupInfo);
    std::shared_ptr<GroupScheduleContext> UpdateRangeScheduleGroupContext(
        std::shared_ptr<GroupScheduleContext> groupCtx, std::int32_t curRangeInsNum);
    bool ExistsGroupContext(const std::string &requestID);
    void GroupScheduleDone(const std::shared_ptr<GroupScheduleContext> &ctx, const Status &status);
    void OnGroupScheduleDecisionSuccessful(const std::vector<schedule_decision::ScheduleResult> &results,
                                           const std::shared_ptr<GroupScheduleContext> &groupCtx);
    std::vector<int> RollbackContext(const std::shared_ptr<GroupScheduleContext> &ctx);

    litebus::Future<Status> ToReserve(const std::vector<schedule_decision::ScheduleResult> &results,
                                      const std::shared_ptr<GroupScheduleContext> &groupCtx);
    void OnReserve(const litebus::Future<Status> &future, const std::vector<schedule_decision::ScheduleResult> &results,
                   const std::shared_ptr<GroupScheduleContext> &groupCtx);
    litebus::Future<Status> RollbackReserve(const std::vector<schedule_decision::ScheduleResult> &results,
                                            const std::shared_ptr<GroupScheduleContext> &groupCtx);

    litebus::Future<Status> RollbackRangeReserve(
            const std::vector<int> &rollbackedInd,
            const std::vector<schedule_decision::ScheduleResult> &results,
            const std::shared_ptr<GroupScheduleContext> &groupCtx);

    void ReleaseUnusedReserve(const std::vector<schedule_decision::ScheduleResult> &results,
                              const std::shared_ptr<GroupScheduleContext> &groupCtx);

    litebus::Future<Status> ToBind(const std::vector<schedule_decision::ScheduleResult> &results,
                                   const std::shared_ptr<GroupScheduleContext> &groupCtx);
    void OnBind(const litebus::Future<Status> &future, const std::vector<schedule_decision::ScheduleResult> &results,
                const std::shared_ptr<GroupScheduleContext> &groupCtx);
    litebus::Future<Status> RollbackBind(const std::vector<schedule_decision::ScheduleResult> &results,
                                         const std::shared_ptr<GroupScheduleContext> &groupCtx);
    void OnRollbackBind(const litebus::Future<Status> &future, const std::shared_ptr<GroupScheduleContext> &groupCtx);

    void OnRangeInstanceSchedule(std::shared_ptr<messages::ScheduleRequest> rangeReq,
                                 std::shared_ptr<GroupScheduleContext> groupCtx);

private:
    litebus::AID groupManager_;
    std::shared_ptr<UnderlayerSchedMgr> underlayer_;
    std::shared_ptr<schedule_decision::Scheduler> scheduler_;
    std::unordered_map<std::string, std::shared_ptr<GroupScheduleContext>> groupScheduleCtx_;
    std::shared_ptr<schedule_decision::ScheduleRecorder> recorder_;
};
}  // namespace functionsystem::domain_scheduler
#endif  // DOMAIN_SCHEDULER_DOMAIN_GROUP_CTRL_ACTOR_H
