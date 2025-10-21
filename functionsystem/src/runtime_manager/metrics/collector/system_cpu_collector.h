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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_CPU_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_CPU_COLLECTOR_H

#include "base_metrics_collector.h"

namespace functionsystem::runtime_manager {

namespace system_metrics {
const std::string CPU_CFS_PERIOD_PATH = "/sys/fs/cgroup/cpu/cpu.cfs_period_us";
const std::string CPU_CFS_QUOTA_PATH  = "/sys/fs/cgroup/cpu/cpu.cfs_quota_us";
const std::string CPU_USAGE_PATH = "/sys/fs/cgroup/cpu/cpuacct.usage";
const uint64_t CPU_CAL_INTERVAL = 100;
const uint32_t CPU_SCALE = 1000;
}

class SystemCPUCollector : public BaseMetricsCollector {
public:
    SystemCPUCollector();
    explicit SystemCPUCollector(const std::shared_ptr<ProcFSTools> procFSTools);
    ~SystemCPUCollector() override = default;
    litebus::Future<Metric> GetUsage() const override;
    Metric GetLimit() const override;
    std::string GenFilter() const override;

private:
    static litebus::Option<double> CalCPUUsage(const std::shared_ptr<ProcFSTools> procFSTools);
};

}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_CPU_COLLECTOR_H