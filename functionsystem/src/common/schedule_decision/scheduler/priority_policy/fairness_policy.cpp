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

#include "fairness_policy.h"
#include "common/scheduler_framework/utils/label_affinity_utils.h"

namespace functionsystem::schedule_decision {

FairnessPolicy::FairnessPolicy() {}

// Use 'empty' to indicate that there are no resource affinity options for the instance.
static const std::string EMPTY_PENDING_AFFINITY_STRING = "empty";

PriorityPolicyType FairnessPolicy::GetPriorityPolicyType()
{
    return PriorityPolicyType::FAIRNESS;
}

bool FairnessPolicy::ExistNonAffinityPendingInstances(int priority)
{
    for (const auto &pendingReqAffinityCount : pendingReqAffinityCountMap_) {
        if (pendingReqAffinityCount.first < priority) {
            continue;
        }
        if (pendingReqAffinityCount.second.find(EMPTY_PENDING_AFFINITY_STRING)
            != pendingReqAffinityCount.second.end() &&
            pendingReqAffinityCount.second.at(EMPTY_PENDING_AFFINITY_STRING) > 0) {
            return true;
        }
    }
    return false;
}

void FairnessPolicy::StorePendingAffinity(const std::shared_ptr<InstanceItem> &instance)
{
    uint16_t priority = instance->GetPriority();
    const auto &affinity = instance->scheduleReq->instance().scheduleoption().affinity();
    if (!affinity.has_resource() ||
        (!affinity.resource().has_requiredaffinity() && !affinity.resource().has_requiredantiaffinity())) {
        pendingReqAffinityCountMap_[priority][EMPTY_PENDING_AFFINITY_STRING]++;
        return;
    }

    auto resourceAffinity = instance->scheduleReq->instance().scheduleoption().affinity().resource();
    resourceAffinity.clear_preferredaffinity();
    resourceAffinity.clear_preferredantiaffinity();
    auto resourceAffinityString = resourceAffinity.SerializeAsString();
    if (pendingReqAffinityCountMap_[priority].find(resourceAffinityString) ==
        pendingReqAffinityCountMap_[priority].end()) {
        pendingReqAffinityMap_[priority].mutable_resources()->Add()->CopyFrom(resourceAffinity);
    }
    pendingReqAffinityCountMap_[priority][resourceAffinityString]++;
}

void FairnessPolicy::StorePendingAffinity(const std::shared_ptr<GroupItem> &group)
{
    if (group->groupReqs.empty()) {
        YRLOG_WARN("{}|schedule requests are empty", group->GetRequestId());
        return;
    }

    auto rangeOpt = group->GetRangeOpt();
    if (rangeOpt.isRange) {
        StorePendingAffinity(group->groupReqs[0]);
        return;
    }

    for (auto &instanceItem : group->groupReqs) {
        StorePendingAffinity(instanceItem);
    }
}

void FairnessPolicy::AddPendingAffinityToInstance(const std::shared_ptr<InstanceItem> &instance)
{
    instance->scheduleReq->mutable_instance()->mutable_scheduleoption()
            ->mutable_affinity()->mutable_inner()->clear_pending();

    uint16_t priority = instance->GetPriority();

    auto instancePendingAffinity = instance->scheduleReq->mutable_instance()->mutable_scheduleoption()
        ->mutable_affinity()->mutable_inner()->mutable_pending();
    for (const auto &pendingReqAffinity : pendingReqAffinityMap_) {
        if (pendingReqAffinity.first < priority) {
            continue;
        }
        if (!pendingReqAffinity.second.resources().empty()) {
            instancePendingAffinity->mutable_resources()->MergeFrom(pendingReqAffinity.second.resources());
        }
    }
}

void FairnessPolicy::AddPendingAffinityToGroup(const std::shared_ptr<GroupItem> &group)
{
    for (auto &instanceItem : group->groupReqs) {
        AddPendingAffinityToInstance(instanceItem);
    }
}

bool FairnessPolicy::HasSimilarResourceDemand(const std::shared_ptr<InstanceItem> &instance)
{
    uint16_t priority = instance->GetPriority();
    // If a pending instance has no resource requiredAffinity/requiredAntiAffinity requirements,
    // it means it will consume all available resources, so new instances will always conflict with it.
    if (ExistNonAffinityPendingInstances(priority)) {
        YRLOG_DEBUG("Pending instance exists with no resource requiredAffinity/requiredAntiAffinity requirements.");
        return true;
    }

    std::string resourceAffinityString = EMPTY_PENDING_AFFINITY_STRING;
    const auto &affinity = instance->scheduleReq->instance().scheduleoption().affinity();
    if (affinity.has_resource() &&
        (affinity.resource().has_requiredaffinity() || affinity.resource().has_requiredantiaffinity())) {
        auto resourceAffinity = instance->scheduleReq->instance().scheduleoption().affinity().resource();
        resourceAffinity.clear_preferredaffinity();
        resourceAffinity.clear_preferredantiaffinity();
        resourceAffinityString = resourceAffinity.SerializeAsString();
    }

    for (const auto &pendingReqAffinityCount : pendingReqAffinityCountMap_) {
        if (pendingReqAffinityCount.first < priority) {
            continue;
        }
        if (pendingReqAffinityCount.second.find(resourceAffinityString) != pendingReqAffinityCount.second.end() &&
            pendingReqAffinityCount.second.at(resourceAffinityString) > 0) {
            return true;
        }
    }
    return false;
}

bool FairnessPolicy::HasSimilarPendingRequest(const std::shared_ptr<QueueItem> &item)
{
    if (item->GetItemType() == QueueItemType::INSTANCE) {
        auto instance = std::dynamic_pointer_cast<InstanceItem>(item);
        return HasSimilarResourceDemand(instance);
    }

    if (item->GetItemType() == QueueItemType::GROUP) {
        auto group = std::dynamic_pointer_cast<GroupItem>(item);
        if (group->groupReqs.empty()) {
            YRLOG_WARN("{}|schedule requests are empty", group->GetRequestId());
            return false;
        }

        auto rangeOpt = group->GetRangeOpt();
        if (rangeOpt.isRange) {
            return HasSimilarResourceDemand(group->groupReqs[0]);
        }

        for (auto &instanceItem : group->groupReqs) {
            auto instance = std::dynamic_pointer_cast<InstanceItem>(instanceItem);
            if (HasSimilarResourceDemand(instance)) {
                return true;
            }
        }
    }

    return false;
}

bool FairnessPolicy::CanSchedule(const std::shared_ptr<QueueItem> &item)
{
    return !HasSimilarPendingRequest(item);
}

void FairnessPolicy::PrepareForScheduling(const std::shared_ptr<QueueItem> &item)
{
    if (item->GetItemType() == QueueItemType::INSTANCE) {
        AddPendingAffinityToInstance(std::dynamic_pointer_cast<InstanceItem>(item));
    } else {
        AddPendingAffinityToGroup(std::dynamic_pointer_cast<GroupItem>(item));
    }
}

void FairnessPolicy::ClearPendingInfos()
{
    pendingReqAffinityCountMap_.clear();
    pendingReqAffinityMap_.clear();
}

void FairnessPolicy::StorePendingInfo(const std::shared_ptr<QueueItem> &item)
{
    if (item->GetItemType() == QueueItemType::INSTANCE) {
        StorePendingAffinity(std::dynamic_pointer_cast<InstanceItem>(item));
    } else {
        StorePendingAffinity(std::dynamic_pointer_cast<GroupItem>(item));
    }
}

}