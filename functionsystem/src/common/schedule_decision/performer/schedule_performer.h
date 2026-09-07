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

#ifndef COMMON_SCHEDULE_DECISION_SCHEDULE_PERFORMER_H
#define COMMON_SCHEDULE_DECISION_SCHEDULE_PERFORMER_H

#include <litebus.hpp>

#include "async/future.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_view.h"
#include "common/schedule_decision/preemption_controller/preemption_controller.h"
#include "common/schedule_decision/queue/queue_item.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::schedule_decision {

enum AllocateType : int { PRE_ALLOCATION, ALLOCATION };

using PreemptInstancesFunc =
    std::function<litebus::Future<Status>(const std::vector<PreemptResult> &preemptResults)>;

class SchedulePerformer {
public:
    struct CandidateSelection {
        std::priority_queue<schedule_framework::NodeScore> &candidateNode;
        std::unordered_map<std::string, int32_t> &preAllocatedSelected;
        bool refreshSelectedUnit{ false };
    };

    explicit SchedulePerformer(const AllocateType &type)
        : type_(type), preemptController_(std::make_shared<PreemptionController>())
    {
    }

    virtual ~SchedulePerformer() = default;

    void RegisterPreemptInstanceCallback(const PreemptInstancesFunc &func)
    {
        preemptInstanceCallback_ = func;
    }

    void SetEnablePrintResourceView(bool enablePrintResourceView)
    {
        enablePrintResourceView_ = enablePrintResourceView;
    }

    void BindResourceView(const std::shared_ptr<resource_view::ResourceView> &resourceView)
    {
        resourceView_ = resourceView;
    }

    void RegisterScheduleFramework(const std::shared_ptr<schedule_framework::Framework> &framework)
    {
        framework_ = framework;
    }

    void SetPlacementPolicy(const std::string &policy)
    {
        placementPolicy_ = policy;
    }

    template <typename T, typename V>
    T DoSchedule(const std::shared_ptr<schedule_framework::PreAllocatedContext> &,
                 const resource_view::ResourceViewInfo &, const std::shared_ptr<V> &) const
    {
        return T();
    }

    ScheduleResult DoSelectByPolicy(const resource_view::ResourceUnit &resources,
                                    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                    const resource_view::InstanceInfo &ins, const std::string &requestID,
                                    const std::string &traceID);

    void PreAllocated(const resource_view::InstanceInfo &ins,
                      const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                      const std::string &requestID, const std::string &traceID, ScheduleResult &result);

    void DoPreAllocated(const resource_view::InstanceInfo &ins, const std::string &requestID,
                        const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                        const std::string &selected, ScheduleResult &result);

    void Allocate(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context, const std::string &selected,
                  const std::string &requestID, resource_view::InstanceInfo ins, ScheduleResult &schedResult);

    std::string GetAlreadyScheduledResult(const std::string &requestID,
                                          const resource_view::ResourceViewInfo &resourceInfo) const;
    std::string GetAlreadyScheduledResult(const std::string &requestID,
                                          const resource_view::ScheduleResourceView &resourceView) const;

    void RollBackAllocated(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                           const std::string &selected, const resource_view::InstanceInfo &ins,
                           const std::shared_ptr<resource_view::ResourceView> &resourceView);

    void RollBackGroupAllocated(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                const std::list<ScheduleResult> &results,
                                const std::vector<std::shared_ptr<messages::ScheduleRequest>> &requests,
                                const std::shared_ptr<resource_view::ResourceView> &resourceView, AllocateType type);

    ScheduleResult DoSelectOne(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                               const resource_view::ResourceViewInfo &resourceInfo,
                               const std::shared_ptr<InstanceItem> &instanceItem);
    ScheduleResult DoSelectOne(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                               const resource_view::ScheduleResourceView &resourceView,
                               const std::shared_ptr<InstanceItem> &instanceItem);

    bool IsScheduleResultNeedPreempt(const ScheduleResult &result);

    GroupScheduleResult DoCollectGroupResult(const std::list<ScheduleResult> &results);

    // preAllocatedSelected is used to collect statistics on the number of units that have been pre-deducted before
    // the current resource view is updated. In this way, resources will not be deducted from a unit repeatedly during
    // batch scheduling.
    bool IsScheduled(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                     const resource_view::ResourceViewInfo &resourceInfo,
                     const std::shared_ptr<InstanceItem> &instanceItem, ScheduleResult &result,
                     std::unordered_map<std::string, int32_t> &preAllocatedSelected);
    bool IsScheduled(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                     const resource_view::ScheduleResourceView &resourceView,
                     const std::shared_ptr<InstanceItem> &instanceItem, ScheduleResult &result,
                     std::unordered_map<std::string, int32_t> &preAllocatedSelected);

    ScheduleResult SelectFromResults(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                     const resource_view::ResourceViewInfo &resourceInfo,
                                     const std::shared_ptr<InstanceItem> &instanceItem,
                                     std::priority_queue<schedule_framework::NodeScore> &candidateNode,
                                     std::unordered_map<std::string, int32_t> &preAllocatedSelected);
    ScheduleResult SelectFromResults(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                     const resource_view::ScheduleResourceView &resourceView,
                                     const std::shared_ptr<InstanceItem> &instanceItem,
                                     CandidateSelection selection);

protected:
    static bool PrepareCandidate(CandidateSelection &selection, schedule_framework::NodeScore &nodeScore);
    void RefreshCandidate(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                          const resource_view::ScheduleResourceView &resourceView,
                          const std::shared_ptr<InstanceItem> &instanceItem,
                          const schedule_framework::NodeScore &nodeScore, CandidateSelection &selection);
    ScheduleResult CommitCandidate(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                   const resource_view::ScheduleResourceView &resourceView,
                                   const std::shared_ptr<InstanceItem> &instanceItem,
                                   schedule_framework::NodeScore &nodeScore, CandidateSelection &selection);

    AllocateType type_;
    bool enablePrintResourceView_ = false;
    std::shared_ptr<resource_view::ResourceView> resourceView_;
    std::shared_ptr<schedule_framework::Framework> framework_;
    std::shared_ptr<PreemptionController> preemptController_;
    PreemptInstancesFunc preemptInstanceCallback_;
    std::string placementPolicy_{ "binpack" };
};

}
#endif  // COMMON_SCHEDULE_DECISION_SCHEDULE_PERFORMER_H
