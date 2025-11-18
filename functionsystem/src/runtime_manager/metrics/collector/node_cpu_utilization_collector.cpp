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

#include "node_cpu_utilization_collector.h"

#include <numeric>

#include "utils/utils.h"

namespace functionsystem::runtime_manager {

NodeCPUUtilizationCollector::NodeCPUUtilizationCollector()
    : NodeCPUUtilizationCollector(std::make_shared<ProcFSTools>())
{
}

NodeCPUUtilizationCollector::NodeCPUUtilizationCollector(const std::shared_ptr<ProcFSTools> procFSTools)
    : BaseMetricsCollector(metrics_type::CPU, collector_type::NODE, procFSTools)
{
}

litebus::Future<Metric> NodeCPUUtilizationCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("system cpu collector get usage.");
    Metric metric;
    if (procFSTools_ == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return metric;
    }

    auto startCpuTime = CalCPUTime(procFSTools_);

    litebus::Promise<Metric> promise;
    auto timerCallback = [promise, startCpuTime, procFSTools = procFSTools_, this]() {
        auto endCpuTime = CalCPUTime(procFSTools);
        if (endCpuTime.empty()) {
            promise.SetValue(Metric{});
            return;
        }
        Metric metric;
        unsigned long startTotal = std::accumulate(startCpuTime.begin(), startCpuTime.end(), 0UL);
        unsigned long startIdle = startCpuTime[3] + startCpuTime[4];  // idle + iowait
        std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_CPU_INTERVAL_));
        unsigned long endTotal = std::accumulate(endCpuTime.begin(), endCpuTime.end(), 0UL);
        unsigned long endIdle = endCpuTime[3] + endCpuTime[4];  // idle + iowait
        if (endTotal == startTotal) {
            metric.value = 0.0;
        } else {
            metric.value =
            100.0 * (1.0 - static_cast<double>(endIdle - startIdle) / static_cast<double>(endTotal - startTotal));
        }
        promise.SetValue(metric);
    };

    litebus::TimerTools::AddTimer(CPU_CAL_INTERVAL_, "TriggerCPUUtilization", timerCallback);

    return promise.GetFuture();
}

Metric NodeCPUUtilizationCollector::GetLimit() const
{
    YRLOG_DEBUG_COUNT_60("system memory collector get usage.");
    litebus::Promise<Metric> promise;
    Metric metric;
    return metric;
}

std::string NodeCPUUtilizationCollector::GenFilter() const
{
    // node-cpu
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

std::vector<unsigned long> NodeCPUUtilizationCollector::CalCPUTime(const std::shared_ptr<ProcFSTools> procFSTools) const
{
    auto cpuinfoOption = procFSTools_->Read("/proc/stat");
    if (cpuinfoOption.IsNone()) {
        YRLOG_WARN("can not get info from /proc/stat");
        return {};
    }

    auto content = cpuinfoOption.Get();

    // content first line is like 'cpu  37277813 25 553311 226585434 38509 386500 107559 0 0 0\n'
    size_t pos = content.find('\n');
    if (pos == std::string::npos) {
        YRLOG_WARN("invalid /proc/stat content");
        return {};
    }

    std::string firstLine = content.substr(0, pos);
    std::istringstream iss(firstLine);
    std::string label;
    iss >> label; // drop 'cpu' string

    std::vector<unsigned long> cpuStats;
    unsigned long value;
    while (iss >> value) {
        cpuStats.push_back(value);
    }
    if (cpuStats.size() < MIN_VALID_TIME_ITEM_) {
        YRLOG_WARN("invalid cpu info, less than four param");
        return {};
    }

    return cpuStats;
}
}