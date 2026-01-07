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
#include "domain_group_ctrl_actor.h"

#include "async/collect.hpp"
#include "async/defer.hpp"
#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "common/schedule_decision/scheduler_common.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/utils/collect_status.h"
#include "common/utils/struct_transfer.h"
#include "common/utils/time_trigger.h"

namespace functionsystem::domain_scheduler {
using namespace functionsystem::explorer;
using namespace std::placeholders;
using GroupScheduleResult = schedule_decision::GroupScheduleResult;
void DomainGroupCtrlActor::Init()
{
    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        "DomainGroupCtrl", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &DomainGroupCtrlActor::UpdateMasterInfo, leaderInfo);
        });
    Receive("ForwardGroupSchedule", &DomainGroupCtrlActor::ForwardGroupSchedule);
}

void DomainGroupCtrlActor::UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo)
{
    groupManager_ = litebus::AID(GROUP_MANAGER_ACTOR_NAME, leaderInfo.address);
    groupManager_.SetProtocol(litebus::BUS_TCP);
}

std::shared_ptr<GroupScheduleContext> DomainGroupCtrlActor::NewGroupContext(
    const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    auto groupCtx = std::make_shared<GroupScheduleContext>();
    groupCtx->beginTime = std::chrono::high_resolution_clock::now();
    groupCtx->rangeScheduleLoopTime = std::chrono::high_resolution_clock::now();
    groupCtx->retryTimes = 0;
    groupCtx->schedulePromise = std::make_shared<litebus::Promise<Status>>();
    groupCtx->groupInfo = groupInfo;
    std::shared_ptr<messages::ScheduleRequest> insRangeRequest;
    for (int i = 0; i < groupInfo->requests_size(); i++) {
        auto request = groupInfo->requests(i);
        if (request.isinsrangescheduler()) {
            insRangeRequest = std::make_shared<messages::ScheduleRequest>(request);
            continue;
        }
        groupCtx->requests.emplace_back(std::make_shared<messages::ScheduleRequest>(groupInfo->requests(i)));
    }
    if (insRangeRequest != nullptr) {
        groupCtx->insRangeScheduler = true;
        groupCtx->insRangeRequest = insRangeRequest;
        auto curRangeInstanceNum = insRangeRequest->rangeopts().currangeinstancenum();
        if (groupInfo->rangerequests_size() > 0) {
            for (int i = 0; i < groupInfo->rangerequests_size(); i++) {
                auto request = groupInfo->rangerequests(i);
                groupCtx->requests.emplace_back(std::make_shared<messages::ScheduleRequest>(
                    groupInfo->rangerequests(i)));
                groupCtx->rangeRequests.emplace_back(std::make_shared<messages::ScheduleRequest>(
                    groupInfo->rangerequests(i)));
            }
        } else {
            for (std::int32_t i = 0; i < curRangeInstanceNum; i++) {
                std::shared_ptr<messages::ScheduleRequest> rangeReq =
                    std::make_shared<messages::ScheduleRequest>(*insRangeRequest);
                auto instanceID = rangeReq->instance().instanceid();
                auto requestID = rangeReq->instance().requestid();
                rangeReq->mutable_instance()->set_instanceid(instanceID + "-r-" + std::to_string(i));
                rangeReq->mutable_instance()->set_requestid(requestID + "-r-" + std::to_string(i));
                rangeReq->set_requestid(rangeReq->requestid() + "-r-" + std::to_string(i));
                groupCtx->requests.emplace_back(rangeReq);
                groupCtx->rangeRequests.emplace_back(rangeReq);
                YRLOG_DEBUG("{}|{} range schedule instanceID({}), instanceRequestID({})", rangeReq->traceid(),
                            rangeReq->requestid(), rangeReq->instance().instanceid(), rangeReq->instance().requestid());
            }
        }
    }
    groupInfo->clear_requests();
    groupScheduleCtx_[groupInfo->requestid()] = groupCtx;
    return groupCtx;
}

