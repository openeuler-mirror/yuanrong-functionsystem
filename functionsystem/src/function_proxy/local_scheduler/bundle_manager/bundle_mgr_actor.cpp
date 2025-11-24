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

#include "bundle_mgr_actor.h"

#include "async/collect.hpp"
#include "async/defer.hpp"

namespace functionsystem::local_scheduler {

const int64_t REPORT_ABNORMAL_RETRY_INTERVAL = 1000;
const std::string BUNDLE_ID_SEPARATOR = "_";

static std::string GetBundleStoreKey(const std::string nodeId)
{
    return "/yr/bundle/" + nodeId;
}

static std::string GetResourceGroupName(const std::string &bundleId)
{
    // BundleId format {rg_name_length}_{rg_name}_{requestID}_{index}
    auto firstSeparator = bundleId.find(BUNDLE_ID_SEPARATOR);
    try {
        auto rgNameLen = std::stoi(bundleId.substr(0, firstSeparator));
        if (rgNameLen > static_cast<int>(bundleId.length())) {
            return "";
        }
        return bundleId.substr(firstSeparator + 1, rgNameLen);
    } catch (const std::invalid_argument& ia) {
        YRLOG_ERROR("Failed to extract rg name from bundleId({}), {}", bundleId, ia.what());
    } catch (const std::out_of_range& oor) {
        YRLOG_ERROR("Failed to extract rg name from bundleId({}), {}", bundleId, oor.what());
    }
    return "";
}

ResourceType GetResourceType(const std::string &rGroup)
{
    resources::InstanceInfo ins;
    ins.mutable_scheduleoption()->set_rgroupname(rGroup);
    return resource_view::GetResourceType(ins);
}

BundleMgrActor::BundleMgrActor(const functionsystem::local_scheduler::BundleManagerActorParam &bundleManagerActorParam)
    : BasisActor(bundleManagerActorParam.actorName),
      nodeID_(bundleManagerActorParam.nodeId),
      bundleOperator_(std::make_shared<BundleOperator>(bundleManagerActorParam.nodeId,
                                                       bundleManagerActorParam.metaStoreClient)),
      reserveToBindTimeoutMs_(bundleManagerActorParam.reservedTimeout)
{
    reportAgentAbnormalHelper_.SetBackOffStrategy([](int64_t attempt) { return REPORT_ABNORMAL_RETRY_INTERVAL; }, -1);
}

void BundleMgrActor::Init()
{
    ActorBase::Init();
    (void)explorer::Explorer::GetInstance().AddLeaderChangedCallback(
        "BundleMgr", [aid(GetAID())](const explorer::LeaderInfo &leaderInfo) {
            litebus::Async(aid, &BundleMgrActor::UpdateMasterInfo, leaderInfo);
        });

    Receive("Reserves", &BundleMgrActor::Reserves);
    Receive("UnReserve", &BundleMgrActor::UnReserve);
    Receive("Bind", &BundleMgrActor::Bind);
    Receive("UnBind", &BundleMgrActor::UnBind);
    Receive("RemoveBundle", &BundleMgrActor::RemoveBundle);
    Receive("ForwardReportAgentAbnormalResponse", &BundleMgrActor::ForwardReportAgentAbnormalResponse);
}

void BundleMgrActor::Finalize()
{
    ActorBase::Finalize();
}

void BundleMgrActor::UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo)
{
    if (resourceGroupManagerAID_ == nullptr) {
        resourceGroupManagerAID_ = std::make_shared<litebus::AID>();
    }
    YRLOG_INFO("begin update master info, cur master: {}, new master: {}", resourceGroupManagerAID_->GetIp(),
               leaderInfo.address);
    resourceGroupManagerAID_ = std::make_shared<litebus::AID>(RESOURCE_GROUP_MANAGER, leaderInfo.address);
    resourceGroupManagerAID_->SetProtocol(litebus::BUS_TCP);
}

litebus::Future<Status> BundleMgrActor::Sync()
{
    ASSERT_IF_NULL(bundleOperator_);
    YRLOG_INFO("start to sync bundle info.");
    return bundleOperator_->GetBundles().Then(
        litebus::Defer(GetAID(), &BundleMgrActor::OnSyncBundle, std::placeholders::_1));
}

litebus::Future<Status> BundleMgrActor::Recover()
{
    return Status::OK();
}

