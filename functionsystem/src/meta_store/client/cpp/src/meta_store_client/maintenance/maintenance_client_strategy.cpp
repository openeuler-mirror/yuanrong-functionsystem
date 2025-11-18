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

#include "maintenance_client_strategy.h"

#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"

namespace functionsystem::meta_store {

const uint32_t EXPLORE_RETRY_DURATION = 1000;

MaintenanceClientStrategy::MaintenanceClientStrategy(const std::string &name, const std::string &address,
                                                     const std::shared_ptr<MetaStoreExplorer> &explorer,
                                                     const MetaStoreTimeoutOption &timeoutOption)
    : litebus::ActorBase(name), address_(address), timeoutOption_(timeoutOption), explorer_(explorer)
{
}

void MaintenanceClientStrategy::Finalize()
{
    isRunning_.store(false);
}

void MaintenanceClientStrategy::Reconnected()
{
    YRLOG_INFO("MaintenanceClientStrategy reconnected");
    isReconnecting_ = false;
    explorer_->Explore().OnComplete(
        litebus::Defer(GetAID(), &MaintenanceClientStrategy::TriggerReconnectedCallbacks, std::placeholders::_1));
}

void MaintenanceClientStrategy::BindReconnectedCallBack(const std::function<void(const std::string &)> &callback)
{
    if (callback != nullptr) {
        reconnectedCallbacks_.push_back(callback);
    }
}

void MaintenanceClientStrategy::TriggerReconnectedCallbacks(const litebus::Future<std::string> &address)
{
    if (address.IsError()) {
        YRLOG_ERROR("failed to get current address from explorer, try again");
        litebus::AsyncAfter(EXPLORE_RETRY_DURATION, GetAID(), &MaintenanceClientStrategy::Reconnected);
        return;
    }

    std::string currentAddress = address.Get();
    for (auto callback : reconnectedCallbacks_) {
        if (callback) {
            callback(currentAddress);
        }
    }
}
}  // namespace functionsystem::meta_store
