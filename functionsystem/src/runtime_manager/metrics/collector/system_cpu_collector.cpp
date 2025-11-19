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

#include "system_cpu_collector.h"

#include <regex>

#include "common/constants/constants.h"
#include "common/logs/logging.h"

namespace functionsystem::runtime_manager {

SystemCPUCollector::SystemCPUCollector() : SystemCPUCollector(std::make_shared<ProcFSTools>())
{
}

SystemCPUCollector::SystemCPUCollector(const std::shared_ptr<ProcFSTools> procFSTools)
    : BaseMetricsCollector(metrics_type::CPU, collector_type::SYSTEM, procFSTools)
{
}

litebus::Future<Metric> SystemCPUCollector::GetUsage() const
{
    YRLOG_DEBUG_COUNT_60("system cpu collector get usage.");
    if (procFSTools_ == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return { Metric{} };
    }

    auto start = CalCPUUsage(procFSTools_);
    if (start.IsNone()) {
        YRLOG_ERROR("cal cpu usage failed.");
        return { Metric{} };
    }

    litebus::Promise<Metric> promise;
    auto timerCallback = [promise, start, func = CalCPUUsage, procFSTools = procFSTools_]() {
        auto end = func(procFSTools);
        if (end.IsNone()) {
            promise.SetValue(Metric{});
            return;
        }
        Metric metric;
        metric.value = (end.Get() - start.Get()) * system_metrics::CPU_SCALE / system_metrics::CPU_CAL_INTERVAL;
        promise.SetValue(metric);
    };

    litebus::TimerTools::AddTimer(system_metrics::CPU_CAL_INTERVAL, "TriggerAMoment", timerCallback);

    return promise.GetFuture();
}

Metric SystemCPUCollector::GetLimit() const
{
    YRLOG_DEBUG_COUNT_60("system cpu collector get limit.");
    Metric metric;
    if (procFSTools_ == nullptr) {
        YRLOG_ERROR("can not read content, procFSTool is nullptr.");
        return metric;
    }

    auto cpuPeriodData = procFSTools_->Read(system_metrics::CPU_CFS_PERIOD_PATH);
    if (cpuPeriodData.IsNone()) {
        YRLOG_ERROR("read cpu period data from {} failed.", system_metrics::CPU_CFS_PERIOD_PATH);
        return metric;
    }
    YRLOG_DEBUG_COUNT_60("cpu period data: {}.", cpuPeriodData.Get());

    auto cpuQuotaData = procFSTools_->Read(system_metrics::CPU_CFS_QUOTA_PATH);
    if (cpuQuotaData.IsNone()) {
        YRLOG_ERROR("read cpu quota data from {} failed.", system_metrics::CPU_CFS_QUOTA_PATH);
        return metric;
    }
    YRLOG_DEBUG_COUNT_60("cpu quota data: {}.", cpuQuotaData.Get());

    double cpuQuotaDataStatus = 0;
    double cpuPeriodDataStatus = 0;
    try {
        cpuQuotaDataStatus = std::stod(cpuQuotaData.Get());
        cpuPeriodDataStatus = std::stod(cpuPeriodData.Get());
    } catch (const std::exception &e) {
        YRLOG_ERROR("stod fail, error:{}", e.what());
        return metric;
    }

    if (abs(cpuPeriodDataStatus) < EPSINON) {
        YRLOG_ERROR("read cpu period data is 0.");
        return metric;
    }
    metric.value = cpuQuotaDataStatus * system_metrics::CPU_SCALE / cpuPeriodDataStatus;
    return metric;
}

std::string SystemCPUCollector::GenFilter() const
{
    // system-cpu
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

litebus::Option<double> SystemCPUCollector::CalCPUUsage(const std::shared_ptr<ProcFSTools> procFSTools)
{
    auto content = procFSTools->Read(system_metrics::CPU_USAGE_PATH);
    if (content.IsNone() || content.Get().empty()) {
        YRLOG_ERROR("read content from {} failed.", system_metrics::CPU_USAGE_PATH);
        return {};
    }

    // trim
    auto cpuAcct = content.Get();
    cpuAcct = litebus::strings::Trim(cpuAcct);
    YRLOG_DEBUG_COUNT_60("cpu acct is {}.", cpuAcct);
    double status = 0;
    try {
        status = std::stod(cpuAcct);
    } catch (const std::exception &e) {
        YRLOG_ERROR("stod fail, error:{}", e.what());
        return {};
    }
    return status;
}

}  // namespace functionsystem::runtime_manager