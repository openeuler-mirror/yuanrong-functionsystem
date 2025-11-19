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

#include "common/constants/constants.h"
#include "common/logs/logging.h"
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
    YRLOG_DEBUG_COUNT_60("node memory collector get limit.");
    Metric metric;
    if (!procFSTools_) {
        return metric;
    }
    // no chache meminfo
    if (totalMem_ == 0.0 && availableMem_ == 0.0) {
        parseMemInfo();
    }
    metric.value = totalMem_ - overheadMemory_;
    return metric;
}

litebus::Future<Metric> NodeMemoryCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("node memory collector get usage.");
    litebus::Promise<Metric> promise;
    Metric metric;
    if (!procFSTools_) {
        return metric;
    }
    parseMemInfo();
    metric.value = totalMem_ - availableMem_;
    promise.SetValue(metric);
    return promise.GetFuture();
}

void NodeMemoryCollector::parseMemInfo() const
{
    totalMem_ = 0.0;
    availableMem_ = 0.0;

    if (!procFSTools_) {
        return;
    }
    auto meminfoOption = procFSTools_->Read("/proc/meminfo");
    if (meminfoOption.IsNone()) {
        return;
    }

    auto meminfo = meminfoOption.Get();
    auto meminfos = Utils::SplitByFunc(meminfo, [](const char &ch) -> bool { return ch == '\n' || ch == '\r'; });
    const std::regex totalRex(R"(^MemTotal\s*:\s*(\d+)\s*kB$)");
    const std::regex avaliRex(R"(^MemAvailable\s*:\s*(\d+)\s*kB$)");
    std::smatch matches;
    for (long unsigned int i = 0; i < meminfos.size() && (totalMem_ == 0.0 || availableMem_ == 0.0); i++) {
        if (std::regex_search(meminfos[i], matches, totalRex)) {
            try {
                totalMem_ = std::stod(matches[1]) / MEMORY_CALC_BASE;
            } catch (std::exception &e) {
                totalMem_ = 0.0;
                availableMem_ = 0.0;
                YRLOG_ERROR("can not set totalMem_ from meminfo");
                break;
            }
        } else if (std::regex_search(meminfos[i], matches, avaliRex)) {
            try {
                availableMem_ = std::stod(matches[1]) / MEMORY_CALC_BASE;
            } catch (std::exception &e) {
                totalMem_ = 0.0;
                availableMem_ = 0.0;
                YRLOG_ERROR("can not set availableMem_ from meminfo");
                break;
            }
        }
    }
}

std::string NodeMemoryCollector::GenFilter() const
{
    // node-memory
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

}  // namespace functionsystem::runtime_manager