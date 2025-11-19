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

#include "resource_selector_filter.h"

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"

namespace functionsystem::schedule_plugin::filter {

std::string ResourceSelectorFilter::GetPluginName()
{
    return RESOURCE_SELECTOR_FILTER_NAME;
}

schedule_framework::Filtered ResourceSelectorFilter::Filter(
    const std::shared_ptr<schedule_framework::ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
    const resource_view::ResourceUnit &resourceUnit)
{
    // if resourceSelector is not enabled, return successful directly
    if (instance.scheduleoption().resourceselector_size() <= 0) {
        YRLOG_DEBUG("{}|there is not requirements of resource selector, ResourceSelectorPlugin does nothing",
                    instance.requestid());
        return schedule_framework::Filtered{ Status::OK(), false, -1 };
    }
    for (auto &itRs : instance.scheduleoption().resourceselector()) {
        bool defaultResourceOwner = itRs.first == RESOURCE_OWNER_KEY && itRs.second == DEFAULT_OWNER_VALUE;
        auto itLabel = resourceUnit.nodelabels().find(itRs.first);
        // When default owner is configured, the corresponding resource label is allowed if it does not exist.
        if (defaultResourceOwner && itLabel == resourceUnit.nodelabels().end()) {
            continue;
        }
        // if cannot find it, failed
        if (itLabel == resourceUnit.nodelabels().end()) {
            YRLOG_DEBUG("{}|ResourceSelectorPlugin doesn't find {}:{} in frag {} labels keys", instance.requestid(),
                        itRs.first, itRs.second, resourceUnit.id());
            return schedule_framework::Filtered{ Status(RESOURCE_NOT_ENOUGH, "Resource Require Label Not Found"), false,
                                                 -1 };
        }

        // if find it, check if value matches
        // if not find value in label values, failed
        if (itLabel->second.items().find(itRs.second) == itLabel->second.items().end()) {
            YRLOG_DEBUG("{}|ResourceSelectorPlugin doesn't find {}:{} in frag {} labels values", instance.requestid(),
                        itRs.first, itRs.second, resourceUnit.id());
            return schedule_framework::Filtered{ Status(RESOURCE_NOT_ENOUGH, "Resource Require Value Not Found"), false,
                                                 -1 };
        }
    }
    return schedule_framework::Filtered{ Status::OK(), false, -1 };
}

std::shared_ptr<schedule_framework::SchedulePolicyPlugin> ResourceSelectorFilterCreator()
{
    return std::make_shared<ResourceSelectorFilter>();
}

REGISTER_SCHEDULER_PLUGIN(RESOURCE_SELECTOR_FILTER_NAME, ResourceSelectorFilterCreator);
}  // namespace functionsystem::schedule_plugin::filter