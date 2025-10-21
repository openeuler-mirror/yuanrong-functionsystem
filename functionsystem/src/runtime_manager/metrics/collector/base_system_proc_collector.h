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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_BASE_SYSTEM_PROC_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_BASE_SYSTEM_PROC_COLLECTOR_H

#include "base_metrics_collector.h"

namespace functionsystem::runtime_manager {

using CallBackFunc = std::function<std::vector<litebus::Future<Metrics>>()>;

class BaseSystemProcCollector {
public:
    BaseSystemProcCollector(const double &limit, const CallBackFunc &callback)
        : limit_(limit), getInstanceMetricsesCallBack_(callback)
    {}

protected:
    double limit_ = 0.0;
    CallBackFunc getInstanceMetricsesCallBack_;
};
}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_BASE_SYSTEM_PROC_COLLECTOR_H
