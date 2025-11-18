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


#include <async/async.hpp>
#include <async/asyncafter.hpp>
#include <async/defer.hpp>

#include "common/explorer/k8s_explorer_actor.h"

namespace functionsystem::explorer {

void K8sExplorerActor::Observe()
{
    resourceLock_->Get().OnComplete(litebus::Defer(GetAID(), &K8sExplorerActor::OnObserveEvent, std::placeholders::_1));
}

void K8sExplorerActor::Finalize()
{
    YRLOG_INFO("clear explorer_actor");
    litebus::TimerTools::Cancel(observerTimer_);

    ExplorerActor::Finalize();
}

void K8sExplorerActor::OnObserveEvent(const litebus::Future<std::shared_ptr<V1Lease>> &lease)
{
    if (lease.IsError() || lease.Get() == nullptr) {
        YRLOG_WARN("failed to get lease lock, err is {}", lease.GetErrorCode());
    } else {
        auto result = resourceLock_->LeaseSpecToLeaderElection(lease.Get()->GetSpec());
        if (cachedLeaderInfo_.address != result->holderIdentity) {
            // leader change
            YRLOG_INFO("leader address changed, old({}), new({})", cachedLeaderInfo_.address, result->holderIdentity);
            // 1. update cache
            cachedLeaderInfo_.address = result->holderIdentity;
            // 2. trigger callbacks
            for (auto &cb : callbacks_) {
                YRLOG_DEBUG("K8sExplorerActor({}) triggers callback({})", electionKeyStr_, cb.first);
                cb.second(cachedLeaderInfo_);
            }
        }
    }
    observerTimer_ =
        litebus::AsyncAfter(electKeepAliveInterval_ * litebus::SECTOMILLI, GetAID(), &K8sExplorerActor::Observe);
}

void K8sExplorerActor::FastPublish(const LeaderInfo &leaderInfo)
{
}
}  // namespace functionsystem::explorer