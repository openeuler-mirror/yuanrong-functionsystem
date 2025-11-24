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
#include "local_group_ctrl_actor.h"
#include "async/collect.hpp"
#include "async/defer.hpp"
#include "common/constants/metastore_keys.h"
#include "common/types/instance_state.h"
#include "common/utils/collect_status.h"
#include "common/utils/struct_transfer.h"
#include "function_proxy/local_scheduler/local_scheduler_service/local_sched_srv.h"
namespace functionsystem::local_scheduler {
const int32_t MAX_GROUP_INSTANCE_SIZE = 256;
const int32_t MAX_RESERVE_TIMEOUT_MS = 120000;
const int32_t MIN_INSTANCE_RANGE_NUM = 1;
const int32_t MAX_INSTANCE_RANGE_NUM = 256;
const int32_t DEFAULT_INSTANCE_RANGE_STEP = 2;
const int64_t MAX_GROUP_SCHEDULE_TIMEOUT_LIMIT_SEC = 600;
const int64_t DEFAULT_GROUP_SCHEDULE_TIMEOUT_LIMIT_SEC = 600;

Status ValidInstanceRangeParam(const core_service::InstanceRange &range)
{
    auto numberMin = range.min();
    auto numberMax = range.max();
    auto step = range.step();
    if (numberMin <= 0) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid range param min(" +
            std::to_string(numberMin) + "), should bigger than 0");
    }
    if (numberMax <= 0) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid range param max(" +
            std::to_string(numberMax) + "), should bigger than 0");
    }
    if (numberMax < numberMin) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid range param max(" +
            std::to_string(numberMax) + "), should bigger than min(" +
            std::to_string(numberMin) + ")");
    }
    if (numberMax > MAX_INSTANCE_RANGE_NUM) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid range param max(" + std::to_string(numberMax) +
            "), should be range (0, " + std::to_string(MAX_INSTANCE_RANGE_NUM) + "]");
    }
    if (step <= 0) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid range param step(" +
            std::to_string(step) + "), should bigger than 0");
    }
    return Status::OK();
}

void MutatingInstanceRangeParam(core_service::InstanceRange *range)
{
    if (range->min() == 0 || range->min() == -1) {
        range->set_min(MIN_INSTANCE_RANGE_NUM);
    }
    if (range->max() == 0 || range->max() == -1) {
        range->set_max(MAX_INSTANCE_RANGE_NUM);
    }
    if (range->step() == 0 || range->step() == -1) {
        range->set_step(DEFAULT_INSTANCE_RANGE_STEP);
    }
}

std::shared_ptr<schedule_decision::GroupSpec> BuildGroupSpec(const std::shared_ptr<GroupContext> &groupCtx)
{
    auto groupSpec = std::make_shared<schedule_decision::GroupSpec>();
    groupSpec->requests = groupCtx->requests;
    groupSpec->groupReqId = groupCtx->groupInfo->requestid();
    groupSpec->cancelTag = litebus::Future<std::string>();
    groupSpec->rangeOpt.isRange = groupCtx->insRangeScheduler;
    if (groupCtx->insRangeScheduler) {
        groupSpec->rangeOpt.min = groupCtx->insRangeRequest->rangeopts().range().min();
        groupSpec->rangeOpt.max = groupCtx->insRangeRequest->rangeopts().range().max();
        groupSpec->rangeOpt.step = groupCtx->insRangeRequest->rangeopts().range().step();
    }
    groupSpec->timeout = groupCtx->groupInfo->groupopts().timeout();
    return groupSpec;
}

LocalGroupCtrlActor::LocalGroupCtrlActor(const std::string &name, const std::string &nodeID,
                                         const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    : LocalGroupCtrlActor(name, nodeID, metaStoreClient, MAX_RESERVE_TIMEOUT_MS)
{
}

LocalGroupCtrlActor::LocalGroupCtrlActor(const std::string &name, const std::string &nodeID,
                                         const std::shared_ptr<MetaStoreClient> &metaStoreClient,
                                         int32_t reservedTimeout)
    : BasisActor(name),
      nodeID_(nodeID),
      groupOperator_(std::make_shared<GroupOperator>(metaStoreClient)),
      reserveToBindTimeoutMs_(reservedTimeout)
{
}

void LocalGroupCtrlActor::Init()
{
    ActorBase::Init();
    Receive("Reserves", &LocalGroupCtrlActor::Reserves);
    Receive("UnReserve", &LocalGroupCtrlActor::UnReserve);
    Receive("Bind", &LocalGroupCtrlActor::Bind);
    Receive("UnBind", &LocalGroupCtrlActor::UnBind);
    Receive("ClearGroup", &LocalGroupCtrlActor::ClearGroup);
    ASSERT_IF_NULL(instanceCtrl_);
    instanceCtrl_->RegisterClearGroupInstanceCallBack([aid(GetAID())](const InstanceInfo &info) {
        litebus::Async(aid, &LocalGroupCtrlActor::ClearLocalGroupInstanceInfo, info);
    });
}

void LocalGroupCtrlActor::ClearLocalGroupInstanceInfo(const InstanceInfo &info)
{
    (void)reserveResult_.erase(info.requestid());
    (void)bindingReqs_.erase(info.requestid());
}

litebus::Future<Status> LocalGroupCtrlActor::Sync()
{
    ASSERT_IF_NULL(groupOperator_);
    YRLOG_INFO("start to sync group info.");
    return groupOperator_->SyncGroupInstances().Then(
        litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnSyncGroup, std::placeholders::_1));
}

litebus::Future<Status> LocalGroupCtrlActor::OnSyncGroup(
    const std::vector<std::shared_ptr<messages::GroupInfo>> &groupInfos)
{
    for (const auto &info : groupInfos) {
        if (info->ownerproxy() != nodeID_) {
            continue;
        }
        (void)NewGroupCtx(info);
    }
    return Status::OK();
}

litebus::Future<Status> LocalGroupCtrlActor::Recover()
{
    for (auto [reqID, groupCtx] : groupCtxs_) {
        auto info = groupCtx->groupInfo;
        YRLOG_INFO("{}|{}|recover group({}) status({})", info->traceid(), reqID, info->groupid(), info->status());
        if (info->status() == static_cast<int32_t>(GroupState::SCHEDULING)) {
            // forward to instance manager for
            auto resp = std::make_shared<CreateResponses>();
            if (groupCtx->insRangeScheduler) {
                groupCtx->persistingPromise.Associate(ForwardGroupSchedule(groupCtx, resp));
                groupCtx->persistingPromise.GetFuture().OnComplete(
                    litebus::Defer(GetAID(), &LocalGroupCtrlActor::CollectInstancesReady, groupCtx));
                continue;
            }
            ForwardGroupSchedule(groupCtx, resp)
                .OnComplete(litebus::Defer(GetAID(), &LocalGroupCtrlActor::CollectInstancesReady, groupCtx));
            resp->set_code(common::ErrorCode::ERR_NONE);
            resp->set_groupid(info->groupid());
            for (auto request : groupCtx->requests) {
                *resp->add_instanceids() = request->instance().instanceid();
            }
            groupCtx->persistingPromise.SetValue(resp);
            continue;
        }
        if (info->status() == static_cast<int32_t>(GroupState::FAILED)) {
            // re notify to remind user kill group
            NotifyGroupResult(
                Status(StatusCode::ERR_GROUP_SCHEDULE_FAILED, info->groupid() + " is already failed. caused by: \n\t" +
                                                                  info->message() + "please kill it to recycle"),
                info->parentid(), groupCtx);
            continue;
        }
    }
    isStarted = true;
    return Status::OK();
}

bool LocalGroupCtrlActor::IsDuplicateGroup(const std::string &from, const std::shared_ptr<CreateRequests> &req)
{
    auto requestID = req->requestid();
    auto iter = groupCtxs_.find(requestID);
    if (iter == groupCtxs_.end()) {
        return false;
    }
    auto groupInfo = iter->second->groupInfo;
    YRLOG_INFO("{}|request already exist. groupID({}) instance num({})", requestID, groupInfo->groupid(),
               groupInfo->requests_size());
    if (groupInfo->status() == static_cast<int32_t>(GroupState::RUNNING)) {
        NotifyGroupResult(Status::OK(), from, iter->second);
    }
    return true;
}

