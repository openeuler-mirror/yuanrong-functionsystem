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

#ifndef DOMAIN_DECISION_QUEUE_ITEM_H
#define DOMAIN_DECISION_QUEUE_ITEM_H

#include "async/future.hpp"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_type.h"
#include "common/schedule_decision/scheduler_common.h"
#include "litebus.hpp"

namespace functionsystem::schedule_decision {
const std::string RECEIVED_TIMESTAMP = "receivedTimestamp";
enum class QueueItemType { INSTANCE, GROUP, AGGREGATED_ITEM};

class QueueItem {
public:
    explicit QueueItem(const litebus::Future<std::string> &cancel) : cancelTag(cancel) {}
    explicit QueueItem() = default;
    virtual ~QueueItem() = default;
    virtual QueueItemType GetItemType() = 0;
    virtual std::string GetRequestId() = 0;
    virtual uint16_t GetPriority() = 0;
    virtual int64_t CreatedTimestamp() = 0;
    virtual void AssociateFailure(const StatusCode code, const std::string reason) = 0;
    inline void TagFailure()
    {
        hasFailed = true;
    }
    inline bool HasFailed() const
    {
        return hasFailed;
    }
    litebus::Future<std::string> cancelTag;

private:
    bool hasFailed = false;
};

inline int64_t InstanceCreatedTimeStamp(const resource_view::InstanceInfo &info)
{
    auto iter = info.extensions().find(RECEIVED_TIMESTAMP);
    if (iter == info.extensions().end()) {
        return 0;
    }
    try {
        auto time = std::stoll(iter->second);
        return time;
    } catch (std::exception &e) {
        YRLOG_WARN("{}|invalid created timestamp of instance({}) using zero, e:{}", info.requestid(), info.instanceid(),
                   e.what());
        return 0;
    }
}

class InstanceItem : public QueueItem {
public:
    explicit InstanceItem(const std::shared_ptr<messages::ScheduleRequest> &req,
                          const std::shared_ptr<litebus::Promise<ScheduleResult>> &promise,
                          const litebus::Future<std::string> &cancel)
        : QueueItem(cancel), scheduleReq(req), schedulePromise(promise)
    {}
    ~InstanceItem() override = default;
    QueueItemType GetItemType() override
    {
        return QueueItemType::INSTANCE;
    }
    std::string GetRequestId() override
    {
        return scheduleReq == nullptr ? "" : scheduleReq->requestid();
    }
    uint16_t GetPriority() override
    {
        if (scheduleReq == nullptr || !scheduleReq->has_instance() || !scheduleReq->instance().has_scheduleoption()) {
            return 0;
        }
        return scheduleReq->instance().scheduleoption().priority();
    }
    void AssociateFailure(const StatusCode code, const std::string reason) override
    {
        if (schedulePromise->GetFuture().IsInit()) {
            schedulePromise->Associate(ScheduleResult{ "", static_cast<int32_t>(code), reason, {}, {} });
        }
    }
    int64_t CreatedTimestamp() override
    {
        if (scheduleReq == nullptr || !scheduleReq->has_instance()) {
            return 0;
        }
        return InstanceCreatedTimeStamp(scheduleReq->instance());
    }
    static std::shared_ptr<InstanceItem> CreateInstanceItem(const std::string &reqId, int priority = 0)
    {
        auto req = std::make_shared<messages::ScheduleRequest>();
        req->set_requestid(reqId);
        req->mutable_instance()->mutable_scheduleoption()->set_priority(priority);
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        (*req->mutable_instance()->mutable_extensions())[RECEIVED_TIMESTAMP] = std::to_string(
            static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()));
        litebus::Future<std::string> cancel;
        auto item = std::make_shared<InstanceItem>(
                req, std::make_shared<litebus::Promise<ScheduleResult>>(), litebus::Future<std::string>());
        return item;
    }
    std::shared_ptr<messages::ScheduleRequest> scheduleReq;
    std::shared_ptr<litebus::Promise<ScheduleResult>> schedulePromise;
};

