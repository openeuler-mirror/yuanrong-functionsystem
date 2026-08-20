/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_OWNER_H
#define FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_OWNER_H

#include "common/constants/signal.h"
#include "common/types/instance_state.h"
#include "common/utils/meta_store_kv_operation.h"

namespace functionsystem {

inline bool IsPausedInstanceManagerOwned(const resources::InstanceInfo &instanceInfo)
{
    return instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::PAUSED)
        && instanceInfo.functionproxyid() == INSTANCE_MANAGER_OWNER;
}

inline bool ShouldDeleteWithoutScheduler(const resources::InstanceInfo &instanceInfo, int32_t signal)
{
    const bool fatalWithoutLiveOwner =
        instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::FATAL)
        && (signal == FAMILY_EXIT_SIGNAL || instanceInfo.functionproxyid() == INSTANCE_MANAGER_OWNER
            || instanceInfo.functionproxyid().empty());
    return fatalWithoutLiveOwner || IsPausedInstanceManagerOwned(instanceInfo);
}

}  // namespace functionsystem

#endif  // FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_OWNER_H
