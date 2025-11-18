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

#include "node_disk_collector.h"
#include <iostream>
#include <sys/statvfs.h>
#include <cstring>
#include <cerrno>

namespace functionsystem::runtime_manager {

NodeDiskCollector::NodeDiskCollector(const std::shared_ptr<ProcFSTools> procFSTools)
    : BaseMetricsCollector(metrics_type::DISK, collector_type::NODE, procFSTools)
{
}

NodeDiskCollector::NodeDiskCollector() : NodeDiskCollector(std::make_shared<ProcFSTools>())
{
}

litebus::Future<Metric> NodeDiskCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("node disk collector get usage.");
    litebus::Promise<Metric> promise;
    Metric metric;
    // refresh diskinfo
    getFileSystemInfo(TARGET_PATH_);
    DevClusterMetrics devClusterMetrics;
    metric.devClusterMetrics = std::move(devClusterMetrics);
    metric.value = totalDiskSpace_ - availableDiskSpace_;
    promise.SetValue(metric);
    return promise.GetFuture();
}

Metric NodeDiskCollector::GetLimit() const
{
    YRLOG_DEBUG_COUNT_60("node disk collector get limit.");
    Metric metric;
    // no chache diskinfo
    if (totalDiskSpace_ == 0.0 && availableDiskSpace_ == 0.0) {
        getFileSystemInfo(TARGET_PATH_);
    }
    metric.value = totalDiskSpace_;
    return metric;
}

std::string NodeDiskCollector::GenFilter() const
{
    // node-disk
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

void NodeDiskCollector::getFileSystemInfo(const std::string& path) const
{
    totalDiskSpace_ = 0.0;
    availableDiskSpace_ = 0.0;
    struct statvfs vfs_info;
    if (statvfs(path.c_str(), &vfs_info) != 0) {
        YRLOG_WARN("acquire disk info failed");
        return;
    }
    uint64_t total_space = static_cast<uint64_t>(vfs_info.f_frsize) * vfs_info.f_blocks;
    uint64_t available_space = static_cast<uint64_t>(vfs_info.f_frsize) * vfs_info.f_bavail;
    totalDiskSpace_ = total_space / (CONVERSION_FACTOR_ * CONVERSION_FACTOR_ * CONVERSION_FACTOR_); // GB
    availableDiskSpace_ = available_space / (CONVERSION_FACTOR_ * CONVERSION_FACTOR_ * CONVERSION_FACTOR_); // GB
}
}