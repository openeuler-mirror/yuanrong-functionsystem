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

#include "common/constants/constants.h"
#include "common/logs/logging.h"

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
    auto startProc = GetProcessCpuTime(pid_, procFSTools_);
    auto startTotal = GetTotalCpuTime(procFSTools_);
    std::this_thread::sleep_for(std::chrono::milliseconds(instance_metrics::CPU_CAL_INTERVAL));
    auto endProc = GetProcessCpuTime(pid_, procFSTools_);
    auto endTotal = GetTotalCpuTime(procFSTools_);
    if (endTotal == startTotal || startProc == 0 || startTotal == 0 || endProc == 0 || endTotal == 0) {
        YRLOG_ERROR("get cpu jiffies from pid {} failed.", pid_);
        return Metric{ {}, instanceID_, {}, {} };
    }
    litebus::Promise<Metric> promise;
    Metric metric;
    metric.instanceID = instanceID_;
    metric.value = instance_metrics::PERCENT_BASE * static_cast<double>(endProc - startProc)
                   / static_cast<double>(endTotal - startTotal);  // percent
    promise.SetValue(metric);
    return promise.GetFuture();
}

// Get cpu jiffy from /proc/stat
unsigned long long InstanceCPUCollector::GetTotalCpuTime(const std::shared_ptr<ProcFSTools> procFSTools) const
{
    if (procFSTools == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return 0;
    }
    auto stat = procFSTools->Read("/proc/stat");
    if (stat.IsNone() || stat.Get().empty()) {
        YRLOG_ERROR("read content from /proc/stat failed.");
        return 0;
    }
    auto content = stat.Get();
    // content first line is like 'cpu  37277813 25 553311 226585434 38509 386500 107559 0 0 0\n'
    size_t pos = content.find('\n');
    if (pos == std::string::npos) {
        YRLOG_WARN("invalid /proc/stat content");
        return 0;
    }
    std::string firstLine = content.substr(0, pos);
    std::istringstream ss(firstLine);
    std::string cpu;
    unsigned long long total = 0;
    unsigned long long val;
    ss >> cpu; // skip "cpu"
    while (ss >> val) {
        total += val;
    }
    return total;
}

// Get cpu jiffy from /proc/pid/stat
unsigned long long InstanceCPUCollector::GetProcessCpuTime(const pid_t &pid,
                                                            const std::shared_ptr<ProcFSTools> procFSTools) const
{
    auto path = instance_metrics::PROCESS_STAT_PATH_EXPRESS;
    path = path.replace(path.find('?'), 1, std::to_string(pid));
    if (procFSTools == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return 0;
    }

    auto realPath = litebus::os::RealPath(path);
    if (realPath.IsNone()) {
        YRLOG_ERROR("failed to get realpath: {}", path);
        return 0;
    }

    auto stat = procFSTools->Read(path);
    if (stat.IsNone() || stat.Get().empty()) {
        YRLOG_ERROR("read content from {} failed.", path);
        return 0;
    }
    YRLOG_DEBUG_COUNT_60("read stat {} from {}.", stat.Get(), path);
    return CalJiffiesForCPUProcess(stat.Get());
}

unsigned long long InstanceCPUCollector::CalJiffiesForCPUProcess(const std::string &stat) const
{
    std::istringstream ss(stat);
    std::string tmp;
    unsigned long long utime;
    unsigned long long stime;
    unsigned long long cutime;
    unsigned long long cstime;
    // Ignore the first 13 fields
    for (int i = 0; i < 13; i++) {
        ss >> tmp;
    }
    ss >> utime >> stime >> cutime >> cstime;
    return utime + stime + cutime + cstime;
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