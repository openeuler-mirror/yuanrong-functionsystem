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
#include "resource_poller.h"

#include "async/asyncafter.hpp"
#include "common/logs/logging.h"
#include "timer/timewatch.hpp"

namespace functionsystem::resource_view {

const uint32_t AFTER_TIMES = 3;

ResourcePoller::ResourcePoller(const std::function<void(const std::string &)> &sendPullResource,
                               const std::function<void(const std::string &)> &delegateReset,
                               const std::function<void(uint64_t)> &deferPull, uint64_t maxConcurrencyPull = 0)
    : sendPullResource_(sendPullResource),
      delegateReset_(delegateReset),
      deferTriggerPull_(deferPull),
      pulling_(0),
      maxConcurrencyPull_(maxConcurrencyPull > 0 ? maxConcurrencyPull : MAX_CONCURRENCY_PULL)
{
}

void ResourcePoller::Stop()
{
    for (auto &it : pulling_) {
        it.second->SetValue(false);
    }
    pulling_.clear();
}

void ResourcePoller::Add(const std::string &id)
{
    if (underlayers_.find(id) != underlayers_.end()) {
        return;
    }
    auto pollInfo = std::make_shared<ResourcePollInfo>(id, litebus::TimeWatch::Now());
    underlayers_[id] = pollInfo;
    toPoll_.push(pollInfo);
}

void ResourcePoller::Del(const std::string &id)
{
    auto iter = underlayers_.find(id);
    if (iter == underlayers_.end()) {
        return;
    }
    (void)underlayers_.erase(iter);
    (void)pulling_.erase(id);
}

void ResourcePoller::Reset(const std::string &id)
{
    auto iter = underlayers_.find(id);
    if (iter == underlayers_.end()) {
        return;
    }
    auto info = iter->second;
    info->latestPulledTime = static_cast<int64_t>(litebus::TimeWatch::Now());
    if (pulling_.find(id) != pulling_.end()) {
        auto promise = pulling_[id];
        (void)pulling_.erase(id);
        promise->SetValue(true);
        toPoll_.push(info);
    }
}

void ResourcePoller::TryPullResource()
{
    const auto currentTime = static_cast<unsigned long>(litebus::TimeWatch::Now());
    std::vector<std::shared_ptr<ResourcePollInfo>> notReachTime;
    while (pulling_.size() < maxConcurrencyPull_ && !toPoll_.empty()) {
        auto pull = toPoll_.front();
        if (underlayers_.find(pull->id) == underlayers_.end()) {
            toPoll_.pop();
            continue;
        }
        auto timeDuration = llabs(static_cast<long long>(currentTime - pull->latestPulledTime));
        // while not reach time interval, pollInfo should be moved to another queue
        if (static_cast<uint64_t>(timeDuration) < pullResourceCycle_) {
            notReachTime.push_back(pull);
            toPoll_.pop();
            continue;
        }
        const auto promise = std::make_shared<litebus::Promise<bool>>();
        pulling_[pull->id] = promise;
        sendPullResource_(pull->id);
        (void)promise->GetFuture().After(
            pullResourceCycle_ * AFTER_TIMES,
            [delegateReset(delegateReset_), id(pull->id)](const litebus::Future<bool> &future) {
                YRLOG_WARN("pull {} timeout, reset to pull", id);
                delegateReset(id);
                return future;
            });
        toPoll_.pop();
    }
    for (auto futurePull : notReachTime) {
        toPoll_.push(futurePull);
    }
    notReachTime.clear();
    deferTriggerPull_(pullResourceCycle_);
}

}  // namespace functionsystem::resource_view