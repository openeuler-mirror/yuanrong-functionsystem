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

#ifndef COMMON_META_STORE_MAINTENANCE_MAINTENANCE_CLIENT_STRATEGY_H
#define COMMON_META_STORE_MAINTENANCE_MAINTENANCE_CLIENT_STRATEGY_H

#include <string>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "meta_store_client/meta_store_struct.h"
#include "meta_store_client/utils/meta_store_explorer.h"

namespace functionsystem::meta_store {

class MaintenanceClientStrategy : public litebus::ActorBase {
public:
    MaintenanceClientStrategy(const std::string &name, const std::string &address,
                              const std::shared_ptr<MetaStoreExplorer> &explorer,
                              const MetaStoreTimeoutOption &timeoutOption);

    ~MaintenanceClientStrategy() override = default;
    virtual litebus::Future<StatusResponse> HealthCheck() = 0;
    virtual litebus::Future<bool> IsConnected() = 0;
    virtual void CheckChannelAndWaitForReconnect() = 0;

    void BindReconnectedCallBack(const std::function<void(const std::string &)> &callback);
    virtual void OnAddressUpdated(const std::string &address) = 0;
    void Reconnected();

protected:
    void Finalize() override;
    void TriggerReconnectedCallbacks(const litebus::Future<std::string> &address);

    std::atomic<bool> isRunning_ = true;
    bool isReconnecting_ = false;
    std::string address_;
    MetaStoreTimeoutOption timeoutOption_;
    std::vector<std::function<void(const std::string &)>> reconnectedCallbacks_;
    std::shared_ptr<MetaStoreExplorer> explorer_ = nullptr;
};
}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_MAINTENANCE_MAINTENANCE_CLIENT_STRATEGY_H
