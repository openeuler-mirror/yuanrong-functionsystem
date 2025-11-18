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

#ifndef FUNCTION_PROXY_COMMON_POSIX_CLIENT_CLIENT_MANAGER_INSTANCE_CLIENTS_H
#define FUNCTION_PROXY_COMMON_POSIX_CLIENT_CLIENT_MANAGER_INSTANCE_CLIENTS_H

#include <memory>
#include <string>
#include <unordered_map>

#include "async/future.hpp"
#include "function_proxy/common/posix_client/base_client.h"
#include "function_proxy/common/posix_client/control_plane_client/control_interface_posix_client.h"
#include "common/status/status.h"

namespace functionsystem {
struct NewClientInfo {
    std::string instanceID;
    std::string runtimeID;
    std::string address;
    int64_t timeoutSec;
    int32_t maxGrpcSize;
};
class InstanceClients {
public:
    InstanceClients() = default;
    virtual ~InstanceClients();

    virtual std::shared_ptr<BaseClient> InsertClient(const std::string &instanceID,
                                                     const std::shared_ptr<BaseClient> &client);
    virtual Status DeleteClient(const std::string &instanceID);
    virtual std::shared_ptr<BaseClient> GetClient(const std::string &instanceID);
    virtual litebus::Future<std::shared_ptr<BaseClient>> GetReadyClient(const std::string &instanceID);

    virtual litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> UpdateClient(
        const NewClientInfo &newClientInfo, const std::shared_ptr<grpc::PosixClient> &posix);

protected:
    std::unordered_map<std::string, std::shared_ptr<BaseClient>> instanceClients_;
    std::unordered_map<std::string, litebus::Promise<std::shared_ptr<BaseClient>>> instanceClientPromises_;
    std::mutex mut_;
};
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_POSIX_CLIENT_CLIENT_MANAGER_INSTANCE_CLIENTS_H