void BundleMgrActor::Reserves(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::Reserves>();
    if (!IsReady()) {
        YRLOG_WARN("Failed to {}, bundle manager actor not ready", name);
        return;
    }
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("Failed to parse request for reserve resource. from({}) msg({}), ignore it", std::string(from),
                    msg);
        return;
    }
    YRLOG_INFO("{}|{}|received request of batch reserve bundle({}) resource, groupID({})", req->traceid(),
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
            litebus::Async(aid, &BundleMgrActor::CollectResourceChangesForOnReserves, resp)
                .Then([aid, from, resp](const Status &status) -> litebus::Future<Status> {
                    litebus::Async(aid, &BundleMgrActor::SendMsg, from, "OnReserves", resp->SerializeAsString());
                    return status;
                });
        });
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> BundleMgrActor::DoReserve(
    std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto resp = std::make_shared<messages::ScheduleResponse>();
    resp->set_requestid(req->requestid());
    resp->set_instanceid(req->instance().instanceid());
    *resp->mutable_contexts() = req->contexts();
    if (reserveResult_.find(req->requestid()) != reserveResult_.end()) {
        YRLOG_INFO("{}|{}|bundle({}) resource is already reserved to {} in {}", req->traceid(), req->requestid(),
                   req->instance().instanceid(), reserveResult_[req->requestid()].result.unitID,
                   reserveResult_[req->requestid()].result.id);
        litebus::TimerTools::Cancel(reserveResult_[req->requestid()].reserveTimer);
        reserveResult_[req->requestid()].reserveTimer =
            litebus::AsyncAfter(reserveToBindTimeoutMs_, GetAID(), &BundleMgrActor::TimeoutToBind, req);
        return resp;
    }
    ASSERT_IF_NULL(scheduler_);
    return scheduler_->ScheduleDecision(req).Then(
        litebus::Defer(GetAID(), &BundleMgrActor::OnReserve, std::placeholders::_1, req, resp));
}

void BundleMgrActor::UnReserve(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!IsPreCheckPassed(from, std::move(name), std::move(msg), req)) {
        return;
    }
    YRLOG_INFO("{}|{}|received request of rollback reserve bundle({}) resource, rGroup({})", req->traceid(),
               req->requestid(), req->instance().instanceid(), GetResourceGroupName(req->instance().instanceid()));
    // rollback reserved resources
    auto resourceView = GetResourceView(req->instance().scheduleoption().rgroupname());
    if (resourceView != nullptr) {
        (void)resourceView->DeleteInstances({ req->instance().instanceid() }, true);
    }
    // clear reserve result cache
    (void)reserveResult_.erase(req->requestid());
    auto resp = std::make_shared<messages::GroupResponse>();
    resp->set_requestid(req->requestid());
    resp->set_traceid(req->traceid());

    (void)CollectResourceChangesForGroupResp(resp).Then([aid(GetAID()), from, resp](const Status &status) ->
                                                        litebus::Future<Status> {
        litebus::Async(aid, &BundleMgrActor::SendMsg, from, "OnUnReserve", resp->SerializeAsString());
        return status;
    });
}

void BundleMgrActor::Bind(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!IsPreCheckPassed(from, std::move(name), std::move(msg), req)) {
        return;
    }
    auto resp = std::make_shared<messages::GroupResponse>();
    resp->set_requestid(req->requestid());
    resp->set_traceid(req->traceid());
    if (reserveResult_.find(req->requestid()) == reserveResult_.end()) {
        YRLOG_INFO("{}|{}|failed to bind bundle, because of not found bundle({}) reserve result, rGroup({})",
                   req->traceid(), req->requestid(), req->instance().instanceid(),
                   GetResourceGroupName(req->instance().instanceid()));
        resp->set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
        Send(from, "OnBind", resp->SerializeAsString());
        return;
    }

    auto result = reserveResult_[req->requestid()].result;
    litebus::TimerTools::Cancel(reserveResult_[req->requestid()].reserveTimer);
    YRLOG_INFO("{}|{}|received request to bind bundle({}) of rGroup({}), deploy to {} in {}", req->traceid(),
               req->requestid(), req->instance().instanceid(), GetResourceGroupName(req->instance().instanceid()),
               result.unitID, result.id);
    auto bundleInfo = reserveResult_[req->requestid()].bundleInfo;
    AddBundle(bundleInfo);

    // persist bundles
    ASSERT_IF_NULL(bundleOperator_);
    (void)PersistBundles().OnComplete(
        litebus::Defer(GetAID(), &BundleMgrActor::OnBind, from, std::placeholders::_1, req, resp));
}

void BundleMgrActor::UnBind(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!IsPreCheckPassed(from, std::move(name), std::move(msg), req)) {
        return;
    }
    YRLOG_INFO("{}|{}|received request of rollback bind bundle({}) resource, rgroup({})", req->traceid(),
               req->requestid(), req->instance().instanceid(), GetResourceGroupName(req->instance().instanceid()));

    messages::GroupResponse resp;
    resp.set_requestid(req->requestid());
    resp.set_traceid(req->traceid());

    // update data in meta store
    DeleteBundle(req->instance().instanceid());
    ASSERT_IF_NULL(bundleOperator_);
    (void)PersistBundles().OnComplete(
        litebus::Defer(GetAID(), &BundleMgrActor::OnUnBind, from, std::placeholders::_1, req));
}