std::shared_ptr<GroupContext> LocalGroupCtrlActor::NewGroupCtx(const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    auto groupCtx = std::make_shared<GroupContext>();
    groupCtx->groupInfo = groupInfo;
    groupCtx->persistingPromise = litebus::Promise<std::shared_ptr<CreateResponses>>();
    std::shared_ptr<messages::ScheduleRequest> insRangeRequest;
    for (auto request : groupInfo->requests()) {
        if (request.isinsrangescheduler()) {
            insRangeRequest = std::make_shared<messages::ScheduleRequest>(request);
            continue;
        }
        groupCtx->requests.emplace_back(std::make_shared<messages::ScheduleRequest>(request));
    }
    if (insRangeRequest != nullptr) {
        groupCtx->insRangeScheduler = true;
        groupCtx->insRangeRequest = insRangeRequest;
        auto curRangeInstanceNum = insRangeRequest->rangeopts().currangeinstancenum();
        if (groupInfo->rangerequests_size() == curRangeInstanceNum) {
            for (auto request : groupInfo->rangerequests()) {
                groupCtx->requests.emplace_back(std::make_shared<messages::ScheduleRequest>(request));
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
                YRLOG_DEBUG("{}|{} range schedule instanceID({}), instanceRequestID({})", rangeReq->traceid(),
                            rangeReq->requestid(), rangeReq->instance().instanceid(), rangeReq->instance().requestid());
            }
        }
    }
    groupCtxs_[groupCtx->groupInfo->requestid()] = groupCtx;
    return groupCtx;
}

void LocalGroupCtrlActor::DeleteGroupCtx(const std::string &requestID)
{
    (void)groupCtxs_.erase(requestID);
}

std::shared_ptr<GroupContext> LocalGroupCtrlActor::GetGroupCtx(const std::string &requestID)
{
    return groupCtxs_.find(requestID) != groupCtxs_.end() ? groupCtxs_[requestID] : nullptr;
}

Status CheckAndUpdateRangeInstanceSchedule(const std::shared_ptr<messages::GroupInfo> &groupInfo,
                                           CreateRequest createReq, messages::ScheduleRequest* scheduleReq)
{
    MutatingInstanceRangeParam(createReq.mutable_schedulingops()->mutable_range());
    auto status = ValidInstanceRangeParam(createReq.schedulingops().range());
    if (!status.IsOk()) {
        return status;
    }
    groupInfo->set_insrangescheduler(true);
    scheduleReq->set_isinsrangescheduler(true);
    *scheduleReq->mutable_rangeopts()->mutable_range() = *createReq.mutable_schedulingops()->mutable_range();
    scheduleReq->mutable_rangeopts()->set_currangeinstancenum(createReq.schedulingops().range().max());
    *groupInfo->mutable_insrange() = createReq.schedulingops().range();
    YRLOG_DEBUG("{}|{} create range schedule groupInfo, owner({}), groupOpt: timeout({}), groupName({}),"
                "sameRunningLifeCycle({}), min({}), max({}), step({}), insRangeScheduler({})",
                groupInfo->traceid(), groupInfo->requestid(), groupInfo->ownerproxy(),
                groupInfo->groupopts().timeout(), groupInfo->groupopts().groupname(),
                groupInfo->groupopts().samerunninglifecycle(),
                groupInfo->insrange().min(), groupInfo->insrange().max(),
                groupInfo->insrange().step(), groupInfo->insrangescheduler());
    return Status::OK();
}

Status TransGroupRequest(const std::string &from, std::string &nodeID, std::shared_ptr<CreateRequests> req,
                         const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    if (req->requests_size() <= 0 || req->requests_size() > MAX_GROUP_INSTANCE_SIZE) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid instance num(" + std::to_string(req->requests_size()) +
            ") of group, should be range (0, " + std::to_string(MAX_GROUP_INSTANCE_SIZE) + "]");
    }
    groupInfo->set_groupid("group-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    groupInfo->set_status(static_cast<int32_t>(GroupState::SCHEDULING));
    groupInfo->set_requestid(req->requestid());
    groupInfo->set_traceid(req->traceid());
    groupInfo->set_parentid(from);
    groupInfo->set_ownerproxy(nodeID);
    groupInfo->set_rgroupname(req->groupopt().rgroupname());
    groupInfo->set_target(resources::CreateTarget::INSTANCE);
    *groupInfo->mutable_groupopts() = req->groupopt();
    int index = 0;
    bool insRangeFlag = false;
    int groupPriority;
    auto affinityHash =
        std::hash<std::string>()(req->requests(0).schedulingops().scheduleaffinity().ShortDebugString());
    for (CreateRequest createReq : *req->mutable_requests()) {
        if (auto it = createReq.createoptions().find("lifecycle");
            it != createReq.createoptions().end() && it->second == "detached") {
            return Status(StatusCode::ERR_PARAM_INVALID, "group schedule does not support detached instance.");
        }
        if (req->groupopt().grouppolicy() == common::GroupPolicy::StrictPack) {
            auto cur = std::hash<std::string>()(createReq.schedulingops().scheduleaffinity().ShortDebugString());
            if (cur != affinityHash) {
                return Status(StatusCode::ERR_PARAM_INVALID,
                              "group schedule with strict pack does not support different affinity.");
            }
        }
        auto scheduleReq = groupInfo->add_requests();
        if (createReq.mutable_schedulingops()->has_range()) {
            if (insRangeFlag) {
                return Status(StatusCode::ERR_PARAM_INVALID, "instance range does not support more than one");
            }
            insRangeFlag = true;
            auto status = CheckAndUpdateRangeInstanceSchedule(groupInfo, createReq, scheduleReq);
            if (!status.IsOk()) {
                return status;
            }
        }
        auto instanceInfo = scheduleReq->mutable_instance();
        if (index == 0) {
            groupPriority = instanceInfo->mutable_scheduleoption()->priority();
        } else {
            if (groupPriority != instanceInfo->mutable_scheduleoption()->priority()) {
                return Status(StatusCode::ERR_PARAM_INVALID, "instance priority does not support more than one");
            }
        }
        scheduleReq->set_traceid(req->traceid());
        scheduleReq->set_requestid(req->requestid() + "-" + std::to_string(index));
        createReq.set_requestid(scheduleReq->requestid());
        scheduleReq->set_scheduleround(0);
        runtime::CallRequest callRequest;
        SetCallReq(callRequest, createReq, from);
        *callRequest.mutable_createoptions() = *createReq.mutable_createoptions();
        // set InstanceInfo
        if (createReq.designatedinstanceid().empty()) {
            createReq.set_designatedinstanceid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        }
        createReq.mutable_schedulingops()->set_rgroupname(groupInfo->rgroupname());
        SetInstanceInfo(instanceInfo, createReq, callRequest, from);
        SetAffinityOpt(*instanceInfo, createReq, scheduleReq);
        if (auto score =
                GroupBinPackAffinity(groupInfo->groupid(), "", groupInfo->groupopts().grouppolicy(), *instanceInfo);
            score != 0) {
            auto ctx = (*scheduleReq->mutable_contexts())[LABEL_AFFINITY_PLUGIN].mutable_affinityctx();
            auto preOptimal = ctx->maxscore();
            ctx->set_maxscore(preOptimal + score);
        }

        instanceInfo->set_groupid(groupInfo->groupid());
        index++;
    }
    if (insRangeFlag && req->requests_size() != 1) {
        return Status(StatusCode::ERR_PARAM_INVALID, "instance range does not support more than one");
    }
    return Status::OK();
}

litebus::Future<Status> LocalGroupCtrlActor::ToGroupInstanceScheduling(const std::shared_ptr<GroupContext> &groupCtx)
{
    auto groupInfo = groupCtx->groupInfo;
    std::list<litebus::Future<Status>> futures;
    for (auto &request : groupCtx->requests) {
        futures.emplace_back(instanceCtrl_->ToScheduling(request));
    }
    return CollectStatus(futures, "collect instance to scheduling status");
}

litebus::Future<std::shared_ptr<CreateResponses>> LocalGroupCtrlActor::OnGroupCreateFailed(
    const Status &status, const std::shared_ptr<GroupContext> &groupCtx)
{
    auto groupInfo = groupCtx->groupInfo;
    auto resp = std::make_shared<CreateResponses>();
    YRLOG_ERROR("{}|{}| create group instance failed, id ({}), err: {}", groupInfo->traceid(), groupInfo->requestid(),
                groupInfo->groupid(), status.ToString());
    resp->set_code(Status::GetPosixErrorCode(status.StatusCode()));
    auto msg = "failed to create group, " + groupInfo->groupid() + ". caused by\n\t" + status.ToString();
    resp->set_message(msg);
    resp->set_groupid(groupInfo->groupid());
    DeleteGroupCtx(groupInfo->requestid());
    return resp;
}

