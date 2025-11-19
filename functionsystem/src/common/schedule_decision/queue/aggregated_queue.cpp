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

#include "common/create_agent_decision/create_agent_decision.h"
#include "common/logs/logging.h"

namespace functionsystem::schedule_decision {

std::string AggregatedQueue::GenerateAggregatedKey(const std::shared_ptr<InstanceItem> &instance)
{
    std::ostringstream oss;
    auto scheduleReq = instance->scheduleReq;
    if (scheduleReq == nullptr || !scheduleReq->has_instance() || !scheduleReq->instance().has_resources()
        || scheduleReq->instance().resources().resources_size() == 0) {
        return "";
    }
    auto cpuResource = scheduleReq->instance().resources().resources().find(resource_view::CPU_RESOURCE_NAME);
    auto memoryResource = scheduleReq->instance().resources().resources().find(resource_view::MEMORY_RESOURCE_NAME);
    auto cpuMnt = cpuResource->second.scalar().value();
    auto memoryMnt = memoryResource->second.scalar().value();
    oss << "priority:" << instance->GetPriority() << "_"
        << "CPU:" << cpuMnt << "_"
        << "Memory:" << memoryMnt;
    auto keyStr = oss.str();
    YRLOG_INFO("success get resource info,keyStr:{}", keyStr);
    return keyStr;
}

bool AggregatedQueue::IsItemNeedAggregate(const std::shared_ptr<QueueItem> &queueItem)
{
    // Instance req related to groupItem and system functions are not aggregated
    if (queueItem->GetItemType() == QueueItemType::GROUP) {
        return false;
    }
    auto instance = std::dynamic_pointer_cast<InstanceItem>(queueItem);
    return !NeedCreateAgentInDomain(instance->scheduleReq->instance(), 0);
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