void BundleMgrActor::RemoveBundle(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!IsReady()) {
        YRLOG_WARN("Failed to {}, bundle manager actor not ready", name);
        return;
    }
    auto req = std::make_shared<messages::RemoveBundleRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for removing bundle. from({}) msg({}), ignore it", std::string(from),
                    msg);
        return;
    }
    YRLOG_INFO("{}|received request for removing bundle rGroupName({}), tenantId({})", req->requestid(),
               req->rgroupname(), req->tenantid());
    HandleRemove(req->rgroupname(), req->tenantid());

    ASSERT_IF_NULL(bundleOperator_);
    (void)PersistBundles().OnComplete(
        litebus::Defer(GetAID(), &BundleMgrActor::OnRemoveBundle, from, std::placeholders::_1, req));
}

void BundleMgrActor::TimeoutToBind(const std::shared_ptr<messages::ScheduleRequest> &req)
{
    if (reserveResult_.find(req->requestid()) == reserveResult_.end()) {
        return;
    }
    YRLOG_WARN("{}|{}|reserved resource for bundle({}) timeouts, going to release it", req->traceid(),
               req->requestid(), req->instance().instanceid(), req->instance().scheduleoption().rgroupname());
    auto resourceView = GetResourceView(req->instance().scheduleoption().rgroupname());
    if (resourceView != nullptr) {
        resourceView->DeleteInstances({ req->instance().instanceid() }, true);
    }
    (void)reserveResult_.erase(req->requestid());
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> BundleMgrActor::OnReserve(
    const litebus::Future<schedule_decision::ScheduleResult> &future,
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const std::shared_ptr<messages::ScheduleResponse> &resp)
{
    ASSERT_FS(future.IsOK());
    auto result = future.Get();
    // schedule failed & no new allocated resources
    *resp->mutable_contexts() = req->contexts();
    if (result.code != static_cast<int32_t>(StatusCode::SUCCESS) &&
        result.code != static_cast<int32_t>(StatusCode::INSTANCE_ALLOCATED)) {
        YRLOG_WARN("{}|{}|failed to reserve resource for bundle({}), rGroup({}) code: {} msg:{}", req->traceid(),
                   req->requestid(), req->instance().instanceid(), GetResourceGroupName(req->instance().instanceid()),
                   result.code, result.reason);
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
                YRLOG_ERROR("{}|{}|failed to reserve for bundle({}), rGroup({}), selected unit ({}) in ({}). retry",
                            req->traceid(), req->requestid(), req->instance().instanceid(),
                            GetResourceGroupName(req->instance().instanceid()), result.unitID, result.id);
                return scheduler->ScheduleDecision(req).Then(
                    litebus::Defer(aid, &BundleMgrActor::OnReserve, std::placeholders::_1, req, resp));
            }
            return litebus::Async(aid, &BundleMgrActor::OnSuccessfulReserve, result, req, resp);
        });
    }
    return OnSuccessfulReserve(result, req, resp);
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> BundleMgrActor::OnSuccessfulReserve(
    const schedule_decision::ScheduleResult &result,
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const std::shared_ptr<messages::ScheduleResponse> &resp)
{
    YRLOG_INFO("{}|{}|success to reserve resource for bundle({}), rGroup({}), selected unit ({}) in {}",
               req->traceid(), req->requestid(), req->instance().instanceid(),
               GetResourceGroupName(req->instance().instanceid()), result.unitID, result.id);
    MergeScheduleResultToRequest(req, result);
    auto reservedContext =
        ReservedContext{ .result = result,
                         .reserveTimer = litebus::AsyncAfter(reserveToBindTimeoutMs_, GetAID(),
                                                             &BundleMgrActor::TimeoutToBind, req),
                         .bundleInfo = GenBundle(req, result) };
    reservedContext.result.code = static_cast<int32_t>(StatusCode::SUCCESS);
    reserveResult_[req->requestid()] = reservedContext;
    (*resp->mutable_contexts())[GROUP_SCHEDULE_CONTEXT].mutable_groupschedctx()->set_reserved(result.unitID);
    return resp;
}

void BundleMgrActor::OnBind(const litebus::AID &to, const litebus::Future<Status> &future,
                            const std::shared_ptr<messages::ScheduleRequest> &req,
                            const std::shared_ptr<messages::GroupResponse> &resp)
{
    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    if (!status.IsOk()) {
        YRLOG_ERROR("{}|{}|failed to update bundle in store, code: {}， msg：{}", req->traceid(), req->requestid(),
                    fmt::underlying(status.StatusCode()), status.GetMessage());
        OnBindFailed(to, status, req, resp);
        return;
    }
    auto resourceView = GetResourceView(GetResourceGroupName(req->instance().instanceid()));
    if (resourceView != nullptr) {
        resourceView->AddResourceUnit(GenResourceUnit(reserveResult_[req->requestid()].bundleInfo));
    }
    (void)CollectResourceChangesForGroupResp(resp).Then([aid(GetAID()), to, resp](const Status &status) ->
                                                        litebus::Future<Status> {
        litebus::Async(aid, &BundleMgrActor::SendMsg, to, "OnBind", resp->SerializeAsString());
        return status;
    });
    reserveResult_.erase(req->requestid());
}

