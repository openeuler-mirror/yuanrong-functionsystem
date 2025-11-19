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

#ifndef COMMON_LEADER_K8S_LEADER_ACTOR_H
#define COMMON_LEADER_K8S_LEADER_ACTOR_H

#include <actor/actor.hpp>

#include "common/resource_lock/lease_lock.h"
#include "leader_actor.h"

namespace functionsystem::leader {

class K8sLeaderActor : public LeaderActor {
public:
    K8sLeaderActor(const std::string &electionKey, const explorer::ElectionInfo &electionInfo,
                   const std::shared_ptr<KubeClient> kubeClient, const std::string &k8sNamespace)
        : LeaderActor("K8sLeaderActor-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), {electionKey},
                      electionInfo)
    {
        resourceLock_ = std::make_shared<resource_lock::LeaseLock>(proposal_, kubeClient, k8sNamespace, electionKey);
    }
    ~K8sLeaderActor() override = default;

    void Elect() override;

    // for test
    [[maybe_unused]] std::shared_ptr<resource_lock::LeaderElectionRecord> GetObservedRecord()
    {
        return observedRecord_;
    }

protected:
    void Init() override;
    void Finalize() override;
private:
    litebus::Future<Status> TryAcquireOrNew();
    void Release();
    void DoTryAcquireOrNew(const std::shared_ptr<resource_lock::LeaderElectionRecord> &record,
                           const std::shared_ptr<litebus::Promise<Status>> &promise);
    void CheckNeedCreateLock(const litebus::Future<std::shared_ptr<V1Lease>> future,
                             const std::shared_ptr<resource_lock::LeaderElectionRecord> &record,
                             const std::shared_ptr<litebus::Promise<Status>> &promise);
    void CheckElectResult(const litebus::Future<Status> &result);
    void SetObservedRecord(const std::shared_ptr<V1Lease> &lease,
                           const std::shared_ptr<litebus::Promise<Status>> &promise);
    bool IsLeader();
    bool IsLeaderValid();
    std::string GetLeader();

    std::shared_ptr<resource_lock::LeaseLock> resourceLock_{nullptr};
    int64_t observedTime_{ 0 };
    std::shared_ptr<resource_lock::LeaderElectionRecord> observedRecord_{nullptr};
    litebus::Timer timer_;
};

}
#endif  // COMMON_LEADER_K8S_LEADER_ACTOR_H
