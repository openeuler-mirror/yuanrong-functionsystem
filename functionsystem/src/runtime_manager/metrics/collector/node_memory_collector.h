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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_NODE_MEMORY_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_NODE_MEMORY_COLLECTOR_H

#include "base_metrics_collector.h"

namespace functionsystem::runtime_manager {

class NodeMemoryCollector : public BaseMetricsCollector {
public:
    NodeMemoryCollector();
    explicit NodeMemoryCollector(const std::shared_ptr<ProcFSTools> procFSTools, double overheadMemory = 0.0);
    ~NodeMemoryCollector() override = default;
    litebus::Future<Metric> GetUsage() const override;
    Metric GetLimit() const override;
    std::string GenFilter() const override;

private:
    void parseMemInfo() const;
    double overheadMemory_;
    mutable double totalMem_{0.0};
    mutable double availableMem_{0.0};
};

}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_NODE_MEMORY_COLLECTOR_H