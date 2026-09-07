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

#include "aggregated_queue.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <vector>

#include "common/create_agent_decision/create_agent_decision.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"

namespace functionsystem::schedule_decision {

namespace {
constexpr size_t SCALAR_SIGNATURE_BASE_CAPACITY = 128;
constexpr std::string_view SCALAR_SIGNATURE_PREFIX = "scalar-fast";
const std::string NUMA_BIND_RESOURCE_KEY = "bind_resource";
const std::string NUMA_BIND_STRATEGY_KEY = "bind_strategy";
const std::string NUMA_BIND_RESOURCE_VALUE = "NUMA";
const std::string NUMA_PACK_STRATEGY = "BIND_Pack";
const std::string NUMA_SPREAD_STRATEGY = "BIND_Spread";

template <typename T>
void AppendScalar(std::string &result, const T &value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    result.append(reinterpret_cast<const char *>(&value), sizeof(T));
}

void AppendString(std::string &result, const std::string &value)
{
    AppendScalar(result, value.size());
    result.append(value);
}

std::string SerializeDeterministically(const google::protobuf::MessageLite &message)
{
    std::string result;
    bool success = false;
    {
        google::protobuf::io::StringOutputStream output(&result);
        google::protobuf::io::CodedOutputStream coded(&output);
        coded.SetSerializationDeterministic(true);
        success = message.SerializeToCodedStream(&coded);
    }
    return success ? result : "";
}

bool HasNumaBinding(const resources::ScheduleOption &option)
{
    const auto &extension = option.extension();
    const auto bindResource = extension.find(NUMA_BIND_RESOURCE_KEY);
    return bindResource != extension.end() && bindResource->second == NUMA_BIND_RESOURCE_VALUE;
}

bool HasNonResourceAffinity(const resource_view::InstanceInfo &instance)
{
    const auto &affinity = instance.scheduleoption().affinity();
    return !affinity.nodeaffinity().affinity().empty() ||
           !affinity.instanceaffinity().affinity().empty() ||
           !affinity.instanceantiaffinity().affinity().empty() || affinity.instance().ByteSizeLong() != 0 ||
           affinity.inner().ByteSizeLong() != 0;
}

void CopyNumaSchedulingInputs(const resources::ScheduleOption &source, resources::ScheduleOption &target)
{
    if (!HasNumaBinding(source)) {
        return;
    }
    auto *extension = target.mutable_extension();
    (*extension)[NUMA_BIND_RESOURCE_KEY] = NUMA_BIND_RESOURCE_VALUE;
    const auto strategy = source.extension().find(NUMA_BIND_STRATEGY_KEY);
    (*extension)[NUMA_BIND_STRATEGY_KEY] =
        strategy != source.extension().end() && strategy->second == NUMA_PACK_STRATEGY
            ? NUMA_PACK_STRATEGY
            : NUMA_SPREAD_STRATEGY;
}

std::string SchedulingInputSignature(const messages::ScheduleRequest &request)
{
    messages::ScheduleRequest normalized;
    const auto &source = request.instance();
    auto *instance = normalized.mutable_instance();
    *instance->mutable_resources() = source.resources();
    const auto &sourceOption = source.scheduleoption();
    auto *option = instance->mutable_scheduleoption();
    option->set_schedpolicyname(sourceOption.schedpolicyname());
    *option->mutable_affinity() = sourceOption.affinity();
    *option->mutable_resourceselector() = sourceOption.resourceselector();
    CopyNumaSchedulingInputs(sourceOption, *option);
    *instance->mutable_labels() = source.labels();
    *instance->mutable_kvlabels() = source.kvlabels();
    // Plugin contexts contain results from previous scans. They must not split
    // otherwise equivalent requests. Strict affinity still depends on these
    // two cross-level inputs, so retain them without the per-Unit caches.
    if (sourceOption.affinity().ByteSizeLong() != 0) {
        const auto context = request.contexts().find(LABEL_AFFINITY_PLUGIN);
        if (context != request.contexts().end() && context->second.has_affinityctx()) {
            const auto &sourceAffinity = context->second.affinityctx();
            auto *affinity = (*normalized.mutable_contexts())[LABEL_AFFINITY_PLUGIN].mutable_affinityctx();
            affinity->set_maxscore(sourceAffinity.maxscore());
            affinity->set_istopdownscheduling(sourceAffinity.istopdownscheduling());
        }
    }
    return SerializeDeterministically(normalized);
}

bool HasOnlyReusableScalarResources(const resource_view::Resources &resources)
{
    const auto &values = resources.resources();
    if (values.find(resource_view::CPU_RESOURCE_NAME) == values.end() ||
        values.find(resource_view::MEMORY_RESOURCE_NAME) == values.end()) {
        return false;
    }
    for (const auto &[name, resource] : values) {
        if (resource_view::IsHeterogeneousResource(name) || resource_view::IsDiskResource(name) ||
            resource.type() != resource_view::ValueType::Value_Type_SCALAR ||
            resource.ranges().ByteSizeLong() != 0 || resource.set().ByteSizeLong() != 0 ||
            resource.vectors().ByteSizeLong() != 0 || resource.disk().ByteSizeLong() != 0 ||
            !resource.heterogeneousinfo().empty() || !resource.extensions().empty() ||
            !resource.runtime().empty() || !resource.driver().empty() || resource.expired()) {
            return false;
        }
    }
    return true;
}

template <typename StringMap>
void AppendSortedStringMap(std::string &result, const StringMap &values)
{
    using Entry = typename std::decay_t<StringMap>::value_type;
    std::vector<const Entry *> ordered;
    ordered.reserve(values.size());
    for (const auto &entry : values) {
        ordered.emplace_back(&entry);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto *left, const auto *right) {
        return left->first < right->first;
    });
    AppendScalar(result, ordered.size());
    for (const auto *entry : ordered) {
        AppendString(result, entry->first);
        AppendString(result, entry->second);
    }
}

void AppendAffinityContextInputs(std::string &result, const messages::ScheduleRequest &request)
{
    int64_t maxScore = 0;
    bool isTopDownScheduling = false;
    const auto context = request.contexts().find(LABEL_AFFINITY_PLUGIN);
    if (context != request.contexts().end() && context->second.has_affinityctx()) {
        maxScore = context->second.affinityctx().maxscore();
        isTopDownScheduling = context->second.affinityctx().istopdownscheduling();
    }
    AppendScalar(result, maxScore);
    AppendScalar(result, isTopDownScheduling);
}

std::string ScalarFastSignature(const messages::ScheduleRequest &request, uint16_t priority)
{
    const auto &instance = request.instance();
    const auto &resources = instance.resources().resources();
    std::string result(SCALAR_SIGNATURE_PREFIX);
    result.reserve(SCALAR_SIGNATURE_BASE_CAPACITY);
    AppendScalar(result, priority);
    using ResourceEntry = typename std::decay_t<decltype(resources)>::value_type;
    std::vector<const ResourceEntry *> orderedResources;
    orderedResources.reserve(resources.size());
    for (const auto &entry : resources) {
        orderedResources.emplace_back(&entry);
    }
    std::sort(orderedResources.begin(), orderedResources.end(), [](const auto *left, const auto *right) {
        return left->first < right->first;
    });
    AppendScalar(result, orderedResources.size());
    for (const auto *entry : orderedResources) {
        AppendString(result, entry->first);
        AppendScalar(result, entry->second.scalar().value());
        AppendScalar(result, entry->second.scalar().limit());
    }
    const auto &option = instance.scheduleoption();
    AppendString(result, SerializeDeterministically(option.affinity().resource()));
    AppendSortedStringMap(result, option.resourceselector());
    if (option.affinity().resource().ByteSizeLong() != 0) {
        AppendAffinityContextInputs(result, request);
    }
    return result;
}
}  // namespace

