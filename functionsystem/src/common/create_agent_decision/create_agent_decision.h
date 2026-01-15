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

#ifndef COMMON_CREATE_AGENT_DECISION_CREATE_AGENT_DECISION_H
#define COMMON_CREATE_AGENT_DECISION_CREATE_AGENT_DECISION_H

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"

namespace functionsystem {
const std::set<std::string> POOLABLE_RESOURCES_KEYS{ resource_view::CPU_RESOURCE_NAME,
                                                     resource_view::MEMORY_RESOURCE_NAME };

[[maybe_unused]] static bool NeedCreateAgentByPoolID(const resources::InstanceInfo &info)
{
    if (auto it = info.createoptions().find(AFFINITY_POOL_ID);
        it != info.createoptions().end() && !it->second.empty()) {
        return true;
    }
    return false;
}

[[maybe_unused]] static bool NeedCreateAgent(const resources::InstanceInfo &info)
{
    if (info.scheduleoption().schedpolicyname() != MONOPOLY_SCHEDULE) {
        return false;
    }
    if (auto iter = info.createoptions().find(RESOURCE_OWNER_KEY);
        iter != info.createoptions().end() && iter->second == STATIC_FUNCTION_OWNER_VALUE) {
        YRLOG_DEBUG("{}|instance({}) is static function, no need to create new agent", info.requestid(),
                    info.instanceid());
        return false;
    }
    if (info.createoptions().find(DELEGATE_CONTAINER) != info.createoptions().end()) {
        YRLOG_DEBUG("instance({}) has delegate container, need to create new agent", info.instanceid());
        return true;
    }

    if (auto iter = info.createoptions().find(RESOURCE_OWNER_KEY);
        iter != info.createoptions().end() && iter->second == SYSTEM_OWNER_VALUE) {
        YRLOG_DEBUG("instance({}) is system function, need to create new agent", info.instanceid());
        return true;
    }

    // if has other resource types, it must be dynamically scheduled
    if (info.resources().resources().size() > POOLABLE_RESOURCES_KEYS.size()) {
        YRLOG_DEBUG("instance({}) has custom resource, need to create new agent", info.instanceid());
        return true;
    }
    return false;
}

[[maybe_unused]] static bool NeedCreateAgentInDomain(const resources::InstanceInfo &info,
                                                     const int32_t scheduleRespCode)
{
    // if createOptions contains AFFINITY_POOL_ID, need to create agent
    if (NeedCreateAgentByPoolID(info)) {
        return true;
    }
    if (scheduleRespCode == static_cast<int32_t>(StatusCode::AFFINITY_SCHEDULE_FAILED)) {
        return false;
    }
    if (!info.scheduleoption().affinity().instanceaffinity().affinity().empty()) {
        YRLOG_DEBUG("instance({}) has an affinity attribute, need to create new agent", info.instanceid());
        return true;
    }
    if (auto it = info.createoptions().find(ENABLE_HORIZONTAL_SCALE_KEY);
        it != info.createoptions().end() && it->second == "true"
        && (scheduleRespCode == static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH)
            || scheduleRespCode == static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH))) {
        YRLOG_INFO("Enable horizontalScale. Create a new agent for instance({})", info.instanceid());
        return true;
    }

    return NeedCreateAgent(info);
}

}  // namespace functionsystem

#endif  // COMMON_CREATE_AGENT_DECISION_CREATE_AGENT_DECISION_H