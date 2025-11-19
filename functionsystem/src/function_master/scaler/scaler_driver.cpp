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

#include "scaler_driver.h"

#include "common/constants/actor_name.h"

namespace functionsystem::scaler {
ScalerDriver::ScalerDriver(const functionsystem::functionmaster::Flags &flags,
                           const std::shared_ptr<MetaStoreClient> &metaStoreClient,
                           const std::shared_ptr<MetaStoreClient> &upgradeWatchClient,
                           const std::shared_ptr<KubeClient> &kubeClient,
                           const ScalerHandlers &handlers)
    : flags_(flags)
{
    metaStorageAccessor_ = std::make_shared<MetaStorageAccessor>(metaStoreClient);
    client_ = kubeClient;
    handlers_.evictAgentHandler = handlers.evictAgentHandler;
    if (flags_.GetSystemUpgradeWatchEnable() && upgradeWatchClient != nullptr) {
        systemUpgradeWatcher_ = std::make_shared<MetaStorageAccessor>(upgradeWatchClient);
        handlers_ = handlers;
    }
    if (flags.GetEnableFrontendPool()) {
        handlers_.scaleUpSystemFuncHandler = handlers.scaleUpSystemFuncHandler;
    }
}

Status ScalerDriver::Start()
{
    if (flags_.GetK8sBasePath().empty()) {
        YRLOG_WARN("invalid k8s path, maybe not supported.");
        return Status::OK();
    }

    // create meta store accessor
    if (metaStorageAccessor_ == nullptr) {
        return Status(StatusCode::FAILED, "failed to create meta store accessor.");
    }

    ScalerParams params{ .k8sNamespace = flags_.GetK8sNamespace(),
                         .gracePeriodSeconds = flags_.GetGracePeriodSeconds(),
                         .systemUpgradeParam = { .isEnabled = flags_.GetSystemUpgradeWatchEnable(),
                                                 .systemUpgradeKey = flags_.GetSystemUpgradeKey(),
                                                 .azID = flags_.GetAzID(),
                                                 .systemUpgradeWatcher = systemUpgradeWatcher_,
                                                 .handlers = handlers_ },
                         .clusterId = flags_.GetClusterID(),
                         .poolConfigPath = flags_.GetPoolConfigPath(),
                         .agentTemplatePath = flags_.GetAgentTemplatePath(),
                         .enableFrontendPool = flags_.GetEnableFrontendPool() };
    actor_ = std::make_shared<scaler::ScalerActor>(SCALER_ACTOR, client_, metaStorageAccessor_, params);
    actor_->ParseParams(flags_);
    (void)litebus::Spawn(actor_);
    // watch mate store events (after all the actors are spawned)
    (void)litebus::Async(actor_->GetAID(), &ScalerActor::SyncNodes).Get();
    (void)litebus::Async(actor_->GetAID(), &ScalerActor::SyncDeploymentAndPodPool).Get();
    (void)litebus::Async(actor_->GetAID(), &ScalerActor::SyncPodAndInstance).Get();
    (void)litebus::Async(actor_->GetAID(), &ScalerActor::Register).Get();
    return litebus::Async(actor_->GetAID(), &ScalerActor::Start).Get();  // await
}

Status ScalerDriver::Stop()
{
    if (actor_ != nullptr) {
        litebus::Terminate(actor_->GetAID());
    }
    return Status::OK();
}

void ScalerDriver::Await()
{
    if (actor_ != nullptr) {
        litebus::Await(actor_->GetAID());
    }
}
}  // namespace functionsystem::scaler