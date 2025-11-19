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

#ifndef COMMON_RESOURCE_VIEW_RESOURCE_POLLER_H
#define COMMON_RESOURCE_VIEW_RESOURCE_POLLER_H

#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>

#include "actor/aid.hpp"
#include "async/future.hpp"

namespace functionsystem::resource_view {

const uint64_t MAX_CONCURRENCY_PULL = 100;

class ResourceViewActor;

class ResourcePoller {
public:
    ResourcePoller(const std::function<void(const std::string &)> &sendPullResource,
                   const std::function<void(const std::string &)> &delegateReset,
                   const std::function<void(uint64_t)> &deferPull, uint64_t maxConcurrencyPull);
    ResourcePoller(const std::function<void(const std::string &)> &sendPullResource,
                   const std::function<void(const std::string &)> &delegateReset,
                   const std::function<void(uint64_t)> &deferPull)
        : ResourcePoller(sendPullResource, delegateReset, deferPull, 0)
    {
    }

    ~ResourcePoller()
    {
        sendPullResource_ = nullptr;
        delegateReset_ = nullptr;
        deferTriggerPull_ = nullptr;
    };

    void Stop();
    void Add(const std::string &id);
    void Del(const std::string &id);
    void Reset(const std::string &id);
    void TryPullResource();

    static void SetInterval(uint64_t pullResourceCycle)
    {
        pullResourceCycle_ = pullResourceCycle;
    }

private:
    inline static uint64_t pullResourceCycle_ = 1000;
    struct ResourcePollInfo {
        std::string id;
        int64_t latestPulledTime;
        ResourcePollInfo(const std::string &id, int64_t nextPullTime) : id(id), latestPulledTime(nextPullTime)
        {
        }
    };
    std::function<void(const std::string &)> sendPullResource_;
    std::function<void(const std::string &)> delegateReset_;
    std::function<void(uint64_t)> deferTriggerPull_;
    std::unordered_map<std::string, std::shared_ptr<ResourcePollInfo>> underlayers_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<bool>>> pulling_;
    std::queue<std::shared_ptr<ResourcePollInfo>> toPoll_;
    uint32_t maxConcurrencyPull_{ MAX_CONCURRENCY_PULL };
};
}  // namespace functionsystem::resource_view
#endif  // COMMON_RESOURCE_VIEW_RESOURCE_POLLER_H