bool IsScalarAggregationEligible(const messages::ScheduleRequest &request)
{
    if (!request.has_instance()) {
        return false;
    }
    const auto &instance = request.instance();
    const auto &option = instance.scheduleoption();
    if (option.schedpolicyname() != "shared" || HasNonResourceAffinity(instance) || HasNumaBinding(option) ||
        !instance.labels().empty() || !instance.kvlabels().empty()) {
        return false;
    }
    return HasOnlyReusableScalarResources(instance.resources());
}

std::string AggregatedQueue::GenerateAggregatedKey(const std::shared_ptr<InstanceItem> &instance)
{
    auto scheduleReq = instance->scheduleReq;
    if (scheduleReq == nullptr || !scheduleReq->has_instance() || !scheduleReq->instance().has_resources()
        || scheduleReq->instance().resources().resources_size() == 0) {
        return "";
    }
    if (semanticKey_) {
        if (IsScalarAggregationEligible(*scheduleReq)) {
            return ScalarFastSignature(*scheduleReq, instance->GetPriority());
        }
        return "priority:" + std::to_string(instance->GetPriority()) + "_" +
               SchedulingInputSignature(*scheduleReq);
    }

    // Keep the original CPU/Memory-only grouping semantics on the legacy path.
    const auto &resources = scheduleReq->instance().resources().resources();
    auto cpuResource = resources.find(resource_view::CPU_RESOURCE_NAME);
    auto memoryResource = resources.find(resource_view::MEMORY_RESOURCE_NAME);
    if (cpuResource == resources.end() || memoryResource == resources.end()) {
        return "";
    }
    return "priority:" + std::to_string(instance->GetPriority()) + "_CPU:" +
           std::to_string(cpuResource->second.scalar().value()) + "_Memory:" +
           std::to_string(memoryResource->second.scalar().value());
}

