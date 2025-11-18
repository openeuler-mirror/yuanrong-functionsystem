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

#ifndef FUNCTION_PROXY_COMMON_OBSERVER_DATA_OBSERVER_H
#define FUNCTION_PROXY_COMMON_OBSERVER_DATA_OBSERVER_H

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "async/option.hpp"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"

namespace functionsystem::function_proxy {
class ObserverActor;
class DataPlaneObserver {
public:
    explicit DataPlaneObserver(const std::shared_ptr<ObserverActor> &observerActor) : observerActor_(observerActor){};

    virtual ~DataPlaneObserver() = default;

    virtual litebus::Future<Status> SubscribeInstanceEvent(const std::string &subscriber,
                                                           const std::string &targetInstance,
                                                           bool ignoreNonExist = false);

    virtual void NotifyMigratingRequest(const std::string &instanceID);
private:
    std::shared_ptr<ObserverActor> observerActor_;
};

}  // namespace functionsystem::function_proxy

#endif  // FUNCTION_PROXY_COMMON_OBSERVER_DATA_OBSERVER_H
