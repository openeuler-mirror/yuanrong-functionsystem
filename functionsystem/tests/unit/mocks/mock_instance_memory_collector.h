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

#ifndef MOCK_INSTANCE_MEMORY_COLLECTOR_H
#define MOCK_INSTANCE_MEMORY_COLLECTOR_H

#include <gmock/gmock.h>

#include "metrics/collector/instance_memory_collector.h"

namespace functionsystem::test {
using namespace functionsystem::runtime_manager;

class MockInstanceMemoryCollector : public InstanceMemoryCollector {
public:
    MockInstanceMemoryCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                                const std::string &deployDir)
        : InstanceMemoryCollector(pid, instanceID, limit, deployDir) {}

    MockInstanceMemoryCollector(const pid_t &pid, const std::string &instanceID, const double &limit,
                                const std::string &deployDir, const std::shared_ptr<ProcFSTools> procFSTools)
        : InstanceMemoryCollector(pid, instanceID, limit, deployDir, procFSTools) {}

    ~MockInstanceMemoryCollector() override = default;

    MOCK_METHOD(litebus::Future<Metric>, GetUsage, (), (const override));
};

}
#endif  // MOCK_INSTANCE_MEMORY_COLLECTOR_H