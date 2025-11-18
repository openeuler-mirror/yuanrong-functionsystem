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

#ifndef FUNCTION_PROXY_COMMON_POSIX_CLIENT_DATA_PLANE_CLIENT_DATA_INTERFACE_CLIENT_MANAGER_PROXY_H
#define FUNCTION_PROXY_COMMON_POSIX_CLIENT_DATA_PLANE_CLIENT_DATA_INTERFACE_CLIENT_MANAGER_PROXY_H

#include "async/future.hpp"
#include "common/status/status.h"
#include "data_interface_posix_client.h"
#include "litebus.hpp"
#include "common/status/status.h"

namespace functionsystem {
class DataInterfaceClientManagerProxy {
public:
    explicit DataInterfaceClientManagerProxy(const litebus::AID &aid) : aid_(aid)
    {
    }
    virtual ~DataInterfaceClientManagerProxy() = default;

    virtual litebus::Future<std::shared_ptr<DataInterfacePosixClient>> GetDataInterfacePosixClient(
        const std::string &instanceID);

    virtual litebus::Future<std::shared_ptr<DataInterfacePosixClient>> NewDataInterfacePosixClient(
        const std::string &instanceID, const std::string &runtimeID, const std::string &address);

    virtual litebus::Future<Status> DeleteClient(const std::string &instanceID);

protected:
    litebus::AID aid_;
};
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_POSIX_CLIENT_DATA_PLANE_CLIENT_DATA_INTERFACE_CLIENT_MANAGER_PROXY_H
