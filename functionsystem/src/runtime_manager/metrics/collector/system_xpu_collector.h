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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_NPU_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_NPU_COLLECTOR_H

#include "base_metrics_collector.h"
#include "heterogeneous_collector/topo_probe.h"
#include "common/utils/proc_fs_tools.h"

namespace functionsystem::runtime_manager {
namespace system_metrics {
const uint64_t NPU_CAL_INTERVAL = 100;
}

class SystemXPUCollector : public BaseMetricsCollector {
public:
    SystemXPUCollector(std::string &nodeID, const MetricsType &type, const std::shared_ptr<ProcFSTools> &procFSTools,
                       const std::shared_ptr<XPUCollectorParams> &params);
    ~SystemXPUCollector() override = default;

    [[nodiscard]] litebus::Future<Metric> GetUsage() const override;
    [[nodiscard]] Metric GetLimit() const override;

    [[nodiscard]] std::string GenFilter() const override;

private:
    [[nodiscard]] DevClusterMetrics GetDevClusterMetrics(const std::string &initType) const;
    std::shared_ptr<TopoProbe> probe_;
    std::string partitionKey_ = "partition";
    std::string uuid_;
};
}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_NPU_COLLECTOR_H
