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

#include "virtual_env_manager.h"

#include "async/async.hpp"

namespace functionsystem::runtime_manager {
VirtualEnvManager::VirtualEnvManager(int virtualEnvIdleTimeLimit)
{
    actor_ = std::make_shared<VirtualEnvMgrActor>(RUNTIME_MANAGER_VIRTUAL_ENV_MGR_ACTOR_NAME, virtualEnvIdleTimeLimit);
    litebus::Spawn(actor_);
}

VirtualEnvManager::~VirtualEnvManager()
{
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
}

void VirtualEnvManager::AddEnvReferInfos(const std::string &envName, const std::string &runtimeId)
{
    litebus::Async(actor_->GetAID(), &VirtualEnvMgrActor::AddEnvReferInfos, envName, runtimeId);
}

void VirtualEnvManager::RmEnvReferInfos(const std::string &runtimeId)
{
    litebus::Async(actor_->GetAID(), &VirtualEnvMgrActor::RmEnvReferInfos, runtimeId);
}

}  // namespace functionsystem::runtime_manager