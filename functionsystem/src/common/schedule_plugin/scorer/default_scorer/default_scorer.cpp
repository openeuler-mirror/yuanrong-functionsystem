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

#include "default_scorer.h"

#include "common/constants/constants.h"
#include "common/resource_view/scala_resource_tool.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "utils/string_utils.hpp"

namespace functionsystem::schedule_plugin::scorer {

std::string DefaultScorer::GetPluginName()
{
    return DEFAULT_SCORER_NAME;
}

schedule_framework::NodeScore DefaultScorer::Score(const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
                                                   const resource_view::InstanceInfo &instance,
                                                   const resource_view::ResourceUnit &resourceUnit)
{
    auto available = resourceUnit.allocatable();
    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    if (auto iter(preContext->allocated.find(resourceUnit.id())); iter != preContext->allocated.end()) {
        available = resourceUnit.allocatable() - iter->second.resource;
    }

    const auto &required = instance.resources().resources();
    int64_t calculated = 0;
    int64_t accumulated = 0;
    for (auto &req : required) {
        // hetero resource score in hetero scorer, disk resource score in disk scorer
        if (resource_view::IsHeterogeneousResource(req.first) || resource_view::IsDiskResource(req.first)) {
            continue;
        }
        // required number is zero don't need to score
        if (resource_view::ScalaValueIsEmpty(req.second)) {
            continue;
        }

        // if a pod is scoring, it must have request resource,
        // if it doesn't have, it is a monopoly instance, we don't verify other resources except CPU and MEM
        auto avail = available.resources().find(req.first);
        if (avail == available.resources().end()) {
            YRLOG_WARN("{} not find in agent resources", req.first);
            continue;
        }
        int64_t remain =
            static_cast<int64_t>((1.0f - req.second.scalar().value() / avail->second.scalar().value()) * 100);
        accumulated += remain;
        calculated++;
    }
    int64_t score = calculated > 0 ? accumulated / calculated : accumulated;
    return schedule_framework::NodeScore(score);
}

std::shared_ptr<schedule_framework::SchedulePolicyPlugin> DefaultScorerCreator()
{
    return std::make_shared<DefaultScorer>();
}

REGISTER_SCHEDULER_PLUGIN(DEFAULT_SCORER_NAME, DefaultScorerCreator);

}  // namespace functionsystem::schedule_plugin::scorer