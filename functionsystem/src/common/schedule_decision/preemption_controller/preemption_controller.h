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

#ifndef DOMAIN_DECISION_PREEMPTION_CONTROLLER_H
#define DOMAIN_DECISION_PREEMPTION_CONTROLLER_H

#include <unordered_map>

#include "common/resource_view/resource_type.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::schedule_decision {
struct PreemptResult {
    Status status;
    std::string unitID;
    std::string ownerID;
    std::vector<resource_view::InstanceInfo> preemptedInstances;
};

struct PreemptableUnit {
    int64_t score;
    std::string unitID;
    std::string ownerID;
    std::vector<resource_view::InstanceInfo> preemptedInstances;
    resource_view::Resources preemptedResources;
};

// for debug info print
struct InFeasibleContext {
    std::unordered_set<std::string> infeasibleUnits;
    std::unordered_set<std::string> crossTenantUnits;
    std::unordered_set<std::string> noPreemptableInstanceUnits;
    const size_t maxRecord = 10;
    void InsertInfeasibleUnit(const std::string &unitID)
    {
        if (infeasibleUnits.size() < maxRecord) {
            infeasibleUnits.insert(unitID);
        }
    }
    void InsertCrossTenantUnit(const std::string &unitID)
    {
        if (crossTenantUnits.size() < maxRecord) {
            crossTenantUnits.insert(unitID);
        }
    }
    void InsertNoPreemptableInstanceUnits(const std::string &unitID)
    {
        if (noPreemptableInstanceUnits.size() < maxRecord) {
            noPreemptableInstanceUnits.insert(unitID);
        }
    }
    void Print(const resource_view::InstanceInfo &instance)
    {
        std::stringstream ss;
        auto out = fmt::format("{}|preempt decision for instance({}): ", instance.requestid(), instance.instanceid());
        ss << out << "{ infeasible: ";
        for (auto &infeasible : infeasibleUnits) {
            ss << infeasible << " ";
        }
        ss << "}, { crossTenant: ";
        for (auto &cross : crossTenantUnits) {
            ss << cross << " ";
        }
        ss << "}, { NoPreemptableInstance: ";
        for (auto &noPreemtable : noPreemptableInstanceUnits) {
            ss << noPreemtable << " ";
        }
        ss << "})";
        YRLOG_INFO("{}", ss.str());
    }
};

class PreemptionController {
public:
    PreemptionController() = default;
    virtual ~PreemptionController() = default;
    /*
     * Schedule the unit resource unit with preempted hint.
     * @param ctx: schedule ctx.
     * @param instance: the instance need to be scheduled.
     * @param resourceUnit: current resource unit.`
     * @return PreemptResult: return preempt result if preemption is valid.
     *    status = StatusCode::DOMAIN_SCHEDULER_NO_PREEMPTABLE_INSTANCE if no instance can be preempted.
     */
    virtual PreemptResult PreemptDecision(const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
                                          const resource_view::InstanceInfo &instance,
                                          const resource_view::ResourceUnit &resourceUnit);

private:
    bool IsUnitMeetRequired(const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx,
                            const resource_view::InstanceInfo &instance, const resource_view::ResourceUnit &frag);
    bool IsCrossedTenant(const resource_view::InstanceInfo &instance, const resource_view::ResourceUnit &frag);
    bool IsResourceAffinityMeetRequired(const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx,
                                        const resource_view::InstanceInfo &instance,
                                        const resource_view::ResourceUnit &frag, int64_t &score);

    bool IsInstancePreemptable(const resource_view::InstanceInfo &srcInstance,
                               const resource_view::InstanceInfo &dstInstance, const resource_view::ResourceUnit &frag);

    PreemptableUnit ChoseInstanceToPreempted(const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx,
                                             const resource_view::InstanceInfo &instance,
                                             const resource_view::ResourceUnit &frag, int64_t &score);
};
}  // namespace functionsystem::domain_scheduler

#endif  // DOMAIN_DECISION_PREEMPTION_CONTROLLER_H