std::shared_ptr<GroupScheduleContext> DomainGroupCtrlActor::UpdateRangeScheduleGroupContext(
    std::shared_ptr<GroupScheduleContext> groupCtx, std::int32_t curRangeInsNum)
{
    ASSERT_IF_NULL(groupCtx);
    if (!groupCtx->insRangeScheduler || groupCtx->insRangeRequest == nullptr) {
        return groupCtx;
    }
    auto insRangeRequest = groupCtx->insRangeRequest;
    auto oldRangeInstanceNum = groupCtx->insRangeRequest->rangeopts().currangeinstancenum();
    groupCtx->insRangeRequest->mutable_rangeopts()->set_currangeinstancenum(curRangeInsNum);
    for (std::int32_t i = 0; i < oldRangeInstanceNum - curRangeInsNum; i++) {
        schedule_framework::ClearContext(*groupCtx->requests.back()->mutable_contexts());
        groupCtx->requests.pop_back();
    }
    for (std::int32_t i = oldRangeInstanceNum; i < curRangeInsNum; i++) {
        if (i < static_cast<int32_t>(groupCtx->rangeRequests.size())) {
            groupCtx->requests.emplace_back(groupCtx->rangeRequests[i]);
            continue;
        }
        std::shared_ptr<messages::ScheduleRequest> rangeReq =
            std::make_shared<messages::ScheduleRequest>(*insRangeRequest);
        auto instanceID = rangeReq->instance().instanceid();
        auto requestID = rangeReq->instance().requestid();
        rangeReq->mutable_instance()->set_instanceid(instanceID + "-r-" + std::to_string(i));
        rangeReq->mutable_instance()->set_requestid(requestID + "-r-" + std::to_string(i));
        rangeReq->set_requestid(rangeReq->requestid() + "-r-" + std::to_string(i));
        groupCtx->requests.emplace_back(rangeReq);
        YRLOG_DEBUG("{}|{} range schedule instanceID({}), instanceRequestID({})", rangeReq->traceid(),
                    rangeReq->requestid(), rangeReq->instance().instanceid(), rangeReq->instance().requestid());
    }
    groupScheduleCtx_[groupCtx->groupInfo->requestid()] = groupCtx;
    return groupCtx;
}

bool DomainGroupCtrlActor::ExistsGroupContext(const std::string &requestID)
{
    return groupScheduleCtx_.find(requestID) != groupScheduleCtx_.end();
}

void DomainGroupCtrlActor::GroupScheduleDone(const std::shared_ptr<GroupScheduleContext> &ctx, const Status &status)
{
    ctx->schedulePromise->SetValue(status);
    (void)groupScheduleCtx_.erase(ctx->groupInfo->requestid());
}

std::shared_ptr<schedule_decision::GroupSpec> BuildGroupSpec(const std::shared_ptr<GroupScheduleContext> &groupCtx,
                                                             const litebus::Future<std::string> &cancelTag)
{
    auto groupSpec = std::make_shared<schedule_decision::GroupSpec>();
    groupSpec->requests = groupCtx->requests;
    groupSpec->groupReqId = groupCtx->groupInfo->requestid();
    groupSpec->cancelTag = cancelTag;
    groupSpec->rangeOpt.isRange = groupCtx->insRangeScheduler;
    groupSpec->groupSchedulePolicy = groupCtx->groupInfo->groupopts().grouppolicy();
    if (groupCtx->insRangeScheduler) {
        groupSpec->rangeOpt.min = groupCtx->insRangeRequest->rangeopts().range().min();
        groupSpec->rangeOpt.max = groupCtx->insRangeRequest->rangeopts().range().max();
        groupSpec->rangeOpt.step = groupCtx->insRangeRequest->rangeopts().range().step();
    }
    groupSpec->timeout = groupCtx->groupInfo->groupopts().timeout();
    return groupSpec;
}

void GroupScheduleDecision(const std::shared_ptr<schedule_decision::ScheduleRecorder> &recorder,
                           const std::shared_ptr<schedule_decision::Scheduler> &scheduler, const litebus::AID &aid,
                           const std::shared_ptr<GroupScheduleContext> &groupCtx, bool priority)
{
    if (!groupCtx->cancelPromise.GetFuture().IsInit()) {
        const auto &reason = groupCtx->cancelPromise.GetFuture().Get();
        YRLOG_WARN("{}|{} group{} schedule decision is already canceled. reason: {}", groupCtx->groupInfo->traceid(),
                   groupCtx->groupInfo->requestid(), groupCtx->groupInfo->groupid(), reason);
        litebus::Async(aid, &DomainGroupCtrlActor::OnGroupScheduleDecision,
                       GroupScheduleResult{ static_cast<int32_t>(StatusCode::ERR_SCHEDULE_CANCELED), reason, {} },
                       groupCtx);
        return;
    }
    auto reqs = groupCtx->requests;
    auto timeout = groupCtx->groupInfo->groupopts().timeout();
    YRLOG_INFO("group(req={}, priority={}, timeout={}) schedule decision", groupCtx->groupInfo->requestid(), priority,
               timeout);
    ASSERT_IF_NULL(scheduler);
    auto spec = BuildGroupSpec(groupCtx, groupCtx->cancelPromise.GetFuture());
    spec->priority = priority;
    auto future = scheduler->GroupScheduleDecision(spec);
    if (timeout > 0) {
        ASSERT_IF_NULL(recorder);
        future = future.After(
            timeout * litebus::SECTOMILLI, [recorder, groupCtx, timeout](
                                               const litebus::Future<GroupScheduleResult> &_1) {
                std::string value = "\nthe group cannot be scheduled within " + std::to_string(timeout) + " s. ";
                return recorder->TryQueryScheduleErr(groupCtx->groupInfo->requestid())
                    .Then([value, groupCtx](const Status &status) -> litebus::Future<GroupScheduleResult> {
                        if (groupCtx->cancelPromise.GetFuture().IsInit()) {
                            groupCtx->cancelPromise.SetFailed(static_cast<int32_t>(StatusCode::ERR_SCHEDULE_CANCELED));
                        }
                        if (!status.IsOk()) {
                            return GroupScheduleResult{ static_cast<int32_t>(status.StatusCode()),
                                                        value + status.RawMessage(),
                                                        {} };
                        }
                        return GroupScheduleResult{
                            static_cast<int32_t>(StatusCode::ERR_SCHEDULE_CANCELED),
                            value + "the possible cause is that the scheduling queue is busy or the scheduling timeout"
                                " configuration is not proper.", {}};
                    });
            });
    }
    future.OnComplete(litebus::Defer(aid, &DomainGroupCtrlActor::OnGroupScheduleDecision, _1, groupCtx));
}

