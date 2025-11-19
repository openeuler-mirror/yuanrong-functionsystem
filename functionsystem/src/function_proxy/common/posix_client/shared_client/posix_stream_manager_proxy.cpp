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
#include "posix_stream_manager_proxy.h"

#include "common/logs/logging.h"
#include "shared_client_manager.h"

namespace functionsystem {
litebus::Future<std::shared_ptr<DataInterfacePosixClient>> PosixStreamManagerProxy::NewDataInterfacePosixClient(
    const std::string &instanceID, const std::string &, const std::string &)
{
    return litebus::Async(aid_, &SharedClientManager::GetReadyClient, instanceID)
        .Then([](const std::shared_ptr<BaseClient> &client)
                  -> litebus::Future<std::shared_ptr<DataInterfacePosixClient>> {
            if (client == nullptr) {
                return nullptr;
            }
            return std::dynamic_pointer_cast<DataInterfacePosixClient>(client);
        });
}

litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> PosixStreamManagerProxy::NewControlInterfacePosixClient(
    const std::string &instanceID, const std::string &runtimeID, const std::string &address,
    std::function<void()> closedCb, int64_t timeoutSec, int32_t maxGrpcSize)
{
    const uint32_t thousands = 1000;
    return litebus::Async(aid_, &SharedClientManager::GetReadyClient, instanceID)
        .After(timeoutSec * thousands,
               [instanceID, runtimeID](const litebus::Future<std::shared_ptr<BaseClient>> &future) {
                   YRLOG_ERROR("{}|{}|Get ready client failed, timeout", instanceID, runtimeID);
                   return nullptr;
               })
        .Then([closedCb](const std::shared_ptr<BaseClient> &client)
                  -> litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> {
            if (client == nullptr) {
                return nullptr;
            }
            client->RegisterUserCallback(closedCb);
            return std::dynamic_pointer_cast<ControlInterfacePosixClient>(client);
        });
}

void PosixStreamManagerProxy::UpdateControlInterfacePosixClient(const std::string &instanceID,
                                                                const std::string &runtimeID,
                                                                const std::shared_ptr<grpc::PosixClient> &posix)
{
    NewClientInfo clientInfo{
        .instanceID = instanceID, .runtimeID = runtimeID, .address = {}, .timeoutSec = {}, .maxGrpcSize = {}
    };
    (void)litebus::Async(aid_, &SharedClientManager::UpdateClient, clientInfo, posix);
}

}  // namespace functionsystem