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

#ifndef INSTANCE_XPU_COLLECTOR_H
#define INSTANCE_XPU_COLLECTOR_H

#include "base_instance_collector.h"
#include "common/constants/constants.h"
#include "runtime_manager/utils/utils.h"
#include "heterogeneous_collector/gpu_probe.h"
#include "heterogeneous_collector/npu_probe.h"
namespace functionsystem::runtime_manager {

struct InstInfoWithXPU {
    pid_t pid;
    std::string instanceID;
    std::string deployDir;
    ::messages::RuntimeInstanceInfo info;
    std::string nodeID;
};

class InstanceXPUCollector : public BaseInstanceCollector, public BaseMetricsCollector {
public:

    InstanceXPUCollector(const InstInfoWithXPU &basicInstInfo, const MetricsType &type,
                                       const std::shared_ptr<ProcFSTools>& procFSTools,
                                       const std::shared_ptr<XPUCollectorParams>& params);
    ~InstanceXPUCollector() override = default;

    std::string GenFilter() const override;
    litebus::Future<Metric> GetUsage() const override;
    Metric GetLimit() const override;

private:
    [[nodiscard]] DevClusterMetrics GetDevClusterMetrics(const std::string &initType) const;
    void ResolveLogicCardIDs(const std::string &env);
    std::string realIDs_;
    std::vector<int> logicIDs_;
    std::shared_ptr<TopoProbe> probe_;
    std::string uuid_;
};
}
#endif // INSTANCE_XPU_COLLECTOR_H
