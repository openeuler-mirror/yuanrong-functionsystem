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

#ifndef FUNCTION_PROXY_COMMON_STATE_MACHINE_INSTANCE_LISTENER_H
#define FUNCTION_PROXY_COMMON_STATE_MACHINE_INSTANCE_LISTENER_H

#include <string>

namespace functionsystem {

class InstanceListener {
public:
    virtual ~InstanceListener()
    {
    }
    virtual void Update(const std::string &instanceID, const resources::InstanceInfo &instanceInfo,
                        bool isForceUpdate) = 0;
    virtual void Delete(const std::string &instanceID, int64_t modRevision) = 0;
};

}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_STATE_MACHINE_INSTANCE_LISTENER_H
