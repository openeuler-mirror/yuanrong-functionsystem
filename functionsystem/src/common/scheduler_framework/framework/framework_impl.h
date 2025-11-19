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
#ifndef SCHEDULER_FRAMEWORK_IMPL_H
#define SCHEDULER_FRAMEWORK_IMPL_H

#include <string>
#include <unordered_map>

#include "common/resource_view/resource_type.h"
#include "common/scheduler_framework/framework/framework.h"
#include "common/scheduler_framework/framework/policy.h"
#include "common/status/status.h"

namespace functionsystem::schedule_framework {
class FrameworkImpl : public Framework {
public:
    FrameworkImpl() = default;
    explicit FrameworkImpl(int32_t relaxed) : Framework(), relaxed_(relaxed) {}
    ~FrameworkImpl() override = default;
    bool RegisterPolicy(const std::shared_ptr<SchedulePolicyPlugin> &plugin) override;
    bool UnRegisterPolicy(const std::string &name) override;

    ScheduleResults SelectFeasible(const std::shared_ptr<ScheduleContext> &ctx,
                                   const resource_view::InstanceInfo &instance,
                                   const resource_view::ResourceUnit &resourceUnit, uint32_t expectedFeasible) override;
private:
    std::shared_ptr<PreFilterResult> PreFilter(const std::shared_ptr<ScheduleContext> &ctx,
                                               const resource_view::InstanceInfo &instance,
                                               const resource_view::ResourceUnit &resourceUnit);
    struct FilterStatus {
        Status status = Status::OK();
        // When isFatalErr is set to true, the framework exits the current scheduling loop. while status is ok,
        // isFatalErr would be ignored.
        bool isFatalErr = false;
        int32_t availableForRequest = 0;
        // required resource or affinity info
        std::string required{ "" };
    };
    FilterStatus Filter(const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                  const resource_view::ResourceUnit &resourceUnit);

    NodeScore Score(const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                  const resource_view::ResourceUnit &resourceUnit);

    bool IsReachRelaxed(const std::priority_queue<NodeScore> &feasible, uint32_t expectedFeasible) const;

    std::unordered_map<std::string, double> scorePluginWeight;
    using Plugins = std::map<std::string, std::shared_ptr<SchedulePolicyPlugin>>;
    std::unordered_map<PolicyType, Plugins> plugins_;
    std::string latelySelected;
    int32_t relaxed_ = -1;
};
}  // namespace functionsystem::schedule_framework
#endif  // SCHEDULER_FRAMEWORK_IMPL_H
