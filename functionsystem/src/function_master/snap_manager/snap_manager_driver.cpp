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

#include "snap_manager_driver.h"

namespace functionsystem::snap_manager {
SnapManagerDriver::SnapManagerDriver(std::shared_ptr<SnapManagerActor> snapManagerActor)
    : snapManagerActor_(std::move(snapManagerActor))
{
}

Status SnapManagerDriver::Start()
{
    litebus::AID snapManagerActorAID = litebus::Spawn(snapManagerActor_, false);
    if (!snapManagerActorAID.OK()) {
        return Status(FAILED, "failed to start snap_manager actor.");
    }

    // create http server
    const std::string sm = "snap-manager";
    httpServer_ = std::make_shared<HttpServer>(sm);
    // add snap api route
    snapApiRouteRegister_ = std::make_shared<SnapApiRouter>();
    snapApiRouteRegister_->InitQuerySnapshotHandler(snapManagerActor_);
    snapApiRouteRegister_->InitListSnapshotsHandler(snapManagerActor_);
    snapApiRouteRegister_->InitListByFunctionKeyHandler(snapManagerActor_);
    snapApiRouteRegister_->InitListByTenantHandler(snapManagerActor_);
    snapApiRouteRegister_->InitDeleteSnapshotHandler(snapManagerActor_);
    if (auto registerStatus(httpServer_->RegisterRoute(snapApiRouteRegister_));
        registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register snap api router failed.");
    }
    if (httpServer_) {
        auto hsAID = litebus::Spawn(httpServer_);
    }

    return Status::OK();
}

Status SnapManagerDriver::Stop()
{
    if (httpServer_) {
        litebus::Terminate(httpServer_->GetAID());
    }
    litebus::Terminate(snapManagerActor_->GetAID());
    return Status::OK();
}

void SnapManagerDriver::Await()
{
    if (httpServer_) {
        litebus::Terminate(httpServer_->GetAID());
    }
    litebus::Await(snapManagerActor_->GetAID());
}
}  // namespace functionsystem::snap_manager