void BundleMgrActor::OnBindFailed(const litebus::AID &to, const Status &status,
                                  const std::shared_ptr<messages::ScheduleRequest> &req,
                                  const std::shared_ptr<messages::GroupResponse> &resp)
{
    (void)reserveResult_.erase(req->requestid());
    resp->set_code(static_cast<int32_t>(status.StatusCode()));
    resp->set_message(status.GetMessage());
    Send(to, "OnBind", resp->SerializeAsString());
}

void BundleMgrActor::OnUnBind(const litebus::AID &to, const litebus::Future<Status> &future,
                              const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto resp = std::make_shared<messages::GroupResponse>();
    resp->set_requestid(req->requestid());
    resp->set_traceid(req->traceid());

    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    if (!status.IsOk()) {
        YRLOG_ERROR("{}|{}|failed to update bundle in store, code: {}， msg：{}", req->traceid(), req->requestid(),
                    fmt::underlying(status.StatusCode()), status.GetMessage());
        resp->set_code(static_cast<int32_t>(status.StatusCode()));
        resp->set_message(status.GetMessage());
        Send(to, "OnUnBind", resp->SerializeAsString());
        return;
    }

    (void)reserveResult_.erase(req->requestid());

    // rollback bound resources
    auto resourceView = GetResourceView(GetResourceGroupName(req->instance().instanceid()));
    if (resourceView != nullptr) {
        resourceView->DeleteResourceUnit({ req->instance().instanceid() });
    }

    // rollback reserved resources
    auto reserveResourceView = GetResourceView(req->instance().scheduleoption().rgroupname());
    if (reserveResourceView != nullptr) {
        reserveResourceView->DeleteInstances({ req->instance().instanceid() }, true);
    }
    (void)CollectResourceChangesForGroupResp(resp).Then([aid(GetAID()), to, resp](const Status &status) ->
                                                        litebus::Future<Status> {
        litebus::Async(aid, &BundleMgrActor::SendMsg, to, "OnUnBind", resp->SerializeAsString());
        return status;
    });
}

void BundleMgrActor::HandleRemove(const std::string &rGroupName, const std::string &tenantId)
{
    std::vector<std::string> bundleIdsToDelete;
    for (auto &it : bundles_) {
        if (it.second.tenantid() == tenantId && it.second.rgroupname() == rGroupName) {
            bundleIdsToDelete.emplace_back(it.first);
        }
    }
    for (auto &bundleId : bundleIdsToDelete) {
        RemoveBundleById(bundleId);
    }
}

void BundleMgrActor::RemoveBundleById(const std::string &bundleId)
{
    // find descendants
    std::set<std::string> descendants;
    for (auto &it : bundles_) {
        if (it.second.parentid() == bundleId) {
            descendants.insert(it.first);
        }
    }

    for (auto &descendant : descendants) {
        if (auto it = bundles_.find(descendant); it != bundles_.end()) {
            RemoveBundleById(descendant);
        }
    }
    // remove resource in view
    if (auto it = bundles_.find(bundleId); it != bundles_.end()) {
        resourceViewMgr_->GetInf(GetResourceType(it->second.rgroupname()))->GetResourceUnit(bundleId).OnComplete(
            litebus::Defer(GetAID(), &BundleMgrActor::DoRemoveBundle, std::placeholders::_1, it->second));
    }
    // remove in cache
    DeleteBundle(bundleId);
}

void BundleMgrActor::DoRemoveBundle(const litebus::Future<litebus::Option<ResourceUnit>> &future,
                                    const messages::BundleInfo bundleInfo)
{
    ASSERT_FS(future.IsOK());
    auto unitOpt = future.Get();
    if (unitOpt.IsSome()) {
        auto unit =  unitOpt.Get();
        for (auto &ins : unit.instances()) {
            if (ins.second.scheduleoption().target() == resources::CreateTarget::INSTANCE) {
                instanceCtrl_->ForceDeleteInstance(ins.first);
            }
        }
        resourceViewMgr_->GetInf(GetResourceType(bundleInfo.rgroupname()))->DeleteResourceUnit(unit.id());
        resourceViewMgr_->GetInf(GetResourceType(bundleInfo.parentrgroupname()))->
            DeleteInstances({ unit.id() }, true);
    } else {
        YRLOG_WARN("ResourceUnit({}) is empty", bundleInfo.bundleid());
    }
}