void DomainGroupCtrlActor::OnGroupScheduleDecision(
    const litebus::Future<schedule_decision::GroupScheduleResult> &future,
    const std::shared_ptr<GroupScheduleContext> &ctx)
{
    if (future.IsError()) {
        GroupScheduleDone(ctx,
                          Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to group schedule decision"));
        return;
    }
    auto result = future.Get();
    if (result.code == StatusCode::ERR_PARAM_INVALID) {
        GroupScheduleDone(ctx, Status(static_cast<StatusCode>(result.code), result.reason));
        return;
    }
    if (result.code == StatusCode::SUCCESS) {
        YRLOG_DEBUG("{}|{} schedule decision success for ({}) instance, start to reserve and bind",
                    ctx->groupInfo->traceid(), ctx->groupInfo->requestid(), result.results.size());
        OnGroupScheduleDecisionSuccessful(result.results, ctx);
        return;
    }
    auto &groupInfo = ctx->groupInfo;
    YRLOG_ERROR("{}|{}|failed to schedule group({}) groupName({}) code({}) msg({})", groupInfo->traceid(),
                groupInfo->requestid(), groupInfo->groupid(), groupInfo->groupopts().groupname(), result.code,
                result.reason);
    if (ctx->tryScheduleResults.empty()) {
        GroupScheduleDone(ctx, Status(static_cast<StatusCode>(result.code), result.reason));
        return;
    }
    RollbackReserve(ctx->tryScheduleResults, ctx)
        .OnComplete(litebus::Defer(GetAID(), &DomainGroupCtrlActor::GroupScheduleDone, ctx,
                                   Status(static_cast<StatusCode>(result.code), result.reason)));
}

std::vector<int> DomainGroupCtrlActor::RollbackContext(const std::shared_ptr<GroupScheduleContext> &ctx)
{
    // to ensure strict order-preserving in group scheduling,
    // all instances need to be rolled back after the instance reserve failure.
    // while strict packed, all should be rollback
    bool alreadyFailed = false;
    std::vector<int> failedInds;
    ctx->lastReservedInd = -1;
    for (size_t i = 0; i < ctx->requests.size(); i++) {
        auto request = ctx->requests[i];
        auto requestID = request->requestid();
        bool isSuccessful = ctx->failedReserve.find(requestID) == ctx->failedReserve.end();
        if (ctx->groupInfo->groupopts().grouppolicy() != common::GroupPolicy::StrictPack && isSuccessful
            && !alreadyFailed) {
            ctx->lastReservedInd = static_cast<int>(i);
            continue;
        }
        if (isSuccessful && alreadyFailed && ctx->insRangeScheduler
            && i < static_cast<size_t>(ctx->insRangeRequest->rangeopts().range().min())) {
            YRLOG_INFO("{}|{}|instance({}) do not to rollback, min({})", request->traceid(), requestID,
                       request->instance().instanceid(), ctx->insRangeRequest->rangeopts().range().min());
            ctx->lastReservedInd = static_cast<int>(i);
            continue;
        }

        if (i >= ctx->tryScheduleResults.size()) {
            continue;
        }
        alreadyFailed = true;
        YRLOG_INFO("{}|{}|instance({}) is already failed to reserve, rollback it context to retry", request->traceid(),
                   requestID, request->instance().instanceid());
        schedule_framework::ClearContext(*request->mutable_contexts());
        failedInds.emplace_back(i);
    }
    // nothing to rollback
    if (!alreadyFailed) {
        ctx->lastReservedInd = static_cast<int>(ctx->requests.size()) - 1;
    }
    ctx->failedReserve.clear();
    return failedInds;
}

void DomainGroupCtrlActor::ReleaseUnusedReserve(
    const std::vector<schedule_decision::ScheduleResult> &results,
    const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    size_t reservedInstanceCount = groupCtx->lastReservedInd + 1;
    // Skip releasing unused Reserve instances
    if (results.empty() || results.size() >= reservedInstanceCount) {
        return;
    }
    auto unusedReserveCount = reservedInstanceCount - results.size();
    YRLOG_INFO("{}|{}|group({}) - Released {} unused reserved instances, "
               "range scheduled instances({}) < reserved instances({})",
               groupCtx->groupInfo->traceid(), groupCtx->groupInfo->requestid(), groupCtx->groupInfo->groupid(),
               unusedReserveCount, results.size(), reservedInstanceCount);
    ASSERT_IF_NULL(underlayer_);
    // unreserve failed is not concerned
    for (size_t i = results.size() - 1; i < reservedInstanceCount; i--) {
        underlayer_->UnReserve(groupCtx->requests[i]->contexts().at(GROUP_SCHEDULE_CONTEXT).groupschedctx().reserved(),
                               groupCtx->requests[i]);
        (*groupCtx->requests[i]->mutable_contexts())[GROUP_SCHEDULE_CONTEXT]
            .mutable_groupschedctx()->set_reserved("");
    }
    // last reserved index is the last scheduled instance index
    groupCtx->lastReservedInd = results.size() - 1;
}

void DomainGroupCtrlActor::OnGroupScheduleDecisionSuccessful(
    const std::vector<schedule_decision::ScheduleResult> &results,
    const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    ToReserve(results, groupCtx)
        .OnComplete(litebus::Defer(GetAID(), &DomainGroupCtrlActor::OnReserve, _1, results, groupCtx));
}

void CollectReserves(std::shared_ptr<litebus::Promise<Status>> promise,
                     const std::shared_ptr<GroupScheduleContext> &groupCtx,
                     const litebus::Future<std::list<std::shared_ptr<messages::OnReserves>>> &future)
{
    if (future.IsError()) {
        promise->SetValue(Status(static_cast<StatusCode>(future.GetErrorCode()),
                                 "failed to reserve resource for " + groupCtx->groupInfo->groupid()));
        return;
    }
    bool isError = false;
    auto result = Status::OK();
    std::list<std::shared_ptr<messages::ScheduleResponse>> responses;
    for (auto resps : future.Get()) {
        for (auto resp : resps->responses()) {
            responses.emplace_back(std::make_shared<messages::ScheduleResponse>(resp));
            if (resp.code() == static_cast<int32_t>(StatusCode::SUCCESS)) {
                continue;
            }
            // reserve failed no need to confirm
            isError = true;
            result.AppendMessage("failed to reserve for instance " + resp.instanceid() + " of "
                                 + groupCtx->groupInfo->groupid() + " err: " + resp.message());
        }
    }
    if (isError) {
        promise->SetValue(Status(StatusCode::DOMAIN_SCHEDULER_RESERVE, result.GetMessage()));
        return;
    }
    groupCtx->responses = responses;
    promise->SetValue(result);
}

litebus::Future<Status> DomainGroupCtrlActor::ToReserve(const std::vector<schedule_decision::ScheduleResult> &results,
                                                        const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    ASSERT_FS(groupCtx->requests.size() >= results.size());
    std::unordered_map<std::string, std::vector<size_t>> records;
    for (size_t i = 0; i < results.size(); i++) {
        records[results[i].id].emplace_back(i);
    }
    std::list<litebus::Future<std::shared_ptr<messages::OnReserves>>> reserves;
    for (auto [id, indexs] : records) {
        std::unordered_map<std::string, size_t> requestToIndex;
        auto req = std::make_shared<messages::Reserves>();
        std::string post;
        for (auto i : indexs) {
            post += "_" + std::to_string(i);
            *req->add_reserves() = *groupCtx->requests[i];
            *req->add_instanceids() = groupCtx->requests[i]->instance().instanceid();
            requestToIndex[groupCtx->requests[i]->requestid()] = i;
        }
        req->set_requestid(groupCtx->groupInfo->requestid() + post);
        req->set_traceid(groupCtx->groupInfo->traceid());
        req->set_groupid(groupCtx->groupInfo->groupid());
        req->set_target(HasResourceGroupRequest(groupCtx->requests) ? 
            resources::CreateTarget::RESOURCE_GROUP : resources::CreateTarget::INSTANCE);
        auto future = underlayer_->Reserves(id, req);
        future.OnComplete(
            [groupCtx, requestToIndex](const litebus::Future<std::shared_ptr<messages::OnReserves>> &future) {
                ASSERT_FS(future.IsOK());
                for (auto resp : future.Get()->responses()) {
                    auto &requestID = resp.requestid();
                    auto iter = requestToIndex.find(requestID);
                    if (iter == requestToIndex.end()) {
                        continue;
                    }
                    auto i = iter->second;
                    *(groupCtx->requests[i]->mutable_contexts()) = resp.contexts();
                    // reserved would not to rollback, unless domain group schedule decision failed.
                    if (resp.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
                        (*groupCtx->requests[i]->mutable_contexts())[GROUP_SCHEDULE_CONTEXT]
                            .mutable_groupschedctx()
                            ->set_reserved("");
                        (void)groupCtx->failedReserve.insert(requestID);
                    }
                }
            });
        reserves.emplace_back(future);
    }
    auto promise = std::make_shared<litebus::Promise<Status>>();
    (void)litebus::Collect(reserves).OnComplete(
        [groupCtx, promise](const litebus::Future<std::list<std::shared_ptr<messages::OnReserves>>> &future) {
            CollectReserves(promise, groupCtx, future);
        });
    return promise->GetFuture();
}

void DomainGroupCtrlActor::OnReserve(const litebus::Future<Status> &future,
                                     const std::vector<schedule_decision::ScheduleResult> &results,
                                     const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    // failed to rollback && retry schedule
    if (status.IsError()) {
        // update the schedule results, for group schedule failed rollback reserve
        groupCtx->tryScheduleResults = results;
        YRLOG_WARN("{}|{}| failed to schedule group({}) on reserve resource, directly to retry. ({})",
                   groupCtx->groupInfo->traceid(), groupCtx->groupInfo->requestid(), groupCtx->groupInfo->groupid(),
                   status.ToString());
        // a fresh round to retry, rollback failed scheduled result.
        auto rollbackedInd = RollbackContext(groupCtx);
        ASSERT_IF_NULL(recorder_);
        ASSERT_IF_NULL(scheduler_);
        RollbackRangeReserve(rollbackedInd, groupCtx->tryScheduleResults, groupCtx)
            .OnComplete([aid(GetAID()), recorder(recorder_), scheduler(scheduler_), groupCtx]() {
                GroupScheduleDecision(recorder, scheduler, aid, groupCtx, true);
            });
        return;
    }
    // reserve success
    YRLOG_INFO("{}|{}| group schedule reserve success, groupID({}), groupName({})", groupCtx->groupInfo->traceid(),
               groupCtx->groupInfo->requestid(), groupCtx->groupInfo->groupid(),
               groupCtx->groupInfo->groupopts().groupname());
    ReleaseUnusedReserve(groupCtx->tryScheduleResults, groupCtx);
    litebus::Async(GetAID(), &DomainGroupCtrlActor::ToBind, results, groupCtx)
        .OnComplete(litebus::Defer(GetAID(), &DomainGroupCtrlActor::OnBind, _1, results, groupCtx));
}

litebus::Future<Status> DomainGroupCtrlActor::RollbackRangeReserve(
    const std::vector<int> &rollbackedInd,
    const std::vector<schedule_decision::ScheduleResult> &results,
    const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    if (results.empty()) {
        // If the result is empty, rollback is not required.
        return Status::OK();
    }
    std::list<litebus::Future<Status>> unReserves;
    YRLOG_WARN(
        "{}|{}|group({}) schedule rollback reserved instance, instance({}) to be unreserved",
        groupCtx->groupInfo->traceid(), groupCtx->groupInfo->requestid(), groupCtx->groupInfo->groupid(),
        !rollbackedInd.empty()
            ? std::accumulate(std::next(rollbackedInd.begin()), rollbackedInd.end(), std::to_string(rollbackedInd[0]),
                              [](const std::string &a, int b) { return a + "," + std::to_string(b); })
            : "none");
    ASSERT_IF_NULL(underlayer_);
    for (auto i : rollbackedInd) {
        unReserves.emplace_back(underlayer_->UnReserve(results[i].id, groupCtx->requests[i]));
    }
    auto promise = std::make_shared<litebus::Promise<Status>>();
    // unreserve failed is not concerned
    (void)litebus::Collect(unReserves).OnComplete([promise]() { promise->SetValue(Status::OK()); });
    return promise->GetFuture();
}

litebus::Future<Status> DomainGroupCtrlActor::RollbackReserve(
    const std::vector<schedule_decision::ScheduleResult> &results,
    const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    if (results.empty()) {
        // If the result is empty, rollback is not required.
        return Status::OK();
    }
    ASSERT_FS(groupCtx->requests.size() >= results.size());
    std::list<litebus::Future<Status>> unReserves;
    for (size_t i = 0; i < results.size(); i++) {
        unReserves.emplace_back(underlayer_->UnReserve(results[i].id, groupCtx->requests[i]));
    }
    auto promise = std::make_shared<litebus::Promise<Status>>();
    // unreserve failed is not concerned
    (void)litebus::Collect(unReserves).OnComplete([promise]() { promise->SetValue(Status::OK()); });
    return promise->GetFuture();
}

Status GenerateNodeDeviceMap(const std::shared_ptr<GroupScheduleContext> &groupCtx,
    std::unordered_map<std::string, std::set<common::HeteroDeviceInfo, HeteroDeviceCompare>> &nodeDeviceMap,
    std::unordered_map<std::string, std::vector<std::string>> &insDeviceIpMap)
{
    auto &groupInfo = groupCtx->groupInfo;
    for (const auto &resp : groupCtx->responses) {
        auto instanceId = resp->instanceid();
        auto &scheduleResult = resp->scheduleresult();
        auto nodeId = scheduleResult.nodeid();
        if (nodeId.empty()) {
            YRLOG_WARN("{}|{} nodeId of scheduleResult is empty, group id: {}， instanId: {}", groupInfo->traceid(),
                       groupInfo->requestid(), groupInfo->groupid(), instanceId);
            return Status(ERR_INNER_SYSTEM_ERROR, "responses scheduleResult is empty");
        }
        if (scheduleResult.devices_size() == 0) {
            YRLOG_WARN("{}|{} device info of scheduleResult is empty, group id: {}， instanId: {}",
                       groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid(), instanceId);
            return Status(ERR_INNER_SYSTEM_ERROR, "responses scheduleResult is empty");
        }
        for (auto &device : scheduleResult.devices()) {
            insDeviceIpMap[instanceId].push_back(device.deviceip());
            nodeDeviceMap[nodeId].insert(device);
        }
    }
    return Status::OK();
}

Status GenerateServerList(const std::shared_ptr<GroupScheduleContext> &groupCtx,
                          common::FunctionGroupRunningInfo &functionGroupRunningInfo,
                          std::unordered_map<std::string, int> &insRankIdMap)
{
    // key:ndoeId
    std::unordered_map<std::string, std::set<common::HeteroDeviceInfo, HeteroDeviceCompare>> nodeDeviceMap{};
    // key:instanceId  value:deviceIp list
    std::unordered_map<std::string, std::vector<std::string>> insDeviceIpMap{};
    // key:deviceIp  value:device rankId
    std::unordered_map<std::string, int> deviceIP2DeviceRankIdMap{};

    if (auto status = GenerateNodeDeviceMap(groupCtx, nodeDeviceMap, insDeviceIpMap);
        status.IsError()) {
        return status;
    }

    int rankId = 0;
    for (auto &pair : nodeDeviceMap) {
        common::ServerInfo serverInfo;
        serverInfo.set_serverid(pair.first);
        for (auto device : pair.second) {
            device.set_rankid(rankId);
            deviceIP2DeviceRankIdMap[device.deviceip()] = rankId;
            rankId += 1;
            (*serverInfo.add_devices()) = std::move(device);
        }
        (*functionGroupRunningInfo.add_serverlist()) = std::move(serverInfo);
    }
    GenerateInsRankId(insDeviceIpMap, deviceIP2DeviceRankIdMap, insRankIdMap);

    return Status::OK();
}

Status GenerateFunctionGroupRunningInfo(const std::shared_ptr<GroupScheduleContext> &groupCtx,
                                        common::FunctionGroupRunningInfo &functionGroupRunningInfo,
                                        std::unordered_map<std::string, int> &insRankIdMap)
{
    ASSERT_FS(groupCtx->requests.size() == groupCtx->responses.size());
    auto &groupInfo = groupCtx->groupInfo;
    if (groupCtx->responses.empty()) {
        YRLOG_WARN("{}|{} the group({}) responses is empty",
                   groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());
        return Status(ERR_INNER_SYSTEM_ERROR, "schedule responses is empty");
    }

    if (auto status = GenerateServerList(groupCtx, functionGroupRunningInfo, insRankIdMap); status.IsError()) {
        return status;
    }

    functionGroupRunningInfo.set_worldsize(groupCtx->responses.size());
    return Status::OK();
}

litebus::Future<Status> DomainGroupCtrlActor::ToBind(const std::vector<schedule_decision::ScheduleResult> &results,
                                                     const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    ASSERT_FS(groupCtx->requests.size() >= results.size());
    std::list<litebus::Future<Status>> binds;
    if (!HasHeterogeneousRequest(groupCtx->requests) || HasResourceGroupRequest(groupCtx->requests)) {
        for (size_t i = 0; i < results.size(); i++) {
            binds.emplace_back(underlayer_->Bind(results[i].id, groupCtx->requests[i]));
        }
        return CollectStatus(binds, "bind instance on group schedule");
    }

    auto &groupInfo = groupCtx->groupInfo;
    YRLOG_INFO("{}|{} the group({}) requests require heterogeneous resources",
               groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());

    common::FunctionGroupRunningInfo functionGroupRunningInfo{};
    std::unordered_map<std::string, int> insRankIdMap{};
    if (auto status = GenerateFunctionGroupRunningInfo(groupCtx, functionGroupRunningInfo, insRankIdMap);
        status.IsError()) {
        auto &groupInfo = groupCtx->groupInfo;
        YRLOG_WARN("{}|{} failed to generate functionGroupRunningInfo, group id: {}",
                   groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());
        return status;
    }
    YRLOG_DEBUG("{}|{} group id: {}, functionGroupRunningInfo: {}", groupInfo->traceid(),
                groupInfo->requestid(), groupInfo->groupid(), functionGroupRunningInfo.DebugString());
    for (size_t i = 0; i < results.size(); i++) {
        auto &scheduleRequest = groupCtx->requests[i];
        auto instanceId = scheduleRequest->instance().instanceid();
        functionGroupRunningInfo.set_instancerankid(insRankIdMap[instanceId]);
        functionGroupRunningInfo.set_devicename(results[i].heteroProductName);
        std::string groupRunningInfoStr;
        if (!google::protobuf::util::MessageToJsonString(functionGroupRunningInfo, &groupRunningInfoStr).ok()) {
            YRLOG_WARN("{}|{} failed to trans functionGroupRunningInfo to json, group id: {}",
                       groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());
            return Status(StatusCode::ERR_INNER_SYSTEM_ERROR,
                          "failed to trans function Group RunningInfo to json string");
        }
        (*scheduleRequest->mutable_instance()->mutable_createoptions())["FUNCTION_GROUP_RUNNING_INFO"] =
                groupRunningInfoStr;
        binds.emplace_back(underlayer_->Bind(results[i].id, scheduleRequest));
    }
    return CollectStatus(binds, "bind instance on group schedule");
}

void DomainGroupCtrlActor::OnBind(const litebus::Future<Status> &future,
                                  const std::vector<schedule_decision::ScheduleResult> &results,
                                  const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    auto groupInfo = groupCtx->groupInfo;
    if (status.IsError()) {
        YRLOG_WARN("{}|{}| failed to bind instance, going to rollback. group({}:{}) reason({})", groupInfo->traceid(),
                   groupInfo->requestid(), groupInfo->groupid(), groupInfo->groupopts().groupname(), status.ToString());
        litebus::Async(GetAID(), &DomainGroupCtrlActor::RollbackBind, results, groupCtx)
            .OnComplete(litebus::Defer(GetAID(), &DomainGroupCtrlActor::OnRollbackBind, _1, groupCtx));
        return;
    }
    // reserve success
    YRLOG_INFO("{}|{}| group schedule successful, groupID({}), groupName({})", groupCtx->groupInfo->traceid(),
               groupCtx->groupInfo->requestid(), groupCtx->groupInfo->groupid(),
               groupCtx->groupInfo->groupopts().groupname());
    if (groupCtx->insRangeScheduler) {
        groupCtx->insRangeRequest->mutable_rangeopts()->set_currangeinstancenum(results.size());
    }
    groupCtx->tryScheduleResults = results;
    GroupScheduleDone(groupCtx, Status::OK());
}

litebus::Future<Status> DomainGroupCtrlActor::RollbackBind(
    const std::vector<schedule_decision::ScheduleResult> &results,
    const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    ASSERT_FS(groupCtx->requests.size() >= results.size());
    std::list<litebus::Future<Status>> unBinds;
    for (size_t i = 0; i < results.size(); i++) {
        unBinds.emplace_back(underlayer_->UnBind(results[i].id, groupCtx->requests[i]));
    }
    return CollectStatus(unBinds, "rollback bind instance on group schedule",
                         StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER);
}

void DomainGroupCtrlActor::OnRollbackBind(const litebus::Future<Status> &future,
                                          const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    auto groupInfo = groupCtx->groupInfo;
    if (status.StatusCode() == StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER) {
        YRLOG_WARN(
            "{}|{}| node fault occurs during group({}:{}) scheduling, try forwarded to the group manager for "
            "coordination and scheduling. err({})",
            groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid(), groupInfo->groupopts().groupname(),
            status.ToString());
        // forward group to group manager in the future
        GroupScheduleDone(groupCtx, Status(StatusCode::ERR_GROUP_SCHEDULE_FAILED,
                                           "node fault occurs during group schedule, please retry."));
        return;
    }
    YRLOG_WARN("{}|{}| rollback group schedule done, try to reschedule group({}:{})", groupInfo->traceid(),
               groupInfo->requestid(), groupInfo->groupid(), groupInfo->groupopts().groupname());
    ASSERT_IF_NULL(recorder_);
    ASSERT_IF_NULL(scheduler_);
    GroupScheduleDecision(recorder_, scheduler_, GetAID(), groupCtx, true);
}

void DomainGroupCtrlActor::ForwardGroupSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto groupInfo = std::make_shared<messages::GroupInfo>();
    if (!groupInfo->ParseFromString(msg)) {
        YRLOG_WARN("received ForwardGroupSchedule from {}, invalid msg {} ignore", std::string(from), msg);
        return;
    }
    if (groupScheduleCtx_.find(groupInfo->requestid()) != groupScheduleCtx_.end()) {
        YRLOG_WARN("{}|{}|Group is scheduling, groupID({}) groupName({}) instanceNum({}), ignore duplicate request",
                   groupInfo->traceid(), groupInfo->requestid(), std::string(from), groupInfo->groupid(),
                   groupInfo->groupopts().groupname(), groupInfo->requests_size());
        return;
    }
    YRLOG_INFO("{}|{}|received ForwardGroupSchedule from {}, groupID({}) groupName({}) instanceNum({})",
               groupInfo->traceid(), groupInfo->requestid(), std::string(from), groupInfo->groupid(),
               groupInfo->groupopts().groupname(), groupInfo->requests_size());
    auto groupCtx = NewGroupContext(groupInfo);
    if (groupInfo->insrangescheduler()) {
        auto rangeReq = groupCtx->insRangeRequest;
        OnRangeInstanceSchedule(rangeReq, groupCtx);
    } else {
        ASSERT_IF_NULL(recorder_);
        ASSERT_IF_NULL(scheduler_);
        GroupScheduleDecision(recorder_, scheduler_, GetAID(), groupCtx, false);
    }
    groupCtx->schedulePromise->GetFuture().OnComplete(
        litebus::Defer(GetAID(), &DomainGroupCtrlActor::OnGroupScheduleDone, from, _1, groupCtx));
}