bool AggregatedQueue::IsItemNeedAggregate(const std::shared_ptr<QueueItem> &queueItem)
{
    if (!aggregationEnabled_) {
        return false;
    }
    // Instance req related to groupItem and system functions are not aggregated
    if (queueItem->GetItemType() == QueueItemType::GROUP) {
        return false;
    }
    auto instance = std::dynamic_pointer_cast<InstanceItem>(queueItem);
    return instance->conflictedUnitID.empty() &&
           !NeedCreateAgentInDomain(instance->scheduleReq->instance(), 0);
}

void AggregatedQueue::ConfigureAggregation(bool enabled)
{
    if (queueSize_ != 0) {
        YRLOG_WARN("aggregation capability can only be configured before requests are enqueued");
        return;
    }
    aggregationEnabled_ = enabled;
}

Status AggregatedQueue::CheckItemValid(const std::shared_ptr<QueueItem> &queueItem)
{
    if (queueItem == nullptr) {
        YRLOG_WARN("schedule queueItem is nullptr");
        return Status(StatusCode::FAILED, "queueItem is null");
    }
    if (queueItem->GetRequestId().empty()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "get instance requestId failed");
    }
    auto priority = queueItem->GetPriority();
    if (priority > maxPriority_) {
        return Status(StatusCode::ERR_PARAM_INVALID, "instance priority is greater than maxPriority");
    }
    return Status::OK();
}

litebus::Future<Status> AggregatedQueue::Enqueue(const std::shared_ptr<QueueItem> &queueItem)
{
    auto checkResult = CheckItemValid(queueItem);
    if (checkResult != Status::OK()) {
        return checkResult;
    }
    auto priority = queueItem->GetPriority();
    if (!IsItemNeedAggregate(queueItem)) {
        aggregatedReqs[priority].push_back(queueItem);
        queueSize_++;
        return Status::OK();
    }
    auto instance = std::dynamic_pointer_cast<InstanceItem>(queueItem);
    auto keyStr = GenerateAggregatedKey(instance);
    if (keyStr.empty()) {
        return Status(StatusCode::FAILED, "queueItem is invalid");
    }
    auto it = aggregatedReqs.find(queueItem->GetPriority());
    if (strategy_ == STRICTLY_AGGREGATE_STRATEGY) {
        // check elements at the end of the aggregatedItem queue.
        if (it == aggregatedReqs.end() || it->second.back()->GetItemType() != QueueItemType::AGGREGATED_ITEM) {
            auto aggregatedItem = std::make_shared<AggregatedItem>(keyStr, instance);
            aggregatedReqs[instance->GetPriority()].emplace_back(aggregatedItem);
            queueSize_++;
        } else {
            // queue under priority is not empty. Tail element of the queue is of the AggregatedItem type.
            auto backItem = it->second.back();
            auto backAggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(backItem);
            if (backAggregatedItem->aggregatedKey == keyStr) {
                backAggregatedItem->reqQueue->emplace_back(instance);
            } else {
                auto aggregatedItem = std::make_shared<AggregatedItem>(keyStr, instance);
                aggregatedReqs[instance->GetPriority()].emplace_back(aggregatedItem);
                queueSize_++;
            }
        }
    } else if (strategy_ == RELAXED_AGGREGATE_STRATEGY) {
        // whether there are aggregated request queues in the aggregatedItemIndex.
        auto item = aggregatedItemIndex.find(keyStr);
        if (item == aggregatedItemIndex.end()) {
            auto aggregatedItem = std::make_shared<AggregatedItem>(keyStr, instance);
            aggregatedReqs[instance->GetPriority()].emplace_back(aggregatedItem);
            aggregatedItemIndex[keyStr] = aggregatedItem;
            queueSize_++;
        } else {
            item->second->reqQueue->emplace_back(instance);
        }
    }
    return Status::OK();
}

