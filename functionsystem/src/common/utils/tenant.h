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

#ifndef COMMON_UTILS_TENANT_H
#define COMMON_UTILS_TENANT_H

#include <string>

namespace functionsystem {

[[maybe_unused]] static std::string GetAgentPodIpFromRuntimeAddress(const std::string &runtimeAddress)
{
    std::string agentPodIp;
    std::string::size_type pos = runtimeAddress.find(':');
    if (pos != std::string::npos) {
        // Get pod IP address from the runtimeAddress, note it's host IP in process deploy mode
        agentPodIp = runtimeAddress.substr(0, pos);
    }
    return agentPodIp;
}

}  // namespace functionsystem

#endif  // COMMON_UTILS_TENANT_H