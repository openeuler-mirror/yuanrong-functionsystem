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

#ifndef COMMON_LEADER_LEADER_ACTOR_H
#define COMMON_LEADER_LEADER_ACTOR_H

#include <actor/actor.hpp>

#include "common/explorer/explorer.h"

namespace functionsystem::leader {

class LeaderActor : public litebus::ActorBase {
public:
    LeaderActor(const std::string &name, const std::set<std::string> &electionKeySet,
                const explorer::ElectionInfo &electionInfo)
        : litebus::ActorBase(name),
          electionKeySet_(electionKeySet),
          proposal_(electionInfo.identity),
          leaseTTL_(electionInfo.electLeaseTTL),
          keepAliveInterval_(electionInfo.electKeepAliveInterval),
          electRenewInterval_(electionInfo.electRenewInterval)
    {
        std::ostringstream oss;
        for (const auto& element : electionKeySet) {
            oss << element << " ";
        }
        electionKeyStr_ = oss.str();
    }

    virtual ~LeaderActor() override = default;

    virtual void Elect() = 0;

    // register callback when campaign success
    void RegisterCallbackWhenBecomeLeader(const std::function<void()> &callback)
    {
        callbackWhenBecomeLeader_ = callback;
    }

    // register callback when resign
    void RegisterCallbackWhenResign(const std::function<void()> &callback)
    {
        callbackWhenResign_ = callback;
    }

    void RegisterPublishLeaderCallBack(const std::function<void(const explorer::LeaderInfo &)> &callback)
    {
        publishLeaderCallBack_ = callback;
    }

protected:
    void Init() override
    {
    }

    void Finalize() override
    {
    }

    std::function<void()> callbackWhenBecomeLeader_{ nullptr };
    std::function<void()> callbackWhenResign_{ nullptr };
    std::function<void(const explorer::LeaderInfo &)> publishLeaderCallBack_;
    std::shared_ptr<litebus::Promise<bool>> isCampaigning_;
    // concat all election keys as string
    std::string electionKeyStr_;
    std::set<std::string> electionKeySet_;
    std::string proposal_;  // leader's info, Actually it's address(ip + port) of leader

    uint32_t leaseTTL_{ DEFAULT_ELECT_LEASE_TTL };  // lease ttl
    uint32_t keepAliveInterval_{ DEFAULT_ELECT_KEEP_ALIVE_INTERVAL };

    uint32_t electRenewInterval_{ DEFAULT_ELECT_LEASE_TTL / 3 };

    // cached last leader proposal
    explorer::LeaderInfo cachedLeaderInfo_{ "", "" };
};
}  // namespace functionsystem::leader

#endif  // COMMON_LEADER_LEADER_ACTOR_H
