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

#include "control_interface_client_manager_proxy.h"

#include <cstddef>

#include "async/async.hpp"
#include "control_interface_client_manager.h"
namespace functionsystem {

litebus::Future<std::shared_ptr<ControlInterfacePosixClient>>
ControlInterfaceClientManagerProxy::GetControlInterfacePosixClient(const std::string &instanceID)
{
    return litebus::Async(aid_, &ControlInterfaceClientManager::GetControlInterfacePosixClient, instanceID);
}

litebus::Future<std::shared_ptr<ControlInterfacePosixClient>>
ControlInterfaceClientManagerProxy::NewControlInterfacePosixClient(const std::string &instanceID,
                                                                   const std::string &runtimeID,
                                                                   const std::string &address,
                                                                   std::function<void()> closedCb, int64_t timeoutSec,
                                                                   int32_t maxGrpcSize)
{
    return litebus::Async(aid_, &ControlInterfaceClientManager::GetClient, instanceID)
        .Then([](const std::shared_ptr<BaseClient> &client)
                  -> litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> {
                return std::dynamic_pointer_cast<ControlInterfacePosixClient>(client);
        });
}

litebus::Future<Status> ControlInterfaceClientManagerProxy::DeleteClient(const std::string &instanceID)
{
    return litebus::Async(aid_, &ControlInterfaceClientManager::DeleteClient, instanceID);
}

}  // namespace functionsystem