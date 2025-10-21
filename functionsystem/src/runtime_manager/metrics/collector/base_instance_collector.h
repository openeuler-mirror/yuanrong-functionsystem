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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_BASE_INSTANCE_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_BASE_INSTANCE_COLLECTOR_H

#include "base_metrics_collector.h"
#include "common/utils/proc_fs_tools.h"

namespace functionsystem::runtime_manager {

class BaseInstanceCollector {
public:
    BaseInstanceCollector(const pid_t pid, const std::string &instanceID,
                          const double limit, const std::string &deployDir)
        : pid_(pid), instanceID_(instanceID), limit_(limit), deployDir_(deployDir)
    {}

protected:
    pid_t pid_;
    std::string instanceID_;
    double limit_ = 0.0;
    std::string deployDir_;
};

}

#endif // RUNTIME_MANAGER_METRICS_COLLECTOR_BASE_INSTANCE_COLLECTOR_H