litebus::Future<std::shared_ptr<CreateResponses>> LocalGroupCtrlActor::GroupSchedule(
    const std::string &from, const std::shared_ptr<CreateRequests> &req)
{
    auto resp = std::make_shared<CreateResponses>();
    if (!isStarted) {
        YRLOG_INFO("{}|{}| local is recovering please try again later.", req->traceid(), req->requestid());
        resp->set_code(common::ErrorCode::ERR_INNER_COMMUNICATION);
        resp->set_message("local is recovering please try again later");
        return resp;
    }
    if (IsDuplicateGroup(from, req)) {
        auto ctx = GetGroupCtx(req->requestid());
        return ctx->persistingPromise.GetFuture();
    }
    auto groupInfo = std::make_shared<messages::GroupInfo>();
    if (auto status = TransGroupRequest(from, nodeID_, req, groupInfo); status.IsError()) {
        YRLOG_INFO("{}|{}| group request invalid, {}", req->traceid(), req->requestid(), status.ToString());
        resp->set_code(common::ErrorCode::ERR_PARAM_INVALID);
        resp->set_message(status.GetMessage());
        return resp;
    }
    auto groupCtx = NewGroupCtx(groupInfo);
    YRLOG_INFO("{}|{}|received group schedule request, id ({}) instance num {}", req->traceid(), req->requestid(),
               groupInfo->groupid(), req->requests_size());
    ASSERT_IF_NULL(groupOperator_);
    ASSERT_IF_NULL(scheduler_);
    auto future = ToGroupInstanceScheduling(groupCtx).Then(
        [scheduler(scheduler_), groupOperator(groupOperator_), groupCtx, aid(GetAID()), resp](const Status &status)
            -> litebus::Future<std::shared_ptr<CreateResponses>> {
            if (status.IsError()) {
                return litebus::Async(aid, &LocalGroupCtrlActor::OnGroupCreateFailed, status, groupCtx);
            }
            groupCtx->UpdateInfo();
            // currently only put group info to etcd, duplicate groupID is not considered.
            return groupOperator->TxnGroupInstances(groupCtx->groupInfo)
                .Then(litebus::Defer(aid, &LocalGroupCtrlActor::DoLocalGroupSchedule, std::placeholders::_1,
                                     scheduler, groupCtx, resp));
        });
    groupCtx->persistingPromise.Associate(future);
    return future;
}

litebus::Future<std::shared_ptr<CreateResponses>> LocalGroupCtrlActor::DoLocalGroupSchedule(const Status &status,
    std::shared_ptr<schedule_decision::Scheduler> scheduler, std::shared_ptr<GroupContext> groupCtx,
    std::shared_ptr<CreateResponses> resp)
{
    if (status.IsError()) {
        return litebus::Async(GetAID(), &LocalGroupCtrlActor::OnGroupCreateFailed, status, groupCtx);
    }
    auto groupInfo = groupCtx->groupInfo;
    resp->set_code(common::ErrorCode::ERR_NONE);
    resp->set_groupid(groupInfo->groupid());
    auto spec = BuildGroupSpec(groupCtx);
    if (groupCtx->insRangeScheduler) {
        YRLOG_DEBUG("{}|{} start rang instance schedule, groupID({})",
                    groupCtx->groupInfo->traceid(), groupCtx->groupInfo->requestid(), groupInfo->groupid());
        // the maximum number of local scheduling requests is required.
        // if the local scheduling requests do not meet the requirements,
        // the request needs to be forwarded to the upper layer.
        spec->rangeOpt.min = spec->rangeOpt.max;
        return scheduler->GroupScheduleDecision(spec)
            .Then(litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnLocalGroupSchedule,
                                 std::placeholders::_1, groupCtx, resp));
    }
    for (auto request : groupInfo->requests()) {
        *resp->add_instanceids() = request.instance().instanceid();
    }
    // async to schedule, Early return groupID & instanceID
    (void)scheduler->GroupScheduleDecision(spec)
        .OnComplete(litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnLocalGroupSchedule,
                                   std::placeholders::_1, groupCtx, resp));
    return resp;
}

void LocalGroupCtrlActor::NotifyGroupResult(const Status &status, const std::string &to,
                                            const std::shared_ptr<GroupContext> &groupCtx)
{
    auto groupInfo = groupCtx->groupInfo;
    (void)clientManager_->GetControlInterfacePosixClient(to).Then(
        [to, groupInfo, status](const std::shared_ptr<ControlInterfacePosixClient> &client) {
            if (client == nullptr) {
                YRLOG_WARN("{}|{}|not found client of the instance({}), which is creator of group({})",
                           groupInfo->traceid(), groupInfo->requestid(), to, groupInfo->groupid());
                return Status::OK();
            }
            YRLOG_INFO("{}|{}|notify group({}) {} to instance({})", groupInfo->traceid(), groupInfo->requestid(),
                       groupInfo->groupid(), status.IsError() ? "failed" : "successful", to);
            runtime::NotifyRequest notify;
            notify.set_code(Status::GetPosixErrorCode(status.StatusCode()));
            notify.set_message(status.GetMessage());
            notify.set_requestid(groupInfo->requestid());
            // notify failed need to retry or report warning
            client->NotifyResult(std::move(notify));
            return Status::OK();
        });
}

void LocalGroupCtrlActor::OnGroupFailed(const Status &status, const std::shared_ptr<GroupContext> &groupCtx)
{
    if (status.IsOk()) {
        return;
    }
    auto groupInfo = groupCtx->groupInfo;
    YRLOG_ERROR("{}|{}|failed to schedule instance for group {}, {}", groupInfo->traceid(), groupInfo->requestid(),
                groupInfo->groupid(), status.ToString());
    ASSERT_IF_NULL(instanceCtrl_);
    for (auto request : groupCtx->requests) {
        instanceCtrl_->ForceDeleteInstance(request->instance().instanceid());
    }
    groupInfo->set_status(static_cast<int32_t>(GroupState::FAILED));
    groupInfo->set_message(status.MultipleErr() ? status.GetMessage() : status.RawMessage());
    ASSERT_IF_NULL(groupOperator_);
    (void)groupOperator_->TxnGroupInstances(groupInfo).Then(
        [status, aid(GetAID()), groupCtx](const Status &txnStatus) -> litebus::Future<Status> {
            auto groupInfo = groupCtx->groupInfo;
            std::string errMsg = groupInfo->message() +
                                 "\n(please kill the group " + groupInfo->groupid() +
                                 " to avoid FAILED Group information to be left over.)";
            auto code = status.StatusCode();
            if (txnStatus.IsError()) {
                errMsg = errMsg +
                         "\nduring handler above err, the following error occurred while put group "
                         "failed status to etcd:\n\t" +
                         txnStatus.ToString();
                code = txnStatus.StatusCode();
            }
            litebus::Async(aid, &LocalGroupCtrlActor::NotifyGroupResult, Status(code, errMsg), groupInfo->parentid(),
                           groupCtx);
            return {};
        });
}

void LocalGroupCtrlActor::OnGroupSuccessful(const std::shared_ptr<GroupContext> &groupCtx)
{
    auto groupInfo = groupCtx->groupInfo;
    YRLOG_INFO("{}|{}|succeessful to schedule instance for group {}", groupInfo->traceid(), groupInfo->requestid(),
                groupInfo->groupid());
    groupInfo->set_status(static_cast<int32_t>(GroupState::RUNNING));
    ASSERT_IF_NULL(groupOperator_);
    (void)groupOperator_->TxnGroupInstances(groupInfo).Then([aid(GetAID()), groupCtx](
                                                                const Status &txnStatus) -> litebus::Future<Status> {
        if (txnStatus.IsError()) {
            std::string errMsg =
                "the following error occurred while put group "
                "running status to etcd:\n\t" +
                txnStatus.ToString();
            litebus::Async(aid, &LocalGroupCtrlActor::OnGroupFailed, Status(txnStatus.StatusCode(), errMsg), groupCtx);
            return {};
        }
        if (groupCtx->persistingPromise.GetFuture().IsInit()) {
            // Ensure that the notify message is returned to the caller later than the response message.
            groupCtx->persistingPromise.GetFuture().OnComplete([aid, groupCtx]() {
                litebus::Async(aid, &LocalGroupCtrlActor::NotifyGroupResult, Status::OK(),
                               groupCtx->groupInfo->parentid(), groupCtx);
            });
            return {};
        }
        litebus::Async(aid, &LocalGroupCtrlActor::NotifyGroupResult, Status::OK(), groupCtx->groupInfo->parentid(),
                       groupCtx);
        return {};
    });
}