class GroupItem : public QueueItem {
public:
    GroupItem(const std::vector<std::shared_ptr<InstanceItem>> &reqs,
              const std::shared_ptr<litebus::Promise<GroupScheduleResult>> &promise, const std::string &reqId,
              const litebus::Future<std::string> &cancel, const GroupSpec::RangeOpt &opt, const int64_t &timeout = 1)
        : QueueItem(cancel), groupReqs(reqs), groupPromise(promise), groupReqID(reqId), opt(opt), timeout(timeout)
    {
    }
    ~GroupItem() override = default;
    QueueItemType GetItemType() override
    {
        return QueueItemType::GROUP;
    }
    std::string GetRequestId() override
    {
        return groupReqID;
    }
    uint16_t GetPriority() override
    {
        if (groupReqs.empty() || groupReqs.front()->scheduleReq == nullptr ||
            !groupReqs.front()->scheduleReq->has_instance() ||
            !groupReqs.front()->scheduleReq->instance().has_scheduleoption()) {
            return 0;
        }
        return groupReqs.front()->scheduleReq->instance().scheduleoption().priority();
    }
    void AssociateFailure(const StatusCode code, const std::string reason) override
    {
        if (groupPromise->GetFuture().IsInit()) {
            groupPromise->Associate(GroupScheduleResult{ static_cast<int32_t>(code), reason, {} });
        }
    }
    inline GroupSpec::RangeOpt GetRangeOpt() const
    {
        return opt;
    }
    inline int64_t GetTimeout() const
    {
        return timeout;
    }
    int64_t CreatedTimestamp() override
    {
        return InstanceCreatedTimeStamp(groupReqs.front()->scheduleReq->instance());
    }
    static std::shared_ptr<GroupItem> CreateGroupItem(const std::string &reqId, int priority = 0, int insCount = 1)
    {
        std::vector<std::shared_ptr<InstanceItem>> items;
        for (int i = 1; i <= insCount; ++i) {
            items.push_back(InstanceItem::CreateInstanceItem(reqId + "-" + std::to_string(i), priority));
        }
        litebus::Future<std::string> cancel;
        auto groupItem = std::make_shared<GroupItem>(
                items, std::make_shared<litebus::Promise<GroupScheduleResult>>(), reqId, cancel, GroupSpec::RangeOpt());
        return groupItem;
    }
    std::vector<std::shared_ptr<InstanceItem>> groupReqs;
    std::shared_ptr<litebus::Promise<GroupScheduleResult>> groupPromise;
    std::string groupReqID;
    GroupSpec::RangeOpt opt;
    int64_t timeout;
    common::GroupPolicy groupSchedulePolicy;
};


class AggregatedItem : public QueueItem {
public:

    AggregatedItem(const std::string &aggregatedKey, const std::shared_ptr<InstanceItem> &item): aggregatedKey(
        aggregatedKey)
    {
        reqQueue = std::make_shared<std::deque<std::shared_ptr<InstanceItem> > >();
        reqQueue->emplace_back(item);
    }
    void AssociateFailure(const StatusCode code, const std::string reason) override {
        // do nothing for implementing virtual functions
    }
    QueueItemType GetItemType() override
    {
        return QueueItemType::AGGREGATED_ITEM;
    }

    std::string GetRequestId() override
    {
        return (reqQueue == nullptr || reqQueue->empty()) ? "" : reqQueue->front()->GetRequestId();
    }

    uint16_t GetPriority() override
    {
        return (reqQueue == nullptr || reqQueue->empty()) ? 0 : reqQueue->front()->GetPriority();
    }

    int64_t CreatedTimestamp() override
    {
        return (reqQueue == nullptr || reqQueue->empty()) ? 0 : reqQueue->front()->CreatedTimestamp();
    }

    std::string aggregatedKey;
    std::shared_ptr<std::deque<std::shared_ptr<InstanceItem>>> reqQueue;
};

}  // namespace functionsystem::schedule_decision
#endif // DOMAIN_DECISION_QUEUE_ITEM_H