void BundleMgrActor::OnRemoveBundle(const litebus::AID &to, const litebus::Future<Status> &future,
                                    const std::shared_ptr<messages::RemoveBundleRequest> &req)
{
    auto resp = std::make_shared<messages::RemoveBundleResponse>();
    resp->set_requestid(req->requestid());
    resp->set_rgroupname(req->rgroupname());
    ASSERT_FS(future.IsOK());
    auto status = future.Get();
    if (!status.IsOk()) {
        resp->mutable_status()->set_code(static_cast<int32_t>(status.StatusCode()));
        resp->mutable_status()->set_message(status.GetMessage());
        Send(to, "OnRemoveBundle", resp->SerializeAsString());
        return;
    }
    resp->mutable_status()->set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    Send(to, "OnRemoveBundle", resp->SerializeAsString());
}

litebus::Future<Status> BundleMgrActor::PersistBundles()
{
    if (persistingBundles_ != nullptr && waitToPersistBundles_ == nullptr) {
        waitToPersistBundles_ = std::make_shared<litebus::Promise<Status>>();
        return waitToPersistBundles_->GetFuture();
    }
    // bundles are putting to metastore and other update is waiting to update
    // Merge with currently pending updates
    if (waitToPersistBundles_ != nullptr) {
        return waitToPersistBundles_->GetFuture();
    }
    persistingBundles_ = std::make_shared<litebus::Promise<Status>>();
    auto future = persistingBundles_->GetFuture();
    ASSERT_IF_NULL(bundleOperator_);
    (void)bundleOperator_->UpdateBundles(bundles_)
        .OnComplete(litebus::Defer(GetAID(), &BundleMgrActor::OnPutBundlesInMetaStore, std::placeholders::_1));
    return future;
}

void BundleMgrActor::OnPutBundlesInMetaStore(const litebus::Future<Status> &status)
{
    if (persistingBundles_ != nullptr) {
        persistingBundles_->SetValue(status);
        persistingBundles_ = nullptr;
    }
    if (waitToPersistBundles_ == nullptr) {
        return;
    }
    // ready to update new agentInfo
    persistingBundles_ = waitToPersistBundles_;
    waitToPersistBundles_ = nullptr;
    (void)bundleOperator_->UpdateBundles(bundles_)
        .OnComplete(litebus::Defer(GetAID(), &BundleMgrActor::OnPutBundlesInMetaStore, std::placeholders::_1));
}

litebus::Future<Status> BundleMgrActor::CollectResourceChangesForGroupResp(
    const std::shared_ptr<messages::GroupResponse> &resp)
{
    return resourceViewMgr_->GetChanges().Then(
        [resp](const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>> &changes) ->
        litebus::Future<Status> {
            for (auto &it : changes) {
                (*resp->mutable_updateresources())[static_cast<int>(it.first)] = std::move(*it.second);
            }
            return Status::OK();
        });
}

litebus::Future<Status> BundleMgrActor::CollectResourceChangesForOnReserves(
    const std::shared_ptr<messages::OnReserves> &resp)
{
    return resourceViewMgr_->GetChanges().Then(
        [resp](const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>> &changes) ->
        litebus::Future<Status> {
            for (auto &it : changes) {
                (*resp->mutable_updateresources())[static_cast<int>(it.first)] = std::move(*it.second);
            }
            return Status::OK();
        });
}

messages::BundleInfo BundleMgrActor::GenBundle(const std::shared_ptr<messages::ScheduleRequest> &req,
                                               const schedule_decision::ScheduleResult &result)
{
    auto instance = req->instance();
    messages::BundleInfo bundleInfo;
    bundleInfo.set_bundleid(instance.instanceid());
    bundleInfo.set_rgroupname(GetResourceGroupName(instance.instanceid()));
    bundleInfo.set_parentrgroupname(instance.scheduleoption().rgroupname());
    bundleInfo.set_functionproxyid(instance.functionproxyid());
    bundleInfo.set_functionagentid(result.id);
    bundleInfo.set_tenantid(instance.tenantid());
    bundleInfo.set_parentid(result.unitID);
    bundleInfo.mutable_resources()->CopyFrom(instance.resources());
    bundleInfo.mutable_labels()->CopyFrom(instance.labels());
    *bundleInfo.mutable_kvlabels() = instance.kvlabels();
    return bundleInfo;
}

resources::InstanceInfo BundleMgrActor::GenInstanceInfo(const messages::BundleInfo &bundleInfo)
{
    resources::InstanceInfo info;
    info.set_instanceid(bundleInfo.bundleid());
    info.mutable_scheduleoption()->set_rgroupname(bundleInfo.parentrgroupname());
    info.set_functionproxyid(bundleInfo.functionproxyid());
    info.set_functionagentid(bundleInfo.functionagentid());
    info.set_tenantid(bundleInfo.tenantid());
    info.mutable_resources()->CopyFrom(bundleInfo.resources());
    info.mutable_labels()->CopyFrom(bundleInfo.labels());
    *info.mutable_kvlabels() = bundleInfo.kvlabels();
    return info;
}