void DomainGroupCtrlActor::OnRangeInstanceSchedule(
    std::shared_ptr<messages::ScheduleRequest> rangeReq,
    std::shared_ptr<GroupScheduleContext> groupCtx)
{
    auto traceID = rangeReq->traceid();
    auto requestID = rangeReq->requestid();
    auto numberMax = rangeReq->rangeopts().range().max();
    YRLOG_INFO("{}|{}|start range schedule from num({})", rangeReq->traceid(), rangeReq->requestid(), numberMax);
    UpdateRangeScheduleGroupContext(groupCtx, numberMax);
    ASSERT_IF_NULL(recorder_);
    ASSERT_IF_NULL(scheduler_);
    GroupScheduleDecision(recorder_, scheduler_, GetAID(), groupCtx, false);
}

void DomainGroupCtrlActor::OnGroupScheduleDone(const litebus::AID &from, const litebus::Future<Status> &future,
                                               const std::shared_ptr<GroupScheduleContext> &groupCtx)
{
    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    auto groupInfo = groupCtx->groupInfo;
    YRLOG_INFO("{}|{}|finished group schedule from {}, groupID({}) groupName({}). code({}) msg({})",
               groupInfo->traceid(), groupInfo->requestid(), std::string(from), groupInfo->groupid(),
               groupInfo->groupopts().groupname(), fmt::underlying(status.StatusCode()), status.GetMessage());
    messages::GroupResponse resp;
    resp.set_requestid(groupInfo->requestid());
    resp.set_code(status.StatusCode());
    if (groupCtx->insRangeScheduler) {
        YRLOG_DEBUG("{}|{} it's range instance schedule, update range instance num: {}",
                    groupInfo->traceid(), groupInfo->requestid(),
                    groupCtx->insRangeRequest->rangeopts().currangeinstancenum());
        resp.set_rangesuccessnum(groupCtx->insRangeRequest->rangeopts().currangeinstancenum());
    }
    if (status.StatusCode() == StatusCode::SUCCESS) {
        ASSERT_FS(groupCtx->tryScheduleResults.size() <= groupCtx->requests.size());
        for (size_t i = 0; i < groupCtx->tryScheduleResults.size(); i++) {
            auto schedule = messages::ScheduleResult();
            schedule.set_nodeid(groupCtx->tryScheduleResults[i].id);
            (*resp.mutable_scheduleresults())[groupCtx->requests[i]->instance().instanceid()] = std::move(schedule);
        }
    }
    resp.set_message(status.RawMessage());
    Send(from, "OnForwardGroupSchedule", resp.SerializeAsString());
}