litebus::Future<std::shared_ptr<CreateResponses>> LocalGroupCtrlActor::ForwardGroupSchedule(
    const std::shared_ptr<GroupContext> &groupCtx, std::shared_ptr<CreateResponses> rsp)
{
    ASSERT_IF_NULL(localSchedSrv_);
    auto groupInfo = groupCtx->groupInfo;
    return localSchedSrv_->ForwardGroupSchedule(groupInfo).Then(litebus::Defer(GetAID(),
        &LocalGroupCtrlActor::ForwardGroupScheduleDone, std::placeholders::_1, groupCtx, rsp));
}

litebus::Future<std::shared_ptr<CreateResponses>> LocalGroupCtrlActor::ForwardGroupScheduleDone(
    const messages::GroupResponse &groupRsp, const std::shared_ptr<GroupContext> &groupCtx,
    std::shared_ptr<CreateResponses> rsp)
{
    auto groupInfo = groupCtx->groupInfo;
    if (groupRsp.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        auto status = Status(static_cast<StatusCode>(groupRsp.code()), groupRsp.message());
        YRLOG_ERROR("{}|{}|failed to forward schedule group {}, error: {}", groupInfo->traceid(),
                    groupInfo->requestid(), groupInfo->groupid(), status.ToString());
        litebus::Async(GetAID(), &LocalGroupCtrlActor::OnGroupFailed, status, groupCtx);
        rsp->set_code(Status::GetPosixErrorCode(groupRsp.code()));
        rsp->set_message(groupRsp.message());
        return rsp;
    }
    YRLOG_INFO("{}|{}|success schedule group {}", groupInfo->traceid(), groupInfo->requestid(),
               groupInfo->groupid());
    if (groupCtx->insRangeScheduler) {
        YRLOG_DEBUG("{}|{} it's range instance schedule success, update range instance num: {}",
                    groupInfo->traceid(), groupInfo->requestid(), groupRsp.rangesuccessnum());
        int ctxRequestSize = groupCtx->requests.size();
        if (groupRsp.rangesuccessnum() > ctxRequestSize) {
            auto status = Status(static_cast<StatusCode>(common::ErrorCode::ERR_PARAM_INVALID),
                                 "range scheduler return bigger range success instance num");
            YRLOG_ERROR("{}|{}|range scheduler return bigger range success instance num({}), request size({})",
                        groupInfo->traceid(), groupInfo->requestid(), groupRsp.rangesuccessnum(),
                        ctxRequestSize);
            litebus::Async(GetAID(), &LocalGroupCtrlActor::OnGroupFailed, status, groupCtx);
            rsp->set_code(common::ErrorCode::ERR_PARAM_INVALID);
            rsp->set_message("range scheduler return bigger range success instance num");
            return rsp;
        }
        for (size_t start = 0;
             start < static_cast<size_t>(groupRsp.rangesuccessnum()) && start < groupCtx->requests.size(); start++) {
            auto id = groupCtx->requests[start]->instance().instanceid();
            YRLOG_DEBUG("{}|{} range schedule success instanceID({})", groupInfo->traceid(), groupInfo->requestid(),
                        id);
            *rsp->add_instanceids() = id;
        }
        for (int start = groupRsp.rangesuccessnum(); start < ctxRequestSize; start++) {
            auto req = groupCtx->requests.back();
            (void)reserveResult_.erase(req->requestid());
            ASSERT_IF_NULL(instanceCtrl_);
            instanceCtrl_->DeleteSchedulingInstance(req->instance().instanceid(), req->requestid());
            groupCtx->requests.pop_back();
            // During range scheduling, callback function is registered based on the value of max.
            // In the case of order-preserving, set value into the promise of the unscheduled instance.
            groupCtx->groupInsPromise[start]->SetValue(Status::OK());
        }
        groupCtx->insRangeRequest->mutable_rangeopts()->set_currangeinstancenum(groupRsp.rangesuccessnum());
        groupCtx->UpdateInfo();
    }
    groupCtxs_[groupCtx->groupInfo->requestid()] = groupCtx;
    return rsp;
}

void LocalGroupCtrlActor::CollectInstancesReady(const std::shared_ptr<GroupContext> &groupCtx)
{
    std::list<litebus::Future<Status>> futures;
    auto groupInfo = groupCtx->groupInfo;
    groupCtx->groupInsPromise.clear();
    for (auto &request : groupCtx->requests) {
        auto promise = std::make_shared<litebus::Promise<Status>>();
        auto instanceID = request->instance().instanceid();
        auto callback = [promise, groupInfo, request, instanceID](const Status &status) -> litebus::Future<Status> {
            if (!promise->GetFuture().IsInit()) {
                return Status::OK();
            }
            YRLOG_INFO("{}|{}| instance({}) of group({}) is {}", groupInfo->traceid(), groupInfo->requestid(),
                       instanceID, groupInfo->groupid(), status.IsOk() ? "successful" : "failed. " + status.ToString());
            promise->SetValue(status);
            return Status::OK();
        };

        instanceCtrl_->RegisterReadyCallback(request->instance().instanceid(), request, callback);
        futures.emplace_back(promise->GetFuture());
        groupCtx->groupInsPromise.emplace_back(promise);
    }

    CollectStatus(futures, "collect instance ready status", StatusCode::ERR_USER_FUNCTION_EXCEPTION,
                  StatusCode::ERR_GROUP_SCHEDULE_FAILED)
        .OnComplete([groupCtx, aid(GetAID())](const litebus::Future<Status> &future) {
            ASSERT_FS(future.IsOK());
            auto status = future.Get();
            if (status.IsError()) {
                litebus::Async(aid, &LocalGroupCtrlActor::OnGroupFailed, status, groupCtx);
                return;
            }
            litebus::Async(aid, &LocalGroupCtrlActor::OnGroupSuccessful, groupCtx);
        });
}

Status GenerateDeviceInfo(const std::shared_ptr<ResourceUnit> &view, const schedule_decision::ScheduleResult &result,
                          const std::shared_ptr<messages::ScheduleRequest> &req,
                          std::set<common::HeteroDeviceInfo, HeteroDeviceCompare> &deviceInfos)
{
    auto fragment = view->mutable_fragment();
    auto &instance = req->instance();
    auto instanceId = instance.instanceid();
    auto groupid = instance.groupid();
    auto resultId = result.id;
    auto cardType = result.heteroProductName;
    auto fragmentIter = fragment->find(resultId);
    if (fragmentIter == fragment->end()) {
        YRLOG_WARN("{}|{} resource view does not have a resource unit with ID {}, group id: {}, instanceId: {}",
                   req->traceid(), req->requestid(), resultId, groupid, instanceId);
        return Status(ERR_INNER_SYSTEM_ERROR, "resource view does not have resource unit");
    }
    auto &unit = fragmentIter->second;
    auto &resource = unit.capacity().resources();
    if (!HasHeteroResourceNumeric(unit, cardType, resource_view::IDS_KEY) ||
        resource.at(cardType).vectors().values().at(resource_view::IDS_KEY).vectors().empty()) {
        YRLOG_WARN("{}|{} device id is empty in resource unit with ID {}, group id: {}, instanId: {}",
                   req->traceid(), req->requestid(), resultId, groupid, instanceId);
        return Status(ERR_INNER_SYSTEM_ERROR, "device id is empty");
    }
    if (result.realIDs.empty()) {
        YRLOG_WARN("{}|{} realIDs of device is empty in result with ID {}, group id: {}, instanId: {}",
                   req->traceid(), req->requestid(), resultId, groupid, instanceId);
        return Status(ERR_INNER_SYSTEM_ERROR, "realIDs is empty");
    }
    auto &deviceIds = resource.at(cardType).vectors().values().at(resource_view::IDS_KEY).vectors().begin()->second;
    auto maxRealID = *std::max_element(result.realIDs.begin(), result.realIDs.end());
    if (maxRealID + 1 > deviceIds.values_size()) {
        YRLOG_WARN("{}|{} realID is invalid,  max realID({}) > size({}) of deviceId, group id: {}, instanceId: {}",
                   req->traceid(), req->requestid(), maxRealID+1,
                   deviceIds.values_size(), groupid, instanceId);
        return Status(ERR_INNER_SYSTEM_ERROR, "realID is invalid");
    }
    auto deviceIps = GetDeviceIps(unit, cardType);
    for (auto realID : result.realIDs) {
        common::HeteroDeviceInfo device;
        device.set_deviceid(deviceIds.values().at(realID));
        device.set_deviceip(deviceIps[realID]);
        deviceInfos.insert(device);
    }
    return Status::OK();
}

