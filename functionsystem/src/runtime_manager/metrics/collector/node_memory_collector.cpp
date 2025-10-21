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

#include "node_memory_collector.h"

#include <regex>

#include "constants.h"
#include "logs/logging.h"
#include "runtime_manager/utils/utils.h"

namespace functionsystem::runtime_manager {
constexpr double MEMORY_CALC_BASE = 1024.0;

NodeMemoryCollector::NodeMemoryCollector(const std::shared_ptr<ProcFSTools> procFSTools, double overheadMemory)
    : BaseMetricsCollector(metrics_type::MEMORY, collector_type::NODE, procFSTools), overheadMemory_(overheadMemory)
{
}

NodeMemoryCollector::NodeMemoryCollector() : NodeMemoryCollector(std::make_shared<ProcFSTools>())
{
}

Metric NodeMemoryCollector::GetLimit() const
{
    YRLOG_DEBUG_COUNT_60("system memory collector get limit.");
    Metric metric;
    if (!procFSTools_) {
        return metric;
    }

    auto meminfoOption = procFSTools_->Read("/proc/meminfo");
    if (meminfoOption.IsNone()) {
        return metric;
    }

    auto meminfo = meminfoOption.Get();
    auto meminfos = Utils::SplitByFunc(meminfo, [](const char &ch) -> bool { return ch == '\n' || ch == '\r'; });

    const std::regex re(R"(^MemTotal\s*:\s*(\d+)\s*kB$)");  // MemTotal:       65409488 kB
    std::smatch matches;
    double memNum = 0.0;
    for (long unsigned int i = 0; i < meminfos.size(); i++) {
        if (!std::regex_search(meminfos[i], matches, re)) {
            continue;
        }

        try {
            memNum = std::stod(matches[1]);
        } catch (std::exception &e) {
            return metric;
        }
        metric.value = memNum / MEMORY_CALC_BASE - overheadMemory_;
        return metric;
    }

    return metric;
}

litebus::Future<Metric> NodeMemoryCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("system memory collector get usage.");
    litebus::Promise<Metric> promise;
    Metric metric;
    return metric;
}

std::string NodeMemoryCollector::GenFilter() const
{
    // node-memory
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

}  // namespace functionsystem::runtime_manager