std::shared_ptr<QueueItem> AggregatedQueue::Front()
{
    if (CheckIsQueueEmpty()) {
        return nullptr;
    }
    for (int i = maxPriority_; i >= 0; --i) {  // consume req in descending order of priority.
        if (aggregatedReqs.find(i) == aggregatedReqs.end()) {
            continue;
        }
        frontItem_ = aggregatedReqs[i].front();
        frontPriority_ = i;
        return frontItem_;
    }
    return nullptr;
}

litebus::Future<Status> AggregatedQueue::Dequeue()
{
    if (CheckIsQueueEmpty()) {
        return Status(StatusCode::FAILED, "queue is empty");
    }
    // Avoid errors caused by invoking Front before invoking Dequeue.
    if (!CheckIsQueueEmpty() && frontItem_ == nullptr) {
        for (int i = maxPriority_; i >= 0; --i) {
            if (aggregatedReqs.find(i) == aggregatedReqs.end()) {
                continue;
            }
            frontItem_ = aggregatedReqs[i].front();
            frontPriority_ = i;
            break;
        }
    }
    if (frontItem_->GetItemType() == QueueItemType::AGGREGATED_ITEM) {
        auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(frontItem_);
        if (!aggregatedItem->reqQueue->empty()) {
            return Status(StatusCode::FAILED, "aggregateItem.reqQueue is not empty");
        }
        if (strategy_ == RELAXED_AGGREGATE_STRATEGY) {
            aggregatedItemIndex.erase(aggregatedItem->aggregatedKey);
        }
    }
    aggregatedReqs[frontPriority_].pop_front();
    queueSize_--;
    // When the queue of a priority of aggregatedReqs is empty, the key-value pair is removed to save space.
    if (aggregatedReqs[frontPriority_].empty()) {
        aggregatedReqs.erase(frontPriority_);
    }
    frontPriority_ = -1;
    frontItem_ = nullptr;
    YRLOG_DEBUG("dequeue finished,left req size:{}", queueSize_);
    return Status::OK();
}

void AggregatedQueue::Swap(const std::shared_ptr<ScheduleQueue> &targetQueue)
{
    if (targetQueue == nullptr) {
        YRLOG_WARN("targetQueue is nullptr");
        return;
    }
    auto targetAggregatedQueue = std::dynamic_pointer_cast<AggregatedQueue>(targetQueue);
    if (targetAggregatedQueue == nullptr) {
        YRLOG_WARN("targetAggregatedQueue is nullptr");
        return;
    }
    std::swap(aggregatedReqs, targetAggregatedQueue->aggregatedReqs);
    std::swap(queueSize_, targetAggregatedQueue->queueSize_);
    if (strategy_ == RELAXED_AGGREGATE_STRATEGY) {
        std::swap(aggregatedItemIndex, targetAggregatedQueue->aggregatedItemIndex);
    }
}

void AggregatedQueue::Extend(const std::shared_ptr<ScheduleQueue> &targetQueue)
{
    if (!targetQueue) {
        YRLOG_WARN("targetQueue is nullptr");
        return;
    }
    auto targetAggregatedQueue = std::dynamic_pointer_cast<AggregatedQueue>(targetQueue);
    auto targetQueueMap = targetAggregatedQueue->aggregatedReqs;
    for (int i = maxPriority_; i >= 0; i--) {
        if (targetQueueMap.find(i) == targetQueueMap.end()) {
            continue;
        }
        for (const auto &item : targetQueueMap.at(i)) {
            if (item->GetItemType() != QueueItemType::AGGREGATED_ITEM) {
                Enqueue(item);
            } else {
                auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(item);
                for (const auto &inst : *aggregatedItem->reqQueue) {
                    Enqueue(inst);
                }
            }
        }
    }
}

bool AggregatedQueue::CheckIsQueueEmpty()
{
    return queueSize_ == 0;
}

size_t AggregatedQueue::Size()
{
    return queueSize_;
}
}  // namespace functionsystem::schedule_decision
