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

#include "node_cpu_collector.h"

#include <regex>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "runtime_manager/utils/utils.h"

namespace functionsystem::runtime_manager {
constexpr double CPU_CALC_BASE = 1000.0;

NodeCPUCollector::NodeCPUCollector() : NodeCPUCollector(std::make_shared<ProcFSTools>())
{
}

NodeCPUCollector::NodeCPUCollector(const std::shared_ptr<ProcFSTools> procFSTools, double overheadCPU)
    : BaseMetricsCollector(metrics_type::CPU, collector_type::NODE, procFSTools), overheadCPU_(overheadCPU)
{
}

litebus::Future<Metric> NodeCPUCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("system cpu collector get usage.");
    litebus::Promise<Metric> promise;
    Metric metric;
    return metric;
}

Metric NodeCPUCollector::GetLimit() const
{
    YRLOG_DEBUG_COUNT_60("system cpu collector get limit.");
    Metric metric;

    if (!procFSTools_) {
        return metric;
    }

    auto cpuinfoOption = procFSTools_->Read("/proc/cpuinfo");
    if (cpuinfoOption.IsNone()) {
        return metric;
    }

    auto cpuinfo = cpuinfoOption.Get();
    auto cpuinfos = Utils::SplitByFunc(cpuinfo, [](const char &ch) -> bool { return ch == '\n' || ch == '\r'; });

    int cpuNum = 0;
    const std::regex re(R"(^processor\s*:\s*\d+\s*$)");  // eg.processor    : 15
    for (long unsigned int i = 0; i < cpuinfos.size(); i++) {
        if (std::regex_match(cpuinfos[i], re)) {
            cpuNum++;
        }
    }
    metric.value = cpuNum * CPU_CALC_BASE - overheadCPU_;
    return metric;
}

std::string NodeCPUCollector::GenFilter() const
{
    // node-cpu
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

}  // namespace functionsystem::runtime_manager