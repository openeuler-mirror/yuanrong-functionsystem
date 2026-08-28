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
#ifndef SCHEDULER_FRAMEWORK_H
#define SCHEDULER_FRAMEWORK_H

#include <string>
#include <queue>

#include "async/try.hpp"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/schedule_snapshot.h"
#include "common/scheduler_framework/framework/policy.h"
#include "common/scheduler_framework/utils/score.h"
#include "common/status/status.h"

namespace functionsystem::schedule_framework {

constexpr int MAX_UNIT_SCORE = 100;
constexpr int MIN_UNIT_SCORE = 0;

struct ScheduleResult {
    std::string id;
    int32_t code;
    std::string reason;
};

struct FilterResult {
    Status status;
    std::set<std::string> feasibleNodes{};
};

struct ScoreResult {
    Status status;
    std::vector<NodeScore> nodeScoreLists{};
};

struct ScheduleResults {
    int32_t code;
    std::string reason;
    std::priority_queue<NodeScore> sortedFeasibleNodes;
};

struct CandidateEvaluation {
    Status status;
    bool feasible{ false };
    bool fatal{ false };
    NodeScore score{ 0 };
};

class Framework {
public:
    Framework() = default;
    virtual ~Framework() = default;
    virtual bool RegisterPolicy(const std::shared_ptr<SchedulePolicyPlugin> &plugin) = 0;
    virtual bool UnRegisterPolicy(const std::string &name) = 0;
    virtual ScheduleResults SelectFeasible(const std::shared_ptr<ScheduleContext> &ctx,
                                           const resource_view::InstanceInfo &instance,
                                           const resource_view::ResourceUnit &resourceUnit,
                                           uint32_t expectedFeasible) = 0;
    virtual ScheduleResults SelectFeasible(const std::shared_ptr<ScheduleContext> &ctx,
                                           const resource_view::InstanceInfo &instance,
                                           const resource_view::ScheduleResourceView &resourceView,
                                           uint32_t expectedFeasible)
    {
        const auto *legacy = resourceView.GetLegacyResourceUnit();
        if (legacy != nullptr) {
            return SelectFeasible(ctx, instance, *legacy, expectedFeasible);
        }
        return ScheduleResults{ static_cast<int32_t>(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG),
                                "schedule framework does not support Unit snapshot", {} };
    }
    virtual bool SupportsScheduleSnapshot() const
    {
        return false;
    }
    // Unknown plugins may inspect request fields that are intentionally
    // excluded from the built-in aggregation signature. They must opt out of
    // aggregation unless the framework can prove all registered plugins use
    // the built-in scheduling semantics.
    virtual bool SupportsSemanticAggregation() const
    {
        return false;
    }
    virtual CandidateEvaluation EvaluateUnit(const std::shared_ptr<ScheduleContext> &,
                                             const resource_view::InstanceInfo &,
                                             const resource_view::ScheduleResourceView &,
                                             const std::string &)
    {
        return CandidateEvaluation{ Status(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG,
                                           "schedule framework does not support Unit reevaluation"),
                                    false, true, NodeScore{ 0 } };
    }
};
}  // namespace functionsystem::schedule_framework
#endif  // SCHEDULER_FRAMEWORK_H
