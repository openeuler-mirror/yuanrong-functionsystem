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

#ifndef DOMAIN_INSTANCE_CTRL_H
#define DOMAIN_INSTANCE_CTRL_H

#include <async/future.hpp>
#include <litebus.hpp>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/schedule_decision/schedule_queue_actor.h"
#include "common/schedule_decision/schedule_recorder/schedule_recorder.h"
#include "common/schedule_decision/scheduler.h"
#include "common/resource_view/resource_view_mgr.h"
#include "domain_scheduler/underlayer_scheduler_manager/underlayer_sched_mgr.h"
#include "common/resource_view/resource_type.h"


namespace functionsystem::domain_scheduler {
const std::string INSTANCE_CTRL_ACTOR_NAME_POSTFIX = "-DomainInstanceCtrl";
const int32_t DEFAULT_MAX_RETRY_TIMES = 3;
const uint32_t DEFAULT_CREATE_AGENT_AWAIT_RETRY_INTERVAL = 1000;  // ms
const uint32_t DEFAULT_CREATE_AGENT_AWAIT_RETRY_TIMES = 120;
const uint32_t DEFAULT_CREATE_AGENT_RETRY_INTERVAL = 50000;                    // ms
const uint32_t MAX_CREATE_AGENT_RETRY_INTERVAL = 10000;                        // ms
const uint32_t MIN_CREATE_AGENT_RETRY_INTERVAL = 50;                           // ms
const std::vector<uint32_t> RETRY_SCHEDULE_INTERVALS = { 3000, 5000, 10000 };  // ms

class InstanceCtrlActor : public litebus::ActorBase {
public:
    explicit InstanceCtrlActor(const std::string &name)
        : ActorBase(name + INSTANCE_CTRL_ACTOR_NAME_POSTFIX), maxSchedReTryTimes_(DEFAULT_MAX_RETRY_TIMES)
    {
    }
    explicit InstanceCtrlActor(const std::string &name, bool isTolerateUnderlayerAbnormal)
        : ActorBase(name + INSTANCE_CTRL_ACTOR_NAME_POSTFIX),
          maxSchedReTryTimes_(DEFAULT_MAX_RETRY_TIMES),
          isTolerateUnderlayerAbnormal_(isTolerateUnderlayerAbnormal)
    {
    }
    ~InstanceCtrlActor() override = default;

    /* *
     * Schedule instance
     */
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> Schedule(
        const std::shared_ptr<messages::ScheduleRequest> &req);

    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> ScheduleDecision(
        const std::shared_ptr<messages::ScheduleRequest> &req);

    void CreateAgentResponse(const litebus::AID &from, std::string && /* name */, std::string &&msg);

    void UpdateMaxSchedRetryTimes(const uint32_t &retrys);

    void SetDomainLevel(bool isHeader)
    {
        isHeader_ = isHeader;
    }

    void SetScalerAddress(const std::string &address);

    void TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest);

    void BindUnderlayerMgr(const std::shared_ptr<UnderlayerSchedMgr> &underlayer)
    {
        ASSERT_IF_NULL(underlayer);
        underlayer_ = underlayer;
    }

