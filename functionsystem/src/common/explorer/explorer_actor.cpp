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

#include "common/explorer/explorer_actor.h"

#include <utility>

#include "async/async.hpp"
#include "logs/logging.h"
#include "status/status.h"

namespace functionsystem::explorer {

ExplorerActor::ExplorerActor(const std::string &name, std::string electionKey, const ElectionInfo &electionInfo,
                             const litebus::Option<LeaderInfo> &leaderInfo)
    : litebus::ActorBase(name),
      electionKey_(std::move(electionKey)),
      mode_(electionInfo.mode),
      electKeepAliveInterval_(electionInfo.electKeepAliveInterval)
{
    if (mode_ == ETCD_ELECTION_MODE || mode_ == TXN_ELECTION_MODE || mode_ == K8S_ELECTION_MODE) {
        YRLOG_INFO("{} | create explorer, use {} mode", electionKey_, mode_);
    } else {
        if (leaderInfo.IsNone()) {
            YRLOG_ERROR("{} | LeaderInfo is required in standalone, but is none", electionKey_);
            return;
        }
        YRLOG_INFO("{} | create explorer use standalone mode", electionKey_);
        cachedLeaderInfo_ = leaderInfo.Get();
    }
}

void ExplorerActor::Init()
{
    YRLOG_INFO("{} | init explorer, use {} mode", electionKey_, mode_);
    if (mode_ == ETCD_ELECTION_MODE || mode_ == TXN_ELECTION_MODE || mode_ == K8S_ELECTION_MODE) {
        litebus::Async(GetAID(), &ExplorerActor::Observe);
    }
}

void ExplorerActor::Finalize()
{
    YRLOG_INFO("{} | clear explorer_actor", electionKey_);
    callbacks_.clear();
}

void ExplorerActor::RegisterLeaderChangedCallback(const std::string &cbIdentifier,
                                                  const CallbackFuncLeaderChange &cbFunc)
{
    RETURN_IF_NULL(cbFunc);
    callbacks_[cbIdentifier] = cbFunc;
    if (cachedLeaderInfo_.address.empty()) {
        YRLOG_INFO("{} | register leader changed callback({})", electionKey_, cbIdentifier);
    } else {
        YRLOG_INFO("{} | register and trigger leader changed callback({})", electionKey_, cbIdentifier);
        cbFunc(cachedLeaderInfo_);
    }
}

void ExplorerActor::UnregisterLeaderChangedCallback(const std::string &cbIdentifier)
{
    YRLOG_INFO("{} | unregister leader changed callback({})", electionKey_, cbIdentifier);
    (void)callbacks_.erase(cbIdentifier);
}
}  // namespace functionsystem::explorer