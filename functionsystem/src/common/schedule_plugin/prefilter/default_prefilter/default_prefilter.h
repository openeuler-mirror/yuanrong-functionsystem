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

#ifndef FUNCTIONSYSTEM_DEFAULT_PREFILTER_H
#define FUNCTIONSYSTEM_DEFAULT_PREFILTER_H

#include <memory>
#include <string>

#include "common/resource_view/resource_type.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/framework/policy.h"
#include "common/scheduler_framework/utils/score.h"
#include "common/status/status.h"

namespace functionsystem::schedule_plugin::prefilter {

const std::string MONOPOLY_MODE = "monopoly";

struct InstanceInfo {
    std::string instanceID;
    std::string requestID;
    std::string policy;
    double cpuVal;
    double memVal;
};

class DefaultPreFilter : public schedule_framework::PreFilterPlugin {
public:
    DefaultPreFilter() = default;
    ~DefaultPreFilter() override = default;
    std::string GetPluginName() override
    {
        return DEFAULT_PREFILTER_NAME;
    }
    std::shared_ptr<schedule_framework::PreFilterResult> PreFilter(
        const std::shared_ptr<schedule_framework::ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
        const resource_view::ResourceUnit &resourceUnit) override;

private:
    std::shared_ptr<schedule_framework::PreFilterResult> PrecisePreFilter(
        const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx,
        const resource_view::ResourceUnit &resourceUnit, const InstanceInfo &inst) const;

    std::shared_ptr<schedule_framework::PreFilterResult> CommonPreFilter(
        const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx,
        const resource_view::ResourceUnit &resourceUnit, const InstanceInfo &inst) const;
};
}  // namespace functionsystem::schedule_plugin::prefilter

#endif  // FUNCTIONSYSTEM_DEFAULT_PREFILTER_H
