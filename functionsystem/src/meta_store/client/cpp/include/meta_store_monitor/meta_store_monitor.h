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

#ifndef COMMON_META_STORE_ADAPTER_META_STORE_MONITOR_H
#define COMMON_META_STORE_ADAPTER_META_STORE_MONITOR_H

#include <chrono>
#include <memory>

#include "actor/actor.hpp"
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "meta_store_client/maintenance_client.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/metrics/metrics_adapter.h"
#include "timer/timer.hpp"

namespace functionsystem {

class MetaStoreMonitorActor : public litebus::ActorBase {
public:
    MetaStoreMonitorActor(const std::string &address,
                          const MetaStoreMonitorParam &param,
                          const std::shared_ptr<meta_store::MaintenanceClient> &metaStoreClient);
    ~MetaStoreMonitorActor() override = default;
    void Finalize() override;
    void StartMonitor();
    void CheckMetaStoreStatus();
    void RegisterHealthyObserver(const std::shared_ptr<MetaStoreHealthyObserver> &observer);
    bool ClearObservers();

    // for test
    [[maybe_unused]] void SetAlarmLevel(metrics::AlarmLevel alarmLevel)
    {
        alarmLevel_ = alarmLevel;
    }
private:
    void OnCheckMetaStoreStatus(const litebus::Future<StatusResponse> &response);
    void OnMetaStoreHealthy();
    void OnMetaStoreUnhealthy(const litebus::Future<StatusResponse> &response);
    void ResetUnHealthy();
    void IncreaseUnHealthy(const Status &status);

    std::shared_ptr<meta_store::MaintenanceClient> client_{ nullptr };
    uint32_t failedTimes_ = 0;
    std::string address_;
    MetaStoreMonitorParam param_;
    std::vector<std::shared_ptr<MetaStoreHealthyObserver>> observers_;
    litebus::Timer timer_;
    std::chrono::time_point<std::chrono::steady_clock> firingBeginTime_;
    metrics::AlarmLevel alarmLevel_ = metrics::AlarmLevel::OFF;
    bool isChecking_{ false };
};

class MetaStoreMonitor {
public:
    MetaStoreMonitor(const std::string &address, const MetaStoreMonitorParam &param,
                     const std::shared_ptr<meta_store::MaintenanceClient> &metaStoreClient);
    ~MetaStoreMonitor();

    Status CheckMetaStoreConnected();

    void StartMonitor();

    void RegisterHealthyObserver(const std::shared_ptr<MetaStoreHealthyObserver> &observer);

    litebus::Future<bool> ClearObservers();
private:
    std::shared_ptr<meta_store::MaintenanceClient> client_{ nullptr };
    std::shared_ptr<MetaStoreMonitorActor> actor_{ nullptr };
};
}  // namespace functionsystem

#endif