Status GenerateDeviceInfos(const std::shared_ptr<ResourceUnit> &view,
                           const schedule_decision::GroupScheduleResult &scheduleresult,
                           const std::shared_ptr<GroupContext> &groupCtx,
                           std::set<common::HeteroDeviceInfo, HeteroDeviceCompare> &deviceInfos,
                           std::unordered_map<std::string, std::vector<std::string>> &insDeviceIpMap)
{
    for (size_t i = 0; i < scheduleresult.results.size(); i++) {
        auto result = scheduleresult.results[i];
        auto scheduleReq = groupCtx->requests[i];
        auto &instance = scheduleReq->instance();
        auto instanceId = instance.instanceid();

        if (auto status = GenerateDeviceInfo(view, result, scheduleReq, deviceInfos); status.IsError()) {
            return status;
        }
        for (auto &device : deviceInfos) {
            insDeviceIpMap[instanceId].push_back(device.deviceip());
        }
    }
    return Status::OK();
}

Status GenerateFunctionGroupRunningInfo(const std::shared_ptr<resource_view::ResourceUnit> &view,
    const std::shared_ptr<GroupContext> &groupCtx, const schedule_decision::GroupScheduleResult &result,
    common::FunctionGroupRunningInfo &functionGroupRunningInfo,
    std::unordered_map<std::string, int> &insRankIdMap)
{
    auto &groupInfo = groupCtx->groupInfo;

    if (result.results.empty()) {
        YRLOG_WARN("{}|{} the group({}) schedule result is empty",
                   groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());
        return Status(ERR_INNER_SYSTEM_ERROR, "schedule result is empty");
    }

    if (groupCtx->requests.empty()) {
        YRLOG_WARN("{}|{} the group({}) requests is empty",
                   groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());
        return Status(ERR_INNER_SYSTEM_ERROR, "schedule requests is empty");
    }

    std::set<common::HeteroDeviceInfo, HeteroDeviceCompare> deviceInfos{};
    // key:instanceId  value:deviceIp list
    std::unordered_map<std::string, std::vector<std::string>> insDeviceIpMap{};
    // key:deviceIp  value:device rankId
    std::unordered_map<std::string, int> deviceIP2DeviceRankIdMap{};

    common::ServerInfo serverInfo;
    serverInfo.set_serverid(view->id());
    if (auto status = GenerateDeviceInfos(view, result, groupCtx, deviceInfos, insDeviceIpMap); status.IsError()) {
        return status;
    }

    int rankId = 0;
    for (auto device : deviceInfos) {
        device.set_rankid(rankId);
        deviceIP2DeviceRankIdMap[device.deviceip()] = rankId;
        rankId += 1;
        (*serverInfo.add_devices()) = std::move(device);
    }
    GenerateInsRankId(insDeviceIpMap, deviceIP2DeviceRankIdMap, insRankIdMap);

    (*functionGroupRunningInfo.add_serverlist()) = std::move(serverInfo);
    functionGroupRunningInfo.set_worldsize(result.results.size());
    return Status::OK();
}

litebus::Future<std::shared_ptr<CreateResponses>> LocalGroupCtrlActor::OnLocalGroupSchedule(
    const litebus::Future<schedule_decision::GroupScheduleResult> &future,
    const std::shared_ptr<GroupContext> &groupCtx, std::shared_ptr<CreateResponses> rsp)
{
    ASSERT_FS(future.IsOK());
    auto groupInfo = groupCtx->groupInfo;
    // the result returned by the scheduler follows the all-or-nothing principle.
    auto result = future.Get();
    if (result.code != 0) {
        YRLOG_ERROR("{}|{}|failed to schedule instance,  group id: {}, range schedule: {}, err: {}",
            groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid(),
            groupCtx->insRangeScheduler, result.reason);
        return HandleLocalGroupScheduleError(groupCtx, rsp);
    }
    ASSERT_IF_NULL(instanceCtrl_);
    std::list<litebus::Future<Status>> futures;
    for (size_t i = 0; i < result.results.size(); i++) {
        if (result.results[i].allocatedPromise == nullptr) {
            futures.emplace_back(Status::OK());
            continue;
        }
        futures.emplace_back(result.results[i].allocatedPromise->GetFuture());
    }
    CollectStatus(futures, "wait for allocated instance check")
        .OnComplete(litebus::Defer(GetAID(), &LocalGroupCtrlActor::HandleAllocateInsComplete, std::placeholders::_1,
                                   groupCtx, result, rsp));
    for (auto request : groupCtx->requests) {
        *rsp->add_instanceids() = request->instance().instanceid();
    }
    return rsp;
}

litebus::Future<std::shared_ptr<CreateResponses>> LocalGroupCtrlActor::HandleLocalGroupScheduleError(
    const std::shared_ptr<GroupContext> &groupCtx, std::shared_ptr<CreateResponses> rsp)
{
    CollectInstancesReady(groupCtx);
    if (groupCtx->insRangeScheduler) {
        return ForwardGroupSchedule(groupCtx, rsp);
    }
    std::shared_ptr<CreateResponses> createRes = std::make_shared<CreateResponses>();
    ForwardGroupSchedule(groupCtx, rsp);
    return createRes;
}

void LocalGroupCtrlActor::HandleAllocateInsComplete(const litebus::Future<Status> future,
                                                    const std::shared_ptr<GroupContext> &groupCtx,
                                                    const schedule_decision::GroupScheduleResult result,
                                                    std::shared_ptr<CreateResponses> rsp)
{
    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    if (status.IsOk()) {
        HandleAllocateInsSuccess(groupCtx, result, rsp);
        return;
    }
    auto &groupInfo = groupCtx->groupInfo;
    YRLOG_WARN("{}|{}|failed to allocate instance, group id: {}, retry to Group Schedule Decision",
               groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());
    HandleAllocateInsError(groupCtx, rsp);
}

void LocalGroupCtrlActor::HandleAllocateInsSuccess(const std::shared_ptr<GroupContext> &groupCtx,
                                                   const schedule_decision::GroupScheduleResult &result,
                                                   std::shared_ptr<CreateResponses> rsp)
{
    litebus::Async(GetAID(), &LocalGroupCtrlActor::CollectInstancesReady, groupCtx);

    if (!HasHeterogeneousRequest(groupCtx->requests)) {
        for (size_t i = 0; i < result.results.size(); i++) {
            // the smart pointer is used for compatibility.
            // the creating result will be returned by ready promise
            instanceCtrl_->ToCreating(groupCtx->requests[i], result.results[i]);
        }
        return;
    }

    auto &groupInfo = groupCtx->groupInfo;
    YRLOG_INFO("{}|{} the group({}) requests require heterogeneous resources",
               groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());

    ASSERT_IF_NULL(resourceViewMgr_);
    auto type = resource_view::GetResourceType(*groupInfo);
    (void)resourceViewMgr_->GetInf(type)->GetResourceViewCopy().Then(
        [aid(GetAID()), instanceCtrl(instanceCtrl_), groupCtx, result,
         rsp](const std::shared_ptr<resource_view::ResourceUnit> &view) -> litebus::Future<Status> {
            ASSERT_IF_NULL(view);
            common::FunctionGroupRunningInfo functionGroupRunningInfo;
            std::unordered_map<std::string, int> insRankIdMap{};
            if (auto status =
                    GenerateFunctionGroupRunningInfo(view, groupCtx, result, functionGroupRunningInfo, insRankIdMap);
                status.IsError()) {
                auto &groupInfo = groupCtx->groupInfo;
                YRLOG_WARN("{}|{} failed to generate functionGroupRunningInfo, need reschedule, group id: {}",
                           groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());
                litebus::Async(aid, &LocalGroupCtrlActor::HandleAllocateInsError, groupCtx, rsp);
                return {};
            }
            auto &groupInfo = groupCtx->groupInfo;
            YRLOG_DEBUG("{}|{} group id: {}, functionGroupRunningInfo: {}", groupInfo->traceid(),
                        groupInfo->requestid(), groupInfo->groupid(), functionGroupRunningInfo.DebugString());
            for (size_t i = 0; i < result.results.size(); i++) {
                auto &scheduleRequest = groupCtx->requests[i];
                auto instanceId = scheduleRequest->instance().instanceid();
                functionGroupRunningInfo.set_instancerankid(insRankIdMap[instanceId]);
                functionGroupRunningInfo.set_devicename(result.results[i].heteroProductName);
                std::string groupRunningInfoStr;
                if (!google::protobuf::util::MessageToJsonString(functionGroupRunningInfo, &groupRunningInfoStr).ok()) {
                    YRLOG_WARN("{}|{} failed to trans functionGroupRunningInfo to json, group id: {}",
                               groupInfo->traceid(), groupInfo->requestid(), groupInfo->groupid());
                    return Status(StatusCode::ERR_INNER_SYSTEM_ERROR,
                                  "failed to trans function Group RunningInfo to json string");
                }
                (*scheduleRequest->mutable_instance()->mutable_createoptions())["FUNCTION_GROUP_RUNNING_INFO"] =
                    groupRunningInfoStr;
                instanceCtrl->ToCreating(scheduleRequest, result.results[i]);
            }
            return Status::OK();
        });
}

