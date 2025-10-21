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

#include "resource_group_manager_driver.h"

namespace functionsystem::resource_group_manager {
const std::string RESOURCE_GROUP = "resource-group";

Status ResourceGroupManagerDriver::Start()
{
    litebus::AID aid = litebus::Spawn(resourceGroupManagerActor_);
    if (!aid.OK()) {
        return Status(FAILED, "failed to start resource group manager actor.");
    }

    // http server
    httpServer_ = std::make_shared<HttpServer>(RESOURCE_GROUP);
    // add agent api route
    rGroupApiRouteRegister_ = std::make_shared<ResourceGroupApiRouter>();
    rGroupApiRouteRegister_->InitQueryRGroupHandler(resourceGroupManagerActor_);
    if (auto registerStatus(httpServer_->RegisterRoute(rGroupApiRouteRegister_));
        registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register resource group api router failed.");
    }
    if (httpServer_) {
        auto hsAID = litebus::Spawn(httpServer_);
    }
    return Status::OK();
}

Status ResourceGroupManagerDriver::Stop()
{
    if (httpServer_) {
        litebus::Terminate(httpServer_->GetAID());
    }
    litebus::Terminate(resourceGroupManagerActor_->GetAID());
    return Status::OK();
}

void ResourceGroupManagerDriver::Await()
{
    if (httpServer_) {
        litebus::Terminate(httpServer_->GetAID());
    }
    litebus::Await(resourceGroupManagerActor_->GetAID());
}
}  // namespace functionsystem::resource_group_manager