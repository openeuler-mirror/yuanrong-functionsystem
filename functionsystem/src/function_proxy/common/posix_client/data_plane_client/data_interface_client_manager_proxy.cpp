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

#include "data_interface_client_manager_proxy.h"

#include "async/async.hpp"
#include "common/logs/logging.h"
#include "data_interface_client_manager.h"

namespace functionsystem {

litebus::Future<std::shared_ptr<DataInterfacePosixClient>> DataInterfaceClientManagerProxy::GetDataInterfacePosixClient(
    const std::string &instanceID)
{
    return litebus::Async(aid_, &DataInterfaceClientManager::GetDataInterfacePosixClient, instanceID);
}

litebus::Future<std::shared_ptr<DataInterfacePosixClient>> DataInterfaceClientManagerProxy::NewDataInterfacePosixClient(
    const std::string &, const std::string &, const std::string &)
{
    // Currently not support for data client create
    // should to abstract posix stream in the future
    YRLOG_ERROR("DataInterfaceClientManagerProxy currently does not support to new date plane client");
    return nullptr;
}

litebus::Future<Status> DataInterfaceClientManagerProxy::DeleteClient(const std::string &instanceID)
{
    return litebus::Async(aid_, &DataInterfaceClientManager::DeleteClient, instanceID);
}
}  // namespace functionsystem