void LocalGroupCtrlActor::HandleAllocateInsError(const std::shared_ptr<GroupContext> &groupCtx,
                                                 std::shared_ptr<CreateResponses> rsp)
{
    auto type = resource_view::GetResourceType(*groupCtx->groupInfo);
    for (auto request : groupCtx->requests) {
        resourceViewMgr_->GetInf(type)->DeleteInstances({ request->instance().instanceid() }, true);
    }
    auto spec = BuildGroupSpec(groupCtx);
    // async to schedule, Early return groupID & instanceID
    scheduler_->GroupScheduleDecision(spec).OnComplete(
        litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnLocalGroupSchedule, std::placeholders::_1, groupCtx, rsp));
}

void LocalGroupCtrlActor::Reserves(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!CheckIsReady(name)) {
        return;
    }
    auto req = std::make_shared<messages::Reserves>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for reserve resource. from({}) msg({}), ignore it", std::string(from),
                    msg);
        return;
    }
    YRLOG_INFO("{}|{}|received request of batch reserve instance({}) resource, groupID({})", req->traceid(),
               req->requestid(), fmt::join(req->instanceids().begin(), req->instanceids().end(), ","), req->groupid());
    auto resp = std::make_shared<messages::OnReserves>();
    resp->set_requestid(req->requestid());
    resp->set_traceid(req->traceid());
    std::list<litebus::Future<std::shared_ptr<messages::ScheduleResponse>>> futures;
    for (auto r : req->reserves()) {
        auto scheReq = std::make_shared<messages::ScheduleRequest>(r);
        futures.emplace_back(DoReserve(scheReq));
    }
    (void)litebus::Collect(futures).OnComplete(
        [resp, from,
         aid(GetAID())](const litebus::Future<std::list<std::shared_ptr<messages::ScheduleResponse>>> &future) {
            ASSERT_FS(future.IsOK());
            auto rsps = future.Get();
            for (auto rsp : rsps) {
                *resp->add_responses() = std::move(*rsp);
            }
            litebus::Async(aid, &LocalGroupCtrlActor::CollectResourceOnReserve, from, resp);
        });
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> LocalGroupCtrlActor::DoReserve(
        std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto resp = std::make_shared<messages::ScheduleResponse>();
    resp->set_requestid(req->requestid());
    resp->set_instanceid(req->instance().instanceid());
    *resp->mutable_contexts() = req->contexts();
    if (reserveResult_.find(req->requestid()) != reserveResult_.end()) {
        YRLOG_INFO("{}|{}|request of reserve instance({}) resource, groupID({}) already reserved to {}", req->traceid(),
                   req->requestid(), req->instance().instanceid(), req->instance().groupid(),
                   reserveResult_[req->requestid()].result.id);
        litebus::TimerTools::Cancel(reserveResult_[req->requestid()].reserveTimeout);
        // reset timer
        reserveResult_[req->requestid()].reserveTimeout =
            litebus::AsyncAfter(reserveToBindTimeoutMs_, GetAID(), &LocalGroupCtrlActor::TimeoutToBind, req);
        return resp;
    }
    ASSERT_IF_NULL(scheduler_);
    return scheduler_->ScheduleDecision(req).Then(
        litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnReserve, std::placeholders::_1, req, resp));
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> LocalGroupCtrlActor::SetDeviceInfoError(
    const std::shared_ptr<messages::ScheduleRequest> &req, const std::shared_ptr<messages::ScheduleResponse> &resp)
{
    auto type = resource_view::GetResourceType(req->instance());
    resourceViewMgr_->GetInf(type)->DeleteInstances({ req->instance().instanceid() }, true);
    (void)reserveResult_.erase(req->requestid());
    return scheduler_->ScheduleDecision(req).Then(
        litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnReserve, std::placeholders::_1, req, resp));
}

litebus::Future<Status> LocalGroupCtrlActor::SetDeviceInfoToHeteroScheduleResp(
    const schedule_decision::ScheduleResult &result, const std::shared_ptr<messages::ScheduleRequest> &req,
    const std::shared_ptr<messages::ScheduleResponse> &resp)
{
    messages::ScheduleResult scheduleResult;
    scheduleResult.set_nodeid(nodeID_);
    *(resp->mutable_scheduleresult()) = scheduleResult;
    auto type = resource_view::GetResourceType(req->instance());
    return resourceViewMgr_->GetInf(type)->GetResourceViewCopy().Then([aid(GetAID()), result, req, resp](
            const std::shared_ptr<resource_view::ResourceUnit> &view) -> litebus::Future<Status> {
        ASSERT_IF_NULL(view);
        std::set<common::HeteroDeviceInfo, HeteroDeviceCompare> deviceInfos{};
        if (auto status = GenerateDeviceInfo(view, result, req, deviceInfos); status.IsError()) {
            return status;
        }
        for (auto &device : deviceInfos) {
            (*resp->mutable_scheduleresult()->add_devices()) = std::move(device);
        }
        return Status::OK();
    });
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> LocalGroupCtrlActor::OnSuccessfulReserve(
    const schedule_decision::ScheduleResult &result,
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const std::shared_ptr<messages::ScheduleResponse> &resp)
{
    YRLOG_INFO("{}|{}|success to reserve instance({}), groupID({}), selected agent ({})", req->traceid(),
               req->requestid(), req->instance().instanceid(), req->instance().groupid(), result.id);
    auto reservedContext =
        ReservedContext{ .result = result,
                         .reserveTimeout = litebus::AsyncAfter(reserveToBindTimeoutMs_, GetAID(),
                                                               &LocalGroupCtrlActor::TimeoutToBind, req) };
    reservedContext.result.code = static_cast<int32_t>(StatusCode::SUCCESS);
    reserveResult_[req->requestid()] = reservedContext;
    (*resp->mutable_contexts())[GROUP_SCHEDULE_CONTEXT].mutable_groupschedctx()->set_reserved(result.id);

    if (!IsHeterogeneousRequest(req)) {
        return resp;
    }

    return SetDeviceInfoToHeteroScheduleResp(result, req, resp).Then([aid(GetAID()), req, resp, result](
        const litebus::Future<Status> &future) -> litebus::Future<std::shared_ptr<messages::ScheduleResponse>> {
        ASSERT_FS(future.IsOK());
        auto status = future.Get();
        if (status.IsError()) {
            YRLOG_ERROR("{}|{}|failed to set deviceInfo to schedule response,"
                        "instance({}), groupID({}), selected agent ({}). retry to reserve",
                        req->traceid(), req->requestid(), req->instance().instanceid(), req->instance().groupid(),
                        result.id);
            return litebus::Async(aid, &LocalGroupCtrlActor::SetDeviceInfoError, req, resp);
        }
        return resp;
    });
}

void LocalGroupCtrlActor::CollectResourceOnReserve(const litebus::AID &to,
                                                   const std::shared_ptr<messages::OnReserves> &resp)
{
    ASSERT_IF_NULL(resourceViewMgr_);
    (void)resourceViewMgr_->GetChanges().Then(
        [resp, to, aid(GetAID())](const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>> &changes)
            -> litebus::Future<std::shared_ptr<messages::ScheduleResponse>> {
            for (const auto &[type, change] : changes) {
                (*resp->mutable_updateresources())[static_cast<int32_t>(type)] = std::move(*change);
            }
            litebus::Async(aid, &LocalGroupCtrlActor::SendMsg, to, "OnReserves", resp->SerializeAsString());
            return {};
        });
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> LocalGroupCtrlActor::OnReserve(
    const litebus::Future<schedule_decision::ScheduleResult> &future,
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const std::shared_ptr<messages::ScheduleResponse> &resp)
{
    ASSERT_FS(future.IsOK());
    auto result = future.Get();
    // Schedule would change context which need to be updated
    *resp->mutable_contexts() = req->contexts();
    if (result.code != static_cast<int32_t>(StatusCode::SUCCESS) &&
        result.code != static_cast<int32_t>(StatusCode::INSTANCE_ALLOCATED)) {
        YRLOG_WARN("{}|{}|failed to reserve instance({}), groupID({}) code: {} msg:{}", req->traceid(),
                   req->requestid(), req->instance().instanceid(), req->instance().groupid(), result.code,
                   result.reason);
        resp->set_code(result.code);
        resp->set_message(result.reason);
        return resp;
    }
    if (result.allocatedPromise != nullptr) {
        return result.allocatedPromise->GetFuture().Then([scheduler(scheduler_), aid(GetAID()), req, resp,
                                                         result](const litebus::Future<Status> &future) {
            ASSERT_FS(future.IsOK());
            auto status = future.Get();
            if (status.IsError()) {
                YRLOG_ERROR("{}|{}|failed to allocate instance({}), groupID({}), selected agent ({}). retry to reserve",
                            req->traceid(), req->requestid(), req->instance().instanceid(), req->instance().groupid(),
                            result.id);
                return scheduler->ScheduleDecision(req).Then(
                    litebus::Defer(aid, &LocalGroupCtrlActor::OnReserve, std::placeholders::_1, req, resp));
            }
            return litebus::Async(aid, &LocalGroupCtrlActor::OnSuccessfulReserve, result, req, resp);
        });
    }
    return OnSuccessfulReserve(result, req, resp);
}

void LocalGroupCtrlActor::SendMsg(const litebus::AID &to, const std::string &name, const std::string &msg)
{
    (void)Send(to, std::string(name), std::string(msg));
}

void LocalGroupCtrlActor::UnReserve(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!CheckIsReady(name)) {
        return;
    }
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for rollback reserve resource. from({}) msg({}), ignore it",
                    std::string(from), msg);
        return;
    }
    YRLOG_INFO("{}|{}|received request of rollback reserve instance({}) resource, groupID({})", req->traceid(),
               req->requestid(), req->instance().instanceid(), req->instance().groupid());
    ASSERT_IF_NULL(resourceViewMgr_);
    auto type = resource_view::GetResourceType(req->instance());
    resourceViewMgr_->GetInf(type)->DeleteInstances({ req->instance().instanceid() }, true);
    (void)reserveResult_.erase(req->requestid());
    auto resp = std::make_shared<messages::GroupResponse>();
    resp->set_requestid(req->requestid());
    resp->set_traceid(req->traceid());
    (void)resourceViewMgr_->GetChanges().Then(
        [resp, from,
         aid(GetAID())](const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>> &changes)
            -> litebus::Future<Status> {
            for (const auto &[type, change] : changes) {
                ASSERT_IF_NULL(change);
                (*resp->mutable_updateresources())[static_cast<int32_t>(type)] = std::move(*change);
            }
            litebus::Async(aid, &LocalGroupCtrlActor::SendMsg, from, "OnUnReserve", resp->SerializeAsString());
            return {};
        });
}

