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

#include "system_proc_cpu_collector.h"

namespace functionsystem::runtime_manager {

SystemProcCPUCollector::SystemProcCPUCollector(const double &limit, const CallBackFunc &callback)
    : BaseSystemProcCollector(limit, callback), BaseMetricsCollector(metrics_type::CPU, collector_type::SYSTEM, nullptr)
{
}

std::string SystemProcCPUCollector::GenFilter() const
{
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

litebus::Future<Metric> SystemProcCPUCollector::GetUsage() const
{
    auto metricses = getInstanceMetricsesCallBack_();
    double usage = 0;
    for (auto &futureMetrics : metricses) {
        auto metrics = futureMetrics.Get();
        if (metrics.instanceID.IsNone()) {
            continue;
        }

        if (!(metrics.metricsType == metrics_type::CPU)) {
            continue;
        }

        if (metrics.usage.IsSome()) {
            usage += metrics.usage.Get();
        }
    }
    return { Metric{ { usage }, {}, {}, {} } };
}

Metric SystemProcCPUCollector::GetLimit() const
{
    Metric metric;
    metric.value = limit_;
    return metric;
}

}  // namespace functionsystem::runtime_manager
