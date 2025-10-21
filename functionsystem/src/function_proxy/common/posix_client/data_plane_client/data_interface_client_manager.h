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

#ifndef FUNCTION_PROXY_COMMON_POSIX_CLIENT_CLIENT_MANAGER_DATA_INTERFACE_CLIENT_MANAGER_H
#define FUNCTION_PROXY_COMMON_POSIX_CLIENT_CLIENT_MANAGER_DATA_INTERFACE_CLIENT_MANAGER_H

#include "function_proxy/common/posix_client/data_plane_client/data_interface_posix_client.h"
#include "function_proxy/common/posix_client/instance_clients.h"
#include "litebus.hpp"

namespace functionsystem {
class DataInterfaceClientManager : virtual public InstanceClients,
                                      virtual public litebus::ActorBase {
public:
    explicit DataInterfaceClientManager(const std::string &name) : litebus::ActorBase(name) {}
    ~DataInterfaceClientManager() override = default;

    std::shared_ptr<DataInterfacePosixClient> GetDataInterfacePosixClient(const std::string &instanceID);
};
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_POSIX_CLIENT_CLIENT_MANAGER_DATA_INTERFACE_CLIENT_MANAGER_H