void LocalGroupCtrlActor::Bind(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!CheckIsReady(name)) {
        return;
    }
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for bind instance. from({}) msg({}), ignore it", std::string(from), msg);
        return;
    }
    auto resp = std::make_shared<messages::GroupResponse>();
    resp->set_requestid(req->requestid());
    resp->set_traceid(req->traceid());
    if (reserveResult_.find(req->requestid()) == reserveResult_.end()) {
        instanceCtrl_->IsInstanceRunning(req->instance().instanceid())
            .OnComplete(litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnCheckInstanceRunning, std::placeholders::_1,
                                       from, req, resp));
        return;
    }
    if (bindingReqs_.find(req->requestid()) != bindingReqs_.end()) {
        YRLOG_WARN("{}|{}|ignore bind request, because of instance({}) is binding, groupID({})", req->traceid(),
                   req->requestid(), req->instance().instanceid(), req->instance().groupid());
        return;
    }
    bindingReqs_.insert(req->requestid());
    auto result = reserveResult_[req->requestid()].result;
    litebus::TimerTools::Cancel(reserveResult_[req->requestid()].reserveTimeout);
    YRLOG_INFO("{}|{}|received request to bind instance({}) of groupID({}), deploy to {}", req->traceid(),
               req->requestid(), req->instance().instanceid(), req->instance().groupid(), result.id);
    ASSERT_IF_NULL(instanceCtrl_);
    (void)instanceCtrl_->ToCreating(req, result)
        .OnComplete(litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnBind, from, std::placeholders::_1, req, resp));
}

void LocalGroupCtrlActor::OnCheckInstanceRunning(const litebus::Future<bool> &isRunning, const litebus::AID &from,
                                                 const std::shared_ptr<messages::ScheduleRequest> &req,
                                                 const std::shared_ptr<messages::GroupResponse> &resp)
{
    if (isRunning.IsOK() && isRunning.Get()) {
        YRLOG_INFO("{}|{}|received request to bind instance({}), instance is already running", req->traceid(),
                   req->requestid(), req->instance().instanceid());
        OnBind(from, Status::OK(), req, resp);
        return;
    }

    YRLOG_INFO("{}|{}|failed to bind instance, because of not found instance({}) reserve result, groupID({})",
               req->traceid(), req->requestid(), req->instance().instanceid(), req->instance().groupid());
    resp->set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
    Send(from, "OnBind", resp->SerializeAsString());
}

void LocalGroupCtrlActor::TimeoutToBind(const std::shared_ptr<messages::ScheduleRequest> &req)
{
    if (reserveResult_.find(req->requestid()) == reserveResult_.end()) {
        return;
    }
    YRLOG_WARN("{}|{}|instance({}) of group({}) reserved resource timeout, going to release it", req->traceid(),
               req->requestid(), req->instance().instanceid(), req->instance().groupid());
    auto type = resource_view::GetResourceType(req->instance());
    resourceViewMgr_->GetInf(type)->DeleteInstances({ req->instance().instanceid() }, true);
    (void)reserveResult_.erase(req->requestid());
}

void LocalGroupCtrlActor::OnBind(const litebus::AID &to, const litebus::Future<Status> &future,
                                 const std::shared_ptr<messages::ScheduleRequest> &req,
                                 const std::shared_ptr<messages::GroupResponse> &resp)
{
    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    if (status.IsOk()) {
        YRLOG_INFO("{}|{}|successful to bind instance({}) of groupID({})", req->traceid(), req->requestid(),
                   req->instance().instanceid(), req->instance().groupid());
        (void)bindingReqs_.erase(req->requestid());
        Send(to, "OnBind", resp->SerializeAsString());
        return;
    }
    ASSERT_IF_NULL(resourceViewMgr_);
    if (status.StatusCode() == StatusCode::ERR_INSTANCE_DUPLICATED) {
        YRLOG_WARN("{}|{}|instance({}) of groupID({}) is already scheduled to another nodes, rollback local reserve",
                   req->traceid(), req->requestid(), req->instance().instanceid(), req->instance().groupid());

        auto type = resource_view::GetResourceType(req->instance());
        resourceViewMgr_->GetInf(type)->DeleteInstances({ req->instance().instanceid() }, true);
        (void)bindingReqs_.erase(req->requestid());
        Send(to, "OnBind", resp->SerializeAsString());
        return;
    }
    YRLOG_ERROR("{}|{}|failed to bind instance({}) of groupID({}), code: {}， msg：{}", req->traceid(),
                req->requestid(), req->instance().instanceid(), req->instance().groupid(),
                fmt::underlying(status.StatusCode()), status.GetMessage());
    (void)instanceCtrl_->ForceDeleteInstance(req->instance().instanceid())
        .OnComplete(litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnBindFailed, to, status, req, resp));
}

void LocalGroupCtrlActor::OnBindFailed(const litebus::AID &to, const Status &status,
                                       const std::shared_ptr<messages::ScheduleRequest> &req,
                                       const std::shared_ptr<messages::GroupResponse> &resp)
{
    (void)reserveResult_.erase(req->requestid());
    (void)bindingReqs_.erase(req->requestid());
    resp->set_code(static_cast<int32_t>(status.StatusCode()));
    resp->set_message(status.GetMessage());
    Send(to, "OnBind", resp->SerializeAsString());
}

