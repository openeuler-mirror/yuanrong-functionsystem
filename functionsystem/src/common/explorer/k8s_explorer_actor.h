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


#ifndef COMMON_EXPLORER_K8S_EXPLORER_ACTOR_H
#define COMMON_EXPLORER_K8S_EXPLORER_ACTOR_H

#include "actor/actor.hpp"
#include "common/explorer/explorer_actor.h"
#include "common/resource_lock/lease_lock.h"

namespace functionsystem::explorer {

class K8sExplorerActor : public ExplorerActor {
public:
    // when leaseTTL expires, the leadership will be ressigned automatically
    K8sExplorerActor(const std::string &electionKey, const ElectionInfo &electionInfo,
                     const litebus::Option<LeaderInfo> &leaderInfo, const std::shared_ptr<KubeClient> kubeClient,
                     const std::string &k8sNamespace)
        : ExplorerActor("K8sExplorerActor-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), {electionKey},
                        electionInfo, leaderInfo)
    {
        resourceLock_ =
            std::make_shared<resource_lock::LeaseLock>(electionInfo.identity, kubeClient, k8sNamespace, electionKey);
    }

    ~K8sExplorerActor() override = default;

    void Observe() override;
    void FastPublish(const LeaderInfo &leaderInfo) override;
protected:
    void Finalize() override;

private:
    void OnObserveEvent(const litebus::Future<std::shared_ptr<V1Lease>> &lease);

    std::shared_ptr<resource_lock::LeaseLock> resourceLock_{nullptr};
    litebus::Timer observerTimer_;
};
}  // namespace functionsystem::explorer

#endif  // COMMON_EXPLORER_K8S_EXPLORER_ACTOR_H
