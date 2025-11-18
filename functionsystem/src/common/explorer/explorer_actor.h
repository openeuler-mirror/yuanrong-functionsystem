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

#ifndef COMMON_EXPLORER_ACTOR_H
#define COMMON_EXPLORER_ACTOR_H

#include <set>
#include <async/async.hpp>
#include "actor/actor.hpp"
#include "async/option.hpp"

#include "common/constants/constants.h"

namespace functionsystem::explorer {
const std::string DEFAULT_MASTER_ELECTION_KEY = "/yr/leader/function-master";
const std::string FUNCTION_MASTER_K8S_LEASE_NAME = "function-master";

const std::string IAM_SERVER_MASTER_ELECTION_KEY = "/yr/leader/function-iam";
const std::string IAM_SERVER_K8S_LEASE_NAME = "function-iam";

// LeaderInfo
struct LeaderInfo {
    std::string name;
    std::string address;
    int64_t electRevision{ 0 };
};

struct ElectionInfo {
    std::string identity;
    std::string mode;
    uint32_t electKeepAliveInterval{ DEFAULT_ELECT_KEEP_ALIVE_INTERVAL };
    uint32_t electLeaseTTL{ DEFAULT_ELECT_LEASE_TTL };
    uint32_t electRenewInterval{ DEFAULT_ELECT_LEASE_TTL };
};

// Explorer callback function type, function signature should be like
//   void onUpdate(LeaderInfo& li) {}
using CallbackFuncLeaderChange = std::function<void(const LeaderInfo &)>;

/**
 * ExplorerActor will observe current leader, it won't send `leader` request to
 * metastore to query current leader proactively, it depends on the `observe`
 * interface to keep the cache up-to-date.
 **/
class ExplorerActor : public litebus::ActorBase {
public:
    // when leaseTTL expires, the leadership will be ressigned automatically
    ExplorerActor(const std::string name, const std::set<std::string> &electionKeySet,
                  const ElectionInfo &electionInfo, const litebus::Option<LeaderInfo> &leaderInfo);

    ~ExplorerActor() override = default;

    // register callback, may receive register multiple times since there might
    // be multiple components using explorer to detect current leader
    void RegisterLeaderChangedCallback(const std::string &cbIdentifier, const CallbackFuncLeaderChange &cbFunc);

    void UnregisterLeaderChangedCallback(const std::string &cbIdentifier);

    virtual void Observe() = 0;

    virtual void FastPublish(const LeaderInfo &leaderInfo) = 0;
protected:
    void Init() override;
    void Finalize() override;

    // cached current leader info, may not exist
    LeaderInfo cachedLeaderInfo_;
    std::set<std::string> electionKeySet_;
    std::string mode_;
    uint32_t electKeepAliveInterval_;
    int64_t electRevision_{ 0 };
    std::unordered_map<std::string, CallbackFuncLeaderChange> callbacks_;
    // concat all election keys as string
    std::string electionKeyStr_;
    // indicate who I am
    std::string identity_;
};
}  // namespace functionsystem::explorer

#endif  // COMMON_EXPLORER_ACTOR_H
