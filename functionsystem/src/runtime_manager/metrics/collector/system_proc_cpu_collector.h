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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_PROC_CPU_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_PROC_CPU_COLLECTOR_H

#include "base_system_proc_collector.h"

namespace functionsystem::runtime_manager {

class SystemProcCPUCollector : public BaseSystemProcCollector, public BaseMetricsCollector {
public:
    SystemProcCPUCollector(const double &limit, const CallBackFunc &callback);
    ~SystemProcCPUCollector() override = default;
    std::string GenFilter() const override;
    litebus::Future<Metric> GetUsage() const override;
    Metric GetLimit() const override;
};

}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_SYSTEM_PROC_CPU_COLLECTOR_H