bool IsGroupSchedulingMatched(const std::shared_ptr<GroupScheduleContext> &ctx,
                              const std::shared_ptr<messages::CancelSchedule> &cancelRequest)
{
    if (cancelRequest->type() == messages::CancelType::JOB) {
        const auto &jobID = cancelRequest->id();
        return ctx->groupInfo->traceid().find(jobID) != std::string::npos;
    }
    if (cancelRequest->type() == messages::CancelType::GROUP) {
        const auto &groupID = cancelRequest->id();
        return ctx->groupInfo->groupid() == groupID;
    }
    if (cancelRequest->type() == messages::CancelType::PARENT) {
        const auto &parent = cancelRequest->id();
        return ctx->groupInfo->parentid() == parent;
    }
    if (cancelRequest->type() == messages::CancelType::FUNCTION) {
        const auto &function = cancelRequest->id();
        for (auto &req : ctx->requests) {
            if (req->instance().function() == function) {
                return true;
            }
        }
    }
    return false;
}

void DomainGroupCtrlActor::TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest)
{
    if (cancelRequest->type() == messages::CancelType::REQUEST) {
        if (groupScheduleCtx_.find(cancelRequest->id()) != groupScheduleCtx_.end()) {
            auto ctx = groupScheduleCtx_[cancelRequest->id()];
            YRLOG_INFO("{}|{}|try cancel schedule group({}), reason:({})", ctx->groupInfo->traceid(),
                       ctx->groupInfo->requestid(), ctx->groupInfo->groupid(), cancelRequest->reason());
            ctx->cancelPromise.SetValue(cancelRequest->reason());
            return;
        }
    }
    for (auto [requestID, ctx] : groupScheduleCtx_) {
        if (IsGroupSchedulingMatched(ctx, cancelRequest)) {
            YRLOG_INFO("{}|{}|try cancel schedule group({}), reason:({})", ctx->groupInfo->traceid(),
                       requestID, ctx->groupInfo->groupid(), cancelRequest->reason());
            ctx->cancelPromise.SetValue(cancelRequest->reason());
        }
    }
}

std::vector<std::shared_ptr<messages::ScheduleRequest>> DomainGroupCtrlActor::GetRequests()
{
    std::vector<std::shared_ptr<messages::ScheduleRequest>> requests;

    for (const auto &pair : groupScheduleCtx_) {
        requests.insert(requests.end(), pair.second->requests.begin(), pair.second->requests.end());
    }

    return requests;
}
}  // namespace functionsystem::domain_scheduler