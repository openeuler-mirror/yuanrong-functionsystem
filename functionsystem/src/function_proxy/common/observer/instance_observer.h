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
#ifndef FUNCTION_PROXY_COMMON_OBSERVER_INSTANCE_OBSERVER_H
#define FUNCTION_PROXY_COMMON_OBSERVER_INSTANCE_OBSERVER_H

#include <memory>
#include <string>
#include "common/resource_view/resource_type.h"
#include "function_proxy/common/state_machine/instance_listener.h"

namespace functionsystem {

class InstanceObserver {
public:
    virtual ~InstanceObserver()
    {
    }
    virtual void Attach(const std::shared_ptr<InstanceListener> &listener) = 0;
    virtual void Detach(const std::shared_ptr<InstanceListener> &listener) = 0;
    virtual void NotifyUpdateInstance(const std::string &instanceID,
                                      const resource_view::InstanceInfo &instanceInfo, bool isForceUpdate) = 0;
    virtual void NotifyDeleteInstance(const std::string &instanceID, int64_t modRevision) = 0;
};

}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_OBSERVER_INSTANCE_OBSERVER_H
