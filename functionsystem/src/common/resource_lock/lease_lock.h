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

#ifndef FUNCTIONSYSTEM_RESOURCE_LOCK_LEASE_LOCK_H
#define FUNCTIONSYSTEM_RESOURCE_LOCK_LEASE_LOCK_H

#include "common/kube_client/kube_client.h"

namespace functionsystem::resource_lock {

using V1ObjectMeta = functionsystem::kube_client::model::V1ObjectMeta;
using V1LeaseSpec = functionsystem::kube_client::model::V1LeaseSpec;

struct LeaderElectionRecord {
    std::string holderIdentity;
    int32_t leaseDurationSeconds;
    int32_t leaseTransitions;
    std::chrono::time_point<std::chrono::system_clock> acquireTime;
    std::chrono::time_point<std::chrono::system_clock> renewTime;
};

class LeaseLock {
public:
    LeaseLock() = default;
    LeaseLock(const std::string &identity, const std::shared_ptr<KubeClient> &kubeClient,
              const std::string &k8sNamespace, const std::string &leaseName)
        : identity_(identity), kubeClient_(kubeClient), k8sNamespace_(k8sNamespace), leaseName_(leaseName)
    {
    }

    ~LeaseLock() = default;

    litebus::Future<std::shared_ptr<V1Lease>> Get();
    litebus::Future<std::shared_ptr<V1Lease>> Create(const std::shared_ptr<LeaderElectionRecord> &record);
    litebus::Future<std::shared_ptr<V1Lease>> Update(const std::shared_ptr<LeaderElectionRecord> &record);
    std::shared_ptr<V1LeaseSpec> LeaderElectionToLeaseSpec(const std::shared_ptr<LeaderElectionRecord> &record);
    std::shared_ptr<LeaderElectionRecord> LeaseSpecToLeaderElection(const std::shared_ptr<V1LeaseSpec> &spec);
    void SetLease(const std::shared_ptr<V1Lease> &lease);
    bool IsHold(const std::shared_ptr<LeaderElectionRecord> &record);
    std::string Identity()
    {
        return identity_;
    }

private:
    std::string identity_;
    std::shared_ptr<KubeClient> kubeClient_{ nullptr };
    std::string k8sNamespace_;
    std::string leaseName_;
    std::shared_ptr<V1Lease> lease_{nullptr};
};
}

#endif  // FUNCTIONSYSTEM_RESOURCE_LOCK_LEASE_LOCK_H
