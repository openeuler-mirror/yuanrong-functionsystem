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

#ifndef COMMON_META_STORE_ADAPTER_META_STORE_MONITOR_FACTORY_H
#define COMMON_META_STORE_ADAPTER_META_STORE_MONITOR_FACTORY_H
#include "meta_store_client/maintenance_client.h"
#include "common/utils/singleton.h"
#include "meta_store_monitor.h"

namespace functionsystem {
class MetaStoreMonitorFactory : public Singleton<MetaStoreMonitorFactory> {
public:
    MetaStoreMonitorFactory() = default;
    ~MetaStoreMonitorFactory() override = default;

    /**
     * insert meta store monitor
     * If it does not exist, spawn & insert it. If it exists, skip it return existed.
     * @param address meta store address.
     * @return instance of MetaStoreMonitor.
     */
    std::shared_ptr<MetaStoreMonitor> InsertMonitor(
        const std::string &address,
        const MetaStoreMonitorParam &param,
        const std::shared_ptr<meta_store::MaintenanceClient> &maintenanceClient);

    std::shared_ptr<MetaStoreMonitor> GetMonitor(const std::string &address);

    void Clear();
private:
    std::mutex mtx_;
    std::map<std::string, std::shared_ptr<MetaStoreMonitor>> metaStoreMonitorMap_;
};
}  // namespace functionsystem
#endif  // COMMON_META_STORE_ADAPTER_META_STORE_MONITOR_FACTORY_H