ResourceUnit BundleMgrActor::GenResourceUnit(const messages::BundleInfo &bundleInfo)
{
    ResourceUnit unit;
    unit.set_id(bundleInfo.bundleid());
    unit.set_ownerid(bundleInfo.functionagentid());
    unit.mutable_capacity()->CopyFrom(bundleInfo.resources());
    unit.mutable_allocatable()->CopyFrom(bundleInfo.resources());
    auto nodeLabels = unit.mutable_nodelabels();

    // if tenantId defined in bundle, add tenantId in label
    if (!bundleInfo.tenantid().empty()) {
        resources::Value::Counter cnter;
        (void)cnter.mutable_items()->insert({ bundleInfo.tenantid(), 1 });
        (void)nodeLabels->insert({ TENANT_ID, cnter });
    }

    for (const auto &label : bundleInfo.labels()) {
        resources::Value::Counter cnter;
        (void)cnter.mutable_items()->insert({ label, 1 });
        if (auto it = nodeLabels->find(label); it != nodeLabels->end()) {
            it->second = it->second + cnter;
        } else {
            (void)nodeLabels->insert({ AFFINITY_SCHEDULE_LABELS, cnter });
        }
        auto kv = ToLabelKV(label);
        *nodeLabels = *nodeLabels + kv;
    }
    for (const auto &[key, value] : bundleInfo.kvlabels()) {
        resources::Value::Counter defaultCnt;
        (void)defaultCnt.mutable_items()->insert({ value, 1 });
        MapCounter result;
        result[key] = defaultCnt;
        *nodeLabels = *nodeLabels + result;
    }
    return unit;
}

void BundleMgrActor::SendMsg(const litebus::AID &to, const std::string &name, const std::string &msg)
{
    (void)Send(to, std::string(name), std::string(msg));
}

bool BundleMgrActor::IsPreCheckPassed(const litebus::AID &from, std::string &&name, std::string &&msg,
                                      std::shared_ptr<messages::ScheduleRequest> &req)
{
    if (!IsReady()) {
        YRLOG_WARN("Failed to {}, bundle manager actor not ready", name);
        return false;
    }
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("Failed to parse request for reserve resource. from({}) msg({}), ignore it", std::string(from),
                    msg);
        return false;
    }
    return true;
}

std::shared_ptr<ResourceView> BundleMgrActor::GetResourceView(const std::string &rGroup)
{
    if (rGroup.empty()) {
        return nullptr;
    }
    auto type = GetResourceType(rGroup);
    if (type == ResourceType::VIRTUAL) {
        return resourceViewMgr_->GetInf(ResourceType::VIRTUAL);
    }
    return resourceViewMgr_->GetInf(ResourceType::PRIMARY);
}

litebus::Future<Status> BundleMgrActor::SyncBundles(const std::string &agentID)
{
    std::list<litebus::Future<Status>> futures;
    auto promise = std::make_shared<litebus::Promise<Status>>();
    std::map<std::string, resource_view::InstanceAllocatedInfo> map;
    for (const auto &bundleID : agentBundles_[agentID]) {
        auto iter = bundles_.find(bundleID);
        if (iter == bundles_.end()) {
            continue;
        }
        auto parentResourceView = GetResourceView(iter->second.parentrgroupname());
        InstanceAllocatedInfo info = { .instanceInfo = GenInstanceInfo(iter->second), {} };
        map.emplace(bundleID, info);
        if (parentResourceView != nullptr) {
            parentResourceView->AddInstances(map);
        }

        auto resourceView = GetResourceView(iter->second.rgroupname());
        if (resourceView != nullptr) {
            futures.push_back(resourceView->AddResourceUnit(GenResourceUnit(iter->second)));
        }
    }

    litebus::Collect<Status>(futures).OnComplete([promise](const litebus::Future<std::list<Status>> &future) {
        if (future.IsError()) {
            promise->SetFailed(litebus::Status::KERROR);
            return;
        }

        for (auto status : future.Get()) {
            if (status.IsError()) {
                promise->SetFailed(litebus::Status::KERROR);
                return;
            }
        }
        promise->SetValue(Status::OK());
    });
    return promise->GetFuture();
}

litebus::Future<Status> BundleMgrActor::NotifyFailedAgent(const std::string &failedAgentID)
{
    if (agentBundles_.find(failedAgentID) == agentBundles_.end()) {
        YRLOG_WARN("notify agent({}) failed not existed", failedAgentID);
        return Status::OK();
    }
    return NotifyFailedBundles(agentBundles_[failedAgentID])
        .Then(litebus::Defer(GetAID(), &BundleMgrActor::OnNotifyFailedAgent, std::placeholders::_1, failedAgentID));
}

