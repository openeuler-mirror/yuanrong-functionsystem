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

#ifndef FUNCTIONSYSTEM_FAIRNESS_POLICY_H
#define FUNCTIONSYSTEM_FAIRNESS_POLICY_H

#include "common/schedule_decision/scheduler/priority_policy/priority_policy.h"

namespace functionsystem::schedule_decision {

class FairnessPolicy : public PriorityPolicy {
public:
    explicit FairnessPolicy();

    ~FairnessPolicy() override = default;

    PriorityPolicyType GetPriorityPolicyType() override;

    bool CanSchedule(const std::shared_ptr<QueueItem> &item) override;

    void PrepareForScheduling(const std::shared_ptr<QueueItem> &item) override;

    void StorePendingInfo(const std::shared_ptr<QueueItem> &item) override;

    void ClearPendingInfos() override;

private:

    void StorePendingAffinity(const std::shared_ptr<InstanceItem> &instance);

    void StorePendingAffinity(const std::shared_ptr<GroupItem> &group);

    void AddPendingAffinityToInstance(const std::shared_ptr<InstanceItem> &instance);

    void AddPendingAffinityToGroup(const std::shared_ptr<GroupItem> &group);

    bool HasSimilarPendingRequest(const std::shared_ptr<QueueItem> &item);

    bool HasSimilarResourceDemand(const std::shared_ptr<InstanceItem> &instance);

    bool ExistNonAffinityPendingInstances(int priority);

private:
    // key: priority, value: <pending affinity, count>
    std::unordered_map<int, std::unordered_map<std::string, int>> pendingReqAffinityCountMap_;
    // key: priority, value: pending affinity
    std::unordered_map<int, InnerSystemAffinity::PendingAffinity> pendingReqAffinityMap_;
};
}

#endif // FUNCTIONSYSTEM_FAIRNESS_POLICY_H
