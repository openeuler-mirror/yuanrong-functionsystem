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

#include "system_memory_collector.h"

#include <regex>

#include "common/constants/constants.h"
#include "common/logs/logging.h"

namespace functionsystem::runtime_manager {

SystemMemoryCollector::SystemMemoryCollector(const std::shared_ptr<ProcFSTools> procFSTools)
    : BaseMetricsCollector(metrics_type::MEMORY, collector_type::SYSTEM, procFSTools)
{
}

SystemMemoryCollector::SystemMemoryCollector() : SystemMemoryCollector(std::make_shared<ProcFSTools>())
{
}

Metric SystemMemoryCollector::GetLimit() const
{
    YRLOG_DEBUG_COUNT_60("system memory collector get limit.");
    return GetMemoryMetrics(system_metrics::MEMORY_LIMIT_PATH);
}

litebus::Future<Metric> SystemMemoryCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("system memory collector get usage.");
    litebus::Promise<Metric> promise;
    promise.SetValue({ GetMemoryMetrics(system_metrics::MEMORY_USAGE_PATH) });
    return promise.GetFuture();
}

std::string SystemMemoryCollector::GenFilter() const
{
    // system-memory
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

Metric SystemMemoryCollector::GetMemoryMetrics(const std::string &path) const
{
    if (procFSTools_ == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return Metric{};
    }

    auto content = procFSTools_->Read(path);
    if (content.IsNone() || content.Get().empty()) {
        YRLOG_ERROR("read content from {} failed.", path);
        return Metric{};
    }
    auto status = content.Get();
    status = litebus::strings::Trim(status);
    double data = 0;
    try {
        data = std::stod(status);
    } catch (const std::exception &e) {
        YRLOG_ERROR("stod fail, error:{}", e.what());
        return Metric{};
    }
    YRLOG_DEBUG_COUNT_60("get status: {}, from {}.", data, path);

    return Metric{ data / static_cast<double>(system_metrics::MEMORY_SCALE), {}, {}, {} };
}
}  // namespace functionsystem::runtime_manager