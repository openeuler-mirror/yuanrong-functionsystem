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

#include "instance_cpu_collector.h"

#include <regex>

#include "constants.h"
#include "logs/logging.h"

namespace functionsystem::runtime_manager {

InstanceCPUCollector::InstanceCPUCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                                           const std::string &deployDir)
    : InstanceCPUCollector(pid, instanceID, limit, deployDir, std::make_shared<ProcFSTools>())
{
}

InstanceCPUCollector::InstanceCPUCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                                           const std::string &deployDir,
                                           const std::shared_ptr<ProcFSTools> &procFSTools)
    : BaseInstanceCollector(pid, instanceID, limit, deployDir),
      BaseMetricsCollector(metrics_type::CPU, collector_type::INSTANCE, procFSTools)
{
}

std::string InstanceCPUCollector::GenFilter() const
{
    // functionUrn-instanceId-memory
    return litebus::os::Join(litebus::os::Join(deployDir_, instanceID_, '-'), metricsType_, '-');
}

litebus::Future<Metric> InstanceCPUCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("instance cpu collector get usage.");
    auto start = GetCpuJiffies(pid_, procFSTools_);
    if (start.IsNone()) {
        YRLOG_ERROR("get cpu jiffies from pid {} failed.", pid_);
        return Metric{ {}, instanceID_, {}, {}};
    }

    litebus::Promise<Metric> promise;
    litebus::TimerTools::AddTimer(
        instance_metrics::CPU_CAL_INTERVAL, "TriggerAWhile",
        [promise, start, instanceID = instanceID_, fun = GetCpuJiffies, pid = pid_, procFSTools = procFSTools_]() {
            auto end = fun(pid, procFSTools);
            Metric metric;
            metric.instanceID = instanceID;
            if (end.IsNone()) {
                promise.SetValue(metric);
                return;
            }
            metric.value = (end.Get() - start.Get()) * instance_metrics::CPU_SCALE / instance_metrics::CPU_CAL_INTERVAL;
            promise.SetValue(metric);
        });
    return promise.GetFuture();
}

// Get cpu jiffy from /proc/pid/stat
litebus::Option<double> InstanceCPUCollector::GetCpuJiffies(const pid_t &pid,
                                                            const std::shared_ptr<ProcFSTools> procFSTools)
{
    auto path = instance_metrics::PROCESS_STAT_PATH_EXPRESS;
    path = path.replace(path.find('?'), 1, std::to_string(pid));
    if (procFSTools == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return {};
    }

    auto realPath = litebus::os::RealPath(path);
    if (realPath.IsNone()) {
        YRLOG_ERROR("failed to get realpath: {}", path);
        return {};
    }

    auto stat = procFSTools->Read(path);
    if (stat.IsNone() || stat.Get().empty()) {
        YRLOG_ERROR("read content from {} failed.", path);
        return {};
    }
    YRLOG_DEBUG_COUNT_60("read stat {} from {}.", stat.Get(), path);
    return CalJiffiesForCPUProcess(stat.Get());
}

litebus::Option<double> InstanceCPUCollector::CalJiffiesForCPUProcess(const std::string &stat)
{
    auto stats = litebus::strings::Split(stat, " ");
    if (stats.size() != instance_metrics::PROCESS_CPU_STAT_LEN) {
        YRLOG_ERROR("stat size is not equal {}", instance_metrics::PROCESS_CPU_STAT_LEN);
        return {};
    }

    auto utime = 0;
    auto stime = 0;
    auto cutime = 0;
    auto cstime = 0;
    try {
        utime = std::stoi(stats[instance_metrics::CPU_UTIME_INDEX]);
        stime = std::stoi(stats[instance_metrics::CPU_STIME_INDEX]);
        cutime = std::stoi(stats[instance_metrics::CPU_CUTIME_INDEX]);
        cstime = std::stoi(stats[instance_metrics::CPU_CSTIME_INDEX]);
    } catch (const std::exception &e) {
        YRLOG_ERROR("stoi fail, error:{}", e.what());
        return {};
    }
    double value = static_cast<double>(utime + stime + cutime + cstime);
    return value;
}

Metric InstanceCPUCollector::GetLimit() const
{
    YRLOG_DEBUG_COUNT_60("instance cpu collector get limit.");
    Metric metric;
    metric.value = limit_;
    metric.instanceID = instanceID_;
    return metric;
}

}  // namespace functionsystem::runtime_manager