litebus::Future<Status> BundleMgrActor::OnNotifyFailedAgent(const Status &status, const std::string &failedAgentID)
{
    if (status.IsError()) {
        YRLOG_ERROR("failed to notify agent({}) failed to resource group manager", failedAgentID);
        return status;
    }
    YRLOG_INFO("success to notify agent({}) failed to resource group manager", failedAgentID);
    agentBundles_.erase(failedAgentID);
    return status;
}

litebus::Future<Status> BundleMgrActor::NotifyFailedBundles(const std::set<std::string> &bundleIDs)
{
    messages::ReportAgentAbnormalRequest request;
    request.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    for (const auto &bundleID : bundleIDs) {
        auto iter = bundles_.find(bundleID);
        if (iter == bundles_.end()) {
            YRLOG_WARN("try to notify non-exist bundle({}) failed", bundleID);
            continue;
        }
        YRLOG_DEBUG("notify bundle({}) failed", bundleID);
        request.add_bundleids(iter->first);
    }

    return reportAgentAbnormalHelper_
        .Begin(request.requestid(), resourceGroupManagerAID_, "ForwardReportAgentAbnormal",
               request.SerializeAsString())
        .Then(litebus::Defer(GetAID(), &BundleMgrActor::OnReportAgentAbnormal, std::placeholders::_1, request));
}

litebus::Future<Status> BundleMgrActor::OnReportAgentAbnormal(
    const std::shared_ptr<messages::ReportAgentAbnormalResponse> &resp, const messages::ReportAgentAbnormalRequest &req)
{
    if (resp->code() != 0) {
        YRLOG_WARN("{}|failed to notify bundles failed, mgs: {}", resp->requestid(), resp->message());
        return Status(StatusCode::FAILED);
    }

    YRLOG_DEBUG("{}|success to notify bundles(size = {}) failed", req.requestid(), req.bundleids_size());
    for (auto bundleID : req.bundleids()) {
        auto iter = bundles_.find(bundleID);
        if (iter == bundles_.end()) {
            YRLOG_WARN("try to delete non-exist bundle({})", bundleID);
            continue;
        }

        auto parentResourceView = GetResourceView(iter->second.parentrgroupname());
        if (parentResourceView != nullptr) {
            parentResourceView->DeleteInstances({ bundleID }, true);
        }

        auto resourceView = GetResourceView(iter->second.rgroupname());
        if (resourceView != nullptr) {
            resourceView->DeleteResourceUnit({ bundleID });
        }

        DeleteBundle(bundleID);
    }
    return PersistBundles();
}

void BundleMgrActor::ForwardReportAgentAbnormalResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto resp = std::make_shared<messages::ReportAgentAbnormalResponse>();
    if (!resp->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse response for ReportAgentAbnormalResponse");
        return;
    }
    YRLOG_DEBUG("{}|received report bundles abnormal response from({}), {}", resp->requestid(), std::string(from), msg);
    reportAgentAbnormalHelper_.End(resp->requestid(), std::move(resp));
}

litebus::Future<Status> BundleMgrActor::SyncFailedBundles(
    const std::unordered_map<std::string, messages::FuncAgentRegisInfo> &agentMap)
{
    std::set<std::string> failedBundles;
    for (auto bundle : bundles_) {
        auto iter = agentMap.find(bundle.second.functionagentid());
        if (iter != agentMap.end()) {
            continue;
        }
        failedBundles.emplace(bundle.first);
    }
    if (failedBundles.empty()) {
        return Status::OK();
    }
    return NotifyFailedBundles(failedBundles);
}

void BundleMgrActor::UpdateBundlesStatus(const std::string &agentID, const resource_view::UnitStatus &status)
{
    if (agentBundles_.find(agentID) == agentBundles_.end()) {
        YRLOG_WARN("try to update non-exist agent({}) bundle status", agentID);
        return;
    }

    YRLOG_INFO("update agent({}) bundle status({})", agentID, static_cast<int>(status));
    for (const auto &bundleID : agentBundles_[agentID]) {
        auto resourceView = GetResourceView(bundleID);
        if (resourceView != nullptr) {
            (void)resourceView->UpdateUnitStatus(bundleID, status);
        }
    }
}

