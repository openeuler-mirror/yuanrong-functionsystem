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

#include "instance_memory_collector.h"

#include <regex>

#include "common/constants/constants.h"
#include "common/logs/logging.h"

namespace functionsystem::runtime_manager {

InstanceMemoryCollector::InstanceMemoryCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                                                 const std::string &deployDir)
    : InstanceMemoryCollector(pid, instanceID, limit, deployDir, std::make_shared<ProcFSTools>())
{
}

InstanceMemoryCollector::InstanceMemoryCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                                                 const std::string &deployDir,
                                                 const std::shared_ptr<ProcFSTools> procFSTools)
    : BaseInstanceCollector(pid, instanceID, limit, deployDir),
      BaseMetricsCollector(metrics_type::MEMORY, collector_type::INSTANCE, procFSTools)
{
}

Metric InstanceMemoryCollector::GetLimit() const
{
    YRLOG_DEBUG_COUNT_60("instance memory collector get limit.");
    Metric metric;
    metric.value = limit_;
    metric.instanceID = instanceID_;
    return metric;
}

litebus::Future<Metric> InstanceMemoryCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("instance memory collector get usage.");
    // /proc/pid/status
    Metric metric;
    metric.instanceID = instanceID_;

    auto path = instance_metrics::PROCESS_STATUS_PATH_EXPRESS;
    path = path.replace(path.find('?'), 1, std::to_string(pid_));
    if (procFSTools_ == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return metric;
    }
    auto content = procFSTools_->Read(path);
    if (content.IsNone() || content.Get().empty()) {
        YRLOG_ERROR("read content from {} failed.", path);
        return metric;
    }

    auto status = content.Get();
    if (status.find(instance_metrics::MEMORY_SIZE_KEY) == std::string::npos) {
        YRLOG_ERROR("can not find {}.", instance_metrics::MEMORY_SIZE_KEY);
        return metric;
    }
    auto startIndex = status.find(instance_metrics::MEMORY_SIZE_KEY);

    // status: VmRSS: 884 kB
    auto subStatus = status.substr(startIndex);
    if (subStatus.find("kB") == std::string::npos) {
        YRLOG_ERROR("can not find kB.");
        return metric;
    }
    auto endIndex = subStatus.find("kB");

    status = status.substr(startIndex + instance_metrics::MEMORY_SIZE_KEY.length(),
                           endIndex - instance_metrics::MEMORY_SIZE_KEY.length());
    status = litebus::strings::Trim(status);
    YRLOG_DEBUG_COUNT_60("status is {}", status);
    double size = 0;
    try {
        size = std::stod(status);
    } catch (const std::exception &e) {
        YRLOG_ERROR("stod fail, error:{}", e.what());
        return metric;
    }

    metric.value = size / instance_metrics::MEMORY_SCALE;
    return metric;
}

std::string InstanceMemoryCollector::GenFilter() const
{
    // deployDir-instanceId-cpu
    return litebus::os::Join(litebus::os::Join(deployDir_, instanceID_, '-'), metricsType_, '-');
}
}  // namespace functionsystem::runtime_manager
