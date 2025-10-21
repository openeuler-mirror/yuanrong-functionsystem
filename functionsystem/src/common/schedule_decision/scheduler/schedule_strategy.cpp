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
#include "schedule_strategy.h"

#include "common/schedule_plugin/common/plugin_factory.h"

namespace functionsystem::schedule_decision {

void ScheduleStrategy::RegisterSchedulePerformer(const std::shared_ptr<resource_view::ResourceView> &resourceView,
                                                 const std::shared_ptr<schedule_framework::Framework> &framework,
                                                 const PreemptInstancesFunc &func, const AllocateType &type)
{
    framework_ = framework;
    auto instancePerformer = std::make_shared<InstanceSchedulePerformer>(type);
    instancePerformer->BindResourceView(resourceView);
    instancePerformer->RegisterScheduleFramework(framework);
    instancePerformer->RegisterPreemptInstanceCallback(func);

    auto groupPerformer = std::make_shared<GroupSchedulePerformer>(type);
    groupPerformer->BindResourceView(resourceView);
    groupPerformer->RegisterScheduleFramework(framework);
    groupPerformer->RegisterPreemptInstanceCallback(func);

    auto aggregatedPerformer = std::make_shared<AggregatedSchedulePerformer>(type);
    aggregatedPerformer->BindResourceView(resourceView);
    aggregatedPerformer->RegisterScheduleFramework(framework);
    aggregatedPerformer->RegisterPreemptInstanceCallback(func);

    RegisterSchedulePerformer(instancePerformer, groupPerformer, aggregatedPerformer);
}

void ScheduleStrategy::RegisterSchedulePerformer(
    const std::shared_ptr<InstanceSchedulePerformer> &instancePerformer,
    const std::shared_ptr<GroupSchedulePerformer> &groupPerformer,
    const std::shared_ptr<AggregatedSchedulePerformer> &aggregatedPerformer)
{
    instancePerformer_ = instancePerformer;
    groupPerformer_ = groupPerformer;
    aggregatedPerformer_ = aggregatedPerformer;
}

litebus::Future<Status> ScheduleStrategy::RegisterPolicy(const std::string &policyName)
{
    RETURN_STATUS_IF_NULL(framework_, StatusCode::FAILED, "schedule framework nullptr");
    auto plugin = schedule_framework::PluginFactory::GetInstance().CreatePlugin(policyName);
    RETURN_STATUS_IF_NULL(plugin, StatusCode::FAILED, "invalid policy. policy not found ");
    auto success = framework_->RegisterPolicy(plugin);
    if (!success) {
        YRLOG_WARN("{} schedule policy may duplicated", policyName);
        return Status(StatusCode::FAILED, "duplicated schedule policy");
    }
    return Status::OK();
}

}  // namespace functionsystem::schedule_decision