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
#include "meta_store_monitor_factory.h"
#include "common/logs/logging.h"

namespace functionsystem {

std::shared_ptr<MetaStoreMonitor> MetaStoreMonitorFactory::InsertMonitor(
    const std::string &address, const MetaStoreMonitorParam &param,
    const std::shared_ptr<meta_store::MaintenanceClient> &maintenanceClient)
{
    std::lock_guard<std::mutex> locker(mtx_);
    auto it = metaStoreMonitorMap_.find(address);
    if (it == metaStoreMonitorMap_.end()) {
        YRLOG_INFO("meta store {} not exist, create meta store monitor.", address);
        (void)metaStoreMonitorMap_.emplace(address,
                                           std::make_shared<MetaStoreMonitor>(address, param, maintenanceClient));
    }
    return metaStoreMonitorMap_[address];
}

std::shared_ptr<MetaStoreMonitor> MetaStoreMonitorFactory::GetMonitor(const std::string &address)
{
    std::lock_guard<std::mutex> locker(mtx_);
    if (auto iter = metaStoreMonitorMap_.find(address) ; iter != metaStoreMonitorMap_.end()) {
        return iter->second;
    }
    YRLOG_WARN("meta store({}) not exist.", address);
    return nullptr;
}

void MetaStoreMonitorFactory::Clear()
{
    std::lock_guard<std::mutex> locker(mtx_);
    metaStoreMonitorMap_.clear();
}

}  // namespace functionsystem