void LocalGroupCtrlActor::UnBind(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!CheckIsReady(name)) {
        return;
    }
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for bind instance. from({}) msg({}), ignore it", std::string(from), msg);
        return;
    }
    YRLOG_INFO("{}|{}|received request of rollback bind instance({}) resource, groupID({})", req->traceid(),
               req->requestid(), req->instance().instanceid(), req->instance().groupid());
    ASSERT_IF_NULL(resourceViewMgr_);
    auto type = resource_view::GetResourceType(req->instance());
    resourceViewMgr_->GetInf(type)->DeleteInstances({ req->instance().instanceid() });
    (void)instanceCtrl_->ForceDeleteInstance(req->instance().instanceid())
        .OnComplete(litebus::Defer(GetAID(), &LocalGroupCtrlActor::OnUnBind, from, req));
}

void LocalGroupCtrlActor::ClearGroup(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!CheckIsReady(name)) {
        return;
    }
    auto killGroupReq = std::make_shared<::messages::KillGroup>();
    if (!killGroupReq->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for clear group. from({}) msg({}), ignore it", std::string(from), msg);
        return;
    }
    YRLOG_INFO("receive clear group {}", killGroupReq->grouprequestid());
    auto ctx = GetGroupCtx(killGroupReq->grouprequestid());
    if (ctx == nullptr) {
        OnClearGroup(from, killGroupReq->groupid());
        return;
    }
    for (auto request : ctx->requests) {
        (void)reserveResult_.erase(request->requestid());
        ASSERT_IF_NULL(instanceCtrl_);
        instanceCtrl_->DeleteSchedulingInstance(request->instance().instanceid(), request->requestid());
    }
    DeleteGroupCtx(killGroupReq->grouprequestid());
    OnClearGroup(from, killGroupReq->groupid());
}

void LocalGroupCtrlActor::OnClearGroup(const litebus::AID &to, const std::string &groupID)
{
    auto msg = ::messages::KillGroupResponse{};
    msg.set_groupid(groupID);
    Send(to, "OnClearGroup", msg.SerializeAsString());
}

void LocalGroupCtrlActor::OnUnBind(const litebus::AID &to, const std::shared_ptr<messages::ScheduleRequest> &req)
{
    (void)reserveResult_.erase(req->requestid());
    (void)bindingReqs_.erase(req->requestid());
    messages::GroupResponse resp;
    resp.set_requestid(req->requestid());
    resp.set_traceid(req->traceid());
    Send(to, "OnUnBind", resp.SerializeAsString());
}

void LocalGroupCtrlActor::Finalize()
{
    ActorBase::Finalize();
}

bool LocalGroupCtrlActor::CheckIsReady(const std::string &name)
{
    if (!IsReady()) {
        YRLOG_WARN("local group ctrl actor not ready, refuse to {}", name);
        return false;
    }
    return true;
}

void LocalGroupCtrlActor::CompareSynced(const litebus::Future<GroupInfos> &future)
{
    ASSERT_FS(future.IsOK());
    auto groupInfos = future.Get();
    if (groupInfos.empty()) {
        YRLOG_WARN("no group info synced from etcd");
        return;
    }
    std::unordered_map<std::string, std::shared_ptr<messages::GroupInfo>> syncedGroupInfos;
    for (const auto &info : groupInfos) {
        if (info->ownerproxy() != nodeID_) {
            continue;
        }
        // owned by self but not found in cache, which should be deleted
        if (groupCtxs_.find(info->requestid()) == groupCtxs_.end()) {
            YRLOG_INFO("group({}) not found in cache, going to delete it", info->groupid());
            (void)groupOperator_->DeleteGroupInstances(info);
            continue;
        }
        syncedGroupInfos[info->requestid()] = info;
    }
    std::set<std::string> toBeDeleted;
    for (auto &ctxPair : groupCtxs_) {
        auto ctx = ctxPair.second;
        // found in cache but not found in metastore, which should be deleted
        if (syncedGroupInfos.find(ctx->groupInfo->requestid()) == syncedGroupInfos.end()) {
            YRLOG_INFO("{}|group({}) not found in meta, going to clear it in cache", ctx->groupInfo->requestid(),
                       ctx->groupInfo->groupid());
            toBeDeleted.insert(ctx->groupInfo->requestid());
        }
    }
    for (auto &request : toBeDeleted) {
        DeleteGroupCtx(request);
    }
}

void LocalGroupCtrlActor::OnHealthyStatus(const Status &status)
{
    if (status.IsError()) {
        return;
    }
    if (!IsReady()) {
        return;
    }
    YRLOG_INFO("metastore is recovered. sync local group info from metastore.");
    groupOperator_->SyncGroupInstances().OnComplete(
        litebus::Defer(GetAID(), &LocalGroupCtrlActor::CompareSynced, std::placeholders::_1));
}

litebus::Future<Status> LocalGroupCtrlActor::GroupOperator::TxnGroupInstances(
    const std::shared_ptr<messages::GroupInfo> &req)
{
    ASSERT_IF_NULL(metaStoreClient_);
    auto key = GROUP_SCHEDULE + "/" + req->requestid() + "/" + req->groupid();
    YRLOG_INFO("begin to transaction group instances, key: {}", key);
    // the instance information in the current message is redundant and will be optimized in the future.
    std::string jsonStr;
    if (!google::protobuf::util::MessageToJsonString(*req, &jsonStr).ok()) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR,
                      "failed to trans group info to json string. request:" + req->requestid());
    }
    return metaStoreClient_->Put(key, jsonStr, {}).Then([req](const std::shared_ptr<PutResponse> &putResponse) {
        if (putResponse->status.IsError()) {
            return Status(StatusCode::ERR_ETCD_OPERATION_ERROR,
                          "failed to put group info to etcd. request: " + req->requestid() +
                              ", err: " + putResponse->status.GetMessage());
        }
        return Status::OK();
    });
}

litebus::Future<GroupInfos> LocalGroupCtrlActor::GroupOperator::SyncGroupInstances()
{
    YRLOG_INFO("begin to sync group info, key-prefix: {}", GROUP_SCHEDULE);
    ASSERT_IF_NULL(metaStoreClient_);
    return metaStoreClient_->Get(GROUP_SCHEDULE, { .prefix = true })
        .Then([prefix(metaStoreClient_->GetTablePrefix())](const std::shared_ptr<GetResponse> &getResponse)
                  -> litebus::Future<std::vector<std::shared_ptr<messages::GroupInfo>>> {
            std::vector<std::shared_ptr<messages::GroupInfo>> groupInfos;
            if (getResponse->status.IsError()) {
                YRLOG_WARN("failed to sync group info, key-prefix: {} err: ", GROUP_SCHEDULE,
                           getResponse->status.ToString());
                return groupInfos;
            }
            if (getResponse->kvs.empty()) {
                YRLOG_INFO("get no result with key({}) from meta storage", GROUP_SCHEDULE);
                return groupInfos;
            }
            auto jsonOpt = google::protobuf::util::JsonParseOptions();
            jsonOpt.ignore_unknown_fields = true;
            jsonOpt.case_insensitive_enum_parsing = true;
            for (auto &kv : getResponse->kvs) {
                auto eventKey = TrimKeyPrefix(kv.key(), prefix);
                auto groupInfo = std::make_shared<messages::GroupInfo>();
                if (!google::protobuf::util::JsonStringToMessage(kv.value(), groupInfo.get(), jsonOpt).ok()) {
                    YRLOG_WARN("failed to parse {}", eventKey);
                    continue;
                }
                groupInfos.emplace_back(groupInfo);
            }
            return groupInfos;
        });
}

litebus::Future<Status> LocalGroupCtrlActor::GroupOperator::DeleteGroupInstances(
    const std::shared_ptr<messages::GroupInfo> &req)
{
    ASSERT_IF_NULL(metaStoreClient_);
    auto key = GROUP_SCHEDULE + "/" + req->requestid() + "/" + req->groupid();
    YRLOG_INFO("begin to delete group instances, key: {}", key);
    return metaStoreClient_->Delete(key, {}).Then(
        [req](const std::shared_ptr<DeleteResponse> &deleteResponse) -> litebus::Future<Status> {
            if (deleteResponse->status.IsError()) {
                return Status(StatusCode::ERR_ETCD_OPERATION_ERROR,
                              "failed to put group info to etcd. request:" + req->requestid() +
                                  ", err: " + deleteResponse->status.GetMessage());
            }
            return Status::OK();
        });
}
}  // namespace functionsystem::local_scheduler