    void BindResourceView(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
    {
        ASSERT_IF_NULL(resourceViewMgr);
        resourceViewMgr_ = resourceViewMgr;
    }

    inline void BindScheduleRecorder(const std::shared_ptr<schedule_decision::ScheduleRecorder> &recorder)
    {
        ASSERT_IF_NULL(recorder);
        recorder_ = recorder;
    }

    void BindScheduler(const std::shared_ptr<schedule_decision::Scheduler> &scheduler)
    {
        ASSERT_IF_NULL(scheduler);
        scheduler_ = scheduler;
    }

    // only for test
    void SetCreateAgentAwaitRetryInterval(uint32_t interval)
    {
        createAgentAwaitRetryInterval_ = interval;
    }

    void SetCreateAgentAwaitRetryTimes(uint32_t times)
    {
        createAgentAwaitRetryTimes_ = times;
    }

    void SetCreateAgentRetryInterval(uint32_t interval)
    {
        if (interval <= MAX_CREATE_AGENT_RETRY_INTERVAL && interval >= MIN_CREATE_AGENT_RETRY_INTERVAL) {
            createAgentRetryInterval_ = interval;
        }
    }

    void SetRetryScheduleIntervals(const std::vector<uint32_t> &intervals)
    {
        retryScheduleIntervals_ = intervals;
        scheduleRetryTimes_ = intervals.size();
    }

    void SetEnableVerticalScale(bool enable)
    {
        enableVerticalScale_ = enable;
    }

    std::vector<std::shared_ptr<messages::ScheduleRequest>> GetSchedulerQueue()
    {
        // 创建一个vector来存储所有的values
        std::vector<std::shared_ptr<messages::ScheduleRequest>> values;

        // 使用std::transform和std::back_inserter来获取所有的values
        std::transform(schedulerQueueMap_.begin(), schedulerQueueMap_.end(), std::back_inserter(values),
                       [](const auto &pair) { return pair.second; });

        return values;
    }

protected:
    void Init() override;

private:
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> DispatchSchedule(
        const litebus::Future<schedule_decision::ScheduleResult> &result,
        const std::shared_ptr<messages::ScheduleRequest> &req, uint32_t dispatchTimes);

    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> OnDispatchSchedule(
        litebus::Future<std::shared_ptr<messages::ScheduleResponse>> rsp,
        const std::shared_ptr<messages::ScheduleRequest> &req);

    void CheckIsNeedReDispatch(const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &rspFuture,
                               const litebus::Promise<std::shared_ptr<messages::ScheduleResponse>> &promise,
                               const schedule_decision::ScheduleResult &schedResult,
                               const std::shared_ptr<messages::ScheduleRequest> &req, uint32_t dispatchTimes);

    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> CheckReSchedulingIsRequired(
        const std::shared_ptr<messages::ScheduleResponse> &rsp, const std::shared_ptr<messages::ScheduleRequest> &req);

    litebus::Future<std::shared_ptr<messages::CreateAgentResponse>> CreateAgent(
        const std::shared_ptr<messages::ScheduleRequest> &req);

    void RetryCreateAgent(const std::shared_ptr<messages::CreateAgentRequest> &req, uint32_t times);

    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> OnCreateAgent(
        const litebus::Future<std::shared_ptr<messages::CreateAgentResponse>> &createAgentRsp,
        const std::shared_ptr<messages::ScheduleRequest> &req,
        const std::shared_ptr<messages::ScheduleResponse> &scheduleRsp);

    std::shared_ptr<messages::ScheduleResponse> BuildErrorScheduleRsp(
        const schedule_decision::ScheduleResult &result, const std::shared_ptr<messages::ScheduleRequest> &req);

    void RetrySchedule(const std::shared_ptr<messages::ScheduleRequest> &req,
                       const litebus::Promise<std::shared_ptr<messages::ScheduleResponse>> &promise);

    std::shared_ptr<litebus::Promise<std::string>> GetCancelTag(const std::string &requestId);

    bool isHeader_{ false };  // this indicates whether the domain is the head node.
    bool isScalerEnabled_{ false };
    litebus::AID scaler_;
    std::shared_ptr<schedule_decision::Scheduler> scheduler_;
    std::shared_ptr<UnderlayerSchedMgr> underlayer_;
    std::shared_ptr<resource_view::ResourceViewMgr> resourceViewMgr_{ nullptr };

    uint32_t maxSchedReTryTimes_;
    std::unordered_map<std::string, uint32_t> requestTrySchedTimes_;
    std::unordered_map<std::string, uint32_t> waitAgentCreatRetryTimes_;
    std::unordered_map<std::string, litebus::Promise<std::shared_ptr<messages::CreateAgentResponse>>>
        createAgentPromises_;
    std::unordered_map<std::string, litebus::Timer> createAgentRetryTimers_;
    uint32_t createAgentAwaitRetryInterval_ = DEFAULT_CREATE_AGENT_AWAIT_RETRY_INTERVAL;
    std::vector<uint32_t> retryScheduleIntervals_ = RETRY_SCHEDULE_INTERVALS;
    uint32_t createAgentAwaitRetryTimes_ = DEFAULT_CREATE_AGENT_AWAIT_RETRY_TIMES;
    bool isTolerateUnderlayerAbnormal_ = true;
    uint32_t createAgentRetryInterval_ = DEFAULT_CREATE_AGENT_RETRY_INTERVAL;
    void CheckUUID(const std::shared_ptr<messages::ScheduleRequest> &req) const;
    std::shared_ptr<schedule_decision::ScheduleRecorder> recorder_;
    uint32_t scheduleRetryTimes_{ 0 };
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<std::string>>> cancelTag_;
    std::map<std::string, std::shared_ptr<messages::ScheduleRequest>> schedulerQueueMap_;
    bool enableVerticalScale_{ false };
};
}  // namespace functionsystem::domain_scheduler

#endif  // DOMAIN_INSTANCE_CTRL_H
