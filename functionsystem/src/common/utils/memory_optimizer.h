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

#ifndef COMMON_UTILS_MEMORY_OPTIMIZER_H
#define COMMON_UTILS_MEMORY_OPTIMIZER_H

#include <malloc.h>

#include "async/async.hpp"
#include "async/future.hpp"
#include "async/asyncafter.hpp"
#include "common/status/status.h"
#include "litebus.hpp"

namespace functionsystem {
/*
 * Usage:
 *   Call following code in main() to reduce memory usage, but it will affect performance,
 *   so need further test
 *
 *   ````
 *   auto memOpt = MemoryOptimizer();
 *   memOpt.StartTrimming();
 *   ```
 */

// Note: typical num of threads for each component,
//   * function_master: ~40
//   * function_proxy: ~80
//   * function_agent: ~33
const int DEFAULT_MAX_ARENA_NUM = 20;

// For performance consideration, tens of seconds would be good choices
const int DEFAULT_MEMORY_TRIM_INTERVAL_MS = 10 * 1000;

class MemoryTrimmerActor : public litebus::ActorBase {
public:
    explicit MemoryTrimmerActor()
        : litebus::ActorBase("MemoryTrimmer-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString())
    {
    }
    ~MemoryTrimmerActor() override = default;

    void TrimPeriodically()
    {
        (void)malloc_trim(0);
        (void)litebus::AsyncAfter(DEFAULT_MEMORY_TRIM_INTERVAL_MS, GetAID(), &MemoryTrimmerActor::TrimPeriodically);
    }
};

class MemoryOptimizer {
public:
    MemoryOptimizer()
    {
        // start trim actor
        trimmerActor_ = std::make_shared<MemoryTrimmerActor>();
        (void)litebus::Spawn(trimmerActor_);
    }

    virtual ~MemoryOptimizer()
    {
        litebus::Terminate(trimmerActor_->GetAID());
        litebus::Await(trimmerActor_->GetAID());
    }

    void StartTrimming()
    {
        YRLOG_INFO("Start memory optimizing, M_ARENA_MAX: {}, Periodically Trim Interval: {}",
                   DEFAULT_MAX_ARENA_NUM, DEFAULT_MEMORY_TRIM_INTERVAL_MS);
        // set malloc options
        (void)mallopt(M_ARENA_MAX, DEFAULT_MAX_ARENA_NUM);
        // start trim loop
        litebus::Async(trimmerActor_->GetAID(), &MemoryTrimmerActor::TrimPeriodically);
    }
private:
    std::shared_ptr<MemoryTrimmerActor> trimmerActor_;
};
}

#endif  // COMMON_UTILS_MEMORY_OPTIMIZER_H
