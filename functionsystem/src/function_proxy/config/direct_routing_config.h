/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#pragma once

#include <atomic>

namespace functionsystem::function_proxy {

// Feature flag: enable direct routing via LRU cache and single-writer persistence.
// When false (default), the original etcd watch-based broadcast path is used.
// When true, the new direct routing path is activated.
class DirectRoutingConfig {
public:
    DirectRoutingConfig() = default;
    ~DirectRoutingConfig() = default;

    class ScopedEnableGuard {
    public:
        explicit ScopedEnableGuard(bool previous) : previous_(previous) {}
        ~ScopedEnableGuard()
        {
            DirectRoutingConfig::SetEnabled(previous_);
        }

        ScopedEnableGuard(const ScopedEnableGuard &) = delete;
        ScopedEnableGuard &operator=(const ScopedEnableGuard &) = delete;

    private:
        bool previous_;
    };

    static bool IsEnabled()
    {
        return enabled_.load(std::memory_order_relaxed);
    }

    static void SetEnabled(bool enabled)
    {
        enabled_.store(enabled, std::memory_order_relaxed);
    }

    static ScopedEnableGuard EnableForTest()
    {
        bool previous = IsEnabled();
        SetEnabled(true);
        return ScopedEnableGuard(previous);
    }

private:
    inline static std::atomic<bool> enabled_{ false };
};

}  // namespace functionsystem::function_proxy