litebus::Future<Status> BundleMgrActor::BundleOperator::UpdateBundles(
    const std::unordered_map<std::string, messages::BundleInfo> &bundles)
{
    ASSERT_IF_NULL(metaStoreClient_);
    auto key = GetBundleStoreKey(nodeID_);

    if (bundles.size() == 0) {
        YRLOG_INFO("Bundle is empty, delete key {}", key);
        return metaStoreClient_->Delete(key, {}).Then([](
            const std::shared_ptr<DeleteResponse> &deleteResponse) -> litebus::Future<Status> {
            if (deleteResponse->status.IsError()) {
                return Status(StatusCode::ERR_ETCD_OPERATION_ERROR,
                              "failed to delete bundle in etcd. Err: " + deleteResponse->status.GetMessage());
            }
            return Status::OK();
        });
    }

    messages::BundleCollection bundleCollection;
    bundleCollection.mutable_bundles()->insert(bundles.begin(), bundles.end());
    std::string jsonStr;
    if (!google::protobuf::util::MessageToJsonString(bundleCollection, &jsonStr).ok()) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "failed to trans bundle info to json string");
    }
    YRLOG_INFO("Begin to update bundles({}), key: {}", static_cast<int>(bundles.size()), key);
    return metaStoreClient_->Put(key, jsonStr, {}).Then([](const std::shared_ptr<PutResponse> &putResponse) {
        if (putResponse->status.IsError()) {
            return Status(StatusCode::ERR_ETCD_OPERATION_ERROR,
                          "failed to put bundle info to etcd. Err: " + putResponse->status.GetMessage());
        }
        return Status::OK();
    });
}

litebus::Future<std::unordered_map<std::string, messages::BundleInfo>> BundleMgrActor::BundleOperator::GetBundles()
{
    return metaStoreClient_->Get(GetBundleStoreKey(nodeID_), {})
        .Then([nodeId(nodeID_)](const std::shared_ptr<GetResponse> &getResponse)
                  -> litebus::Future<std::unordered_map<std::string, messages::BundleInfo>> {
            std::unordered_map<std::string, messages::BundleInfo> bundles;
            if (getResponse->status.IsError()) {
                YRLOG_WARN("failed to get bundle info, key-prefix: {} err: {}", GetBundleStoreKey(nodeId),
                           getResponse->status.ToString());
                return bundles;
            }
            if (getResponse->kvs.empty()) {
                YRLOG_INFO("Get {} bundle collection from meta store. key({})", getResponse->kvs.size(),
                           GetBundleStoreKey(nodeId));
                return bundles;
            }
            auto jsonOpt = google::protobuf::util::JsonParseOptions();
            jsonOpt.ignore_unknown_fields = true;
            jsonOpt.case_insensitive_enum_parsing = true;
            auto bundleCollection = std::make_shared<messages::BundleCollection>();
            auto kv = getResponse->kvs.front();
            if (!google::protobuf::util::JsonStringToMessage(kv.value(), bundleCollection.get(), jsonOpt).ok()) {
                YRLOG_WARN("{} | failed to parse bundles from {}", kv.key(), kv.value());
                return bundles;
            }
            for (auto &it : bundleCollection->bundles()) {
                bundles.insert({ it.first, it.second });
            }
            return bundles;
        });
}

void BundleMgrActor::OnHealthyStatus(const Status &status)
{
    if (status.IsError()) {
        return;
    }
    if (!IsReady()) {
        return;
    }
    YRLOG_INFO("metastore is recovered. sync bundle info from metastore.");
    bundleOperator_->GetBundles().OnComplete(
        litebus::Defer(GetAID(), &BundleMgrActor::CompareSynced, std::placeholders::_1));
}

litebus::Future<Status> BundleMgrActor::OnSyncBundle(
    const litebus::Future<std::unordered_map<std::string, messages::BundleInfo>> &future)
{
    ASSERT_FS(future.IsOK());
    auto result = future.Get();
    bundles_.clear();
    agentBundles_.clear();
    for (auto &it : result) {
        AddBundle(it.second);
    }
    return Status::OK();
}

void BundleMgrActor::CompareSynced(const litebus::Future<std::unordered_map<std::string, messages::BundleInfo>> &future)
{
    ASSERT_FS(future.IsOK());
    auto persistedBundles = future.Get();
    if (persistedBundles.empty()) {
        YRLOG_WARN("no bundle info synced from etcd");
        return;
    }
    std::set<std::string> toBeDeleted;
    for (auto &it : bundles_) {
        if (persistedBundles.find(it.first) == persistedBundles.end()) {
            toBeDeleted.emplace(it.first);
        }
    }
    for (auto &bundleId : toBeDeleted) {
        DeleteBundle(bundleId);
    }
    (void)PersistBundles();
}

void BundleMgrActor::AddBundle(const messages::BundleInfo &bundle)
{
    bundles_[bundle.bundleid()] = bundle;
    agentBundles_[bundle.functionagentid()].emplace(bundle.bundleid());
}

void BundleMgrActor::DeleteBundle(const std::string &bundleID)
{
    auto iter = bundles_.find(bundleID);
    if (iter == bundles_.end()) {
        YRLOG_WARN("try to delete bundle({})", bundleID);
        return;
    }

    agentBundles_[iter->second.functionagentid()].erase(bundleID);
    if (agentBundles_[iter->second.functionagentid()].empty()) {
        agentBundles_.erase(iter->second.functionagentid());
    }
    bundles_.erase(iter);
}
}  // namespace functionsystem::local_scheduler
