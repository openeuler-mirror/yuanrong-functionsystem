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

#include "instance_manager_driver.h"

namespace functionsystem::instance_manager {
InstanceManagerDriver::InstanceManagerDriver(std::shared_ptr<InstanceManagerActor> instanceManagerActor,
                                             std::shared_ptr<GroupManagerActor> groupManagerActor
                                             )
    : instanceManagerActor_(instanceManagerActor),
      groupManagerActor_(groupManagerActor)
{
}

Status InstanceManagerDriver::Start()
{
    litebus::AID instanceManagerActorAID = litebus::Spawn(instanceManagerActor_, false);
    if (!instanceManagerActorAID.OK()) {
        return Status(FAILED, "failed to start instance_manager actor.");
    }

    litebus::AID groupManagerActorAID = litebus::Spawn(groupManagerActor_);
    if (!groupManagerActorAID.OK()) {
        return Status(FAILED, "failed to start group_manager actor.");
    }

    // create http server
    const std::string im = "instance-manager";
    httpServer_ = std::make_shared<HttpServer>(im);
    // add agent api route
    instanceApiRouteRegister_ = std::make_shared<InstancesApiRouter>();
    instanceApiRouteRegister_->InitQueryInstancesHandler(instanceManagerActor_);
    instanceApiRouteRegister_->InitQueryDebugInstancesHandler(instanceManagerActor_);
    if (auto registerStatus(httpServer_->RegisterRoute(instanceApiRouteRegister_));
        registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register instance api router failed.");
    }
    if (httpServer_) {
        auto hsAID = litebus::Spawn(httpServer_);
    }

    return Status::OK();
}

Status InstanceManagerDriver::Stop()
{
    if (httpServer_) {
        litebus::Terminate(httpServer_->GetAID());
    }
    litebus::Terminate(instanceManagerActor_->GetAID());
    litebus::Terminate(groupManagerActor_->GetAID());
    return Status::OK();
}

void InstanceManagerDriver::Await()
{
    if (httpServer_) {
        litebus::Terminate(httpServer_->GetAID());
    }
    litebus::Await(instanceManagerActor_->GetAID());
    litebus::Await(groupManagerActor_->GetAID());
}
}  // namespace functionsystem::instance_manager
