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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_DISK_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_DISK_COLLECTOR_H

#include <regex>
#include <nlohmann/json.hpp>

#include "base_metrics_collector.h"

namespace functionsystem::runtime_manager {

class DiskCollector : public BaseMetricsCollector {
public:
    DiskCollector();
    explicit DiskCollector(const std::string &diskConfig);
    ~DiskCollector() override = default;
    litebus::Future<Metric> GetUsage() const override;
    Metric GetLimit() const override;
    std::string GenFilter() const override;

private:
    bool InitFromConfig(const std::string& diskConfig);
    bool ValidateDiskConfig(const nlohmann::json& disk);
    void ParseDiskConfig(const nlohmann::json& disk);

    std::string uuid_;
    std::vector<int> diskSizes_;
    std::vector<resource_view::ResourceExtension> diskContents_;
};

}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_DISK_COLLECTOR_H
