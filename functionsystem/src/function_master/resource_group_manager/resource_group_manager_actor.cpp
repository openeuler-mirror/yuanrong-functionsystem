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

#include "resource_group_manager_actor.h"

#include "async/collect.hpp"
#include "async/defer.hpp"
#include "common/utils/struct_transfer.h"
#include "common/types/common_state.h"

namespace functionsystem::resource_group_manager {

const std::string RESOURCE_GROUP_KEY_PREFIX = "/yr/resourcegroup";
const int32_t DEFAULT_RG_SCHEDULE_TIMEOUT_SEC = 30;
const std::string BUNDLE_ID_SEPARATOR = "_";

core_service::CreateResourceGroupResponse GenCreateResourceGroupResponse(const std::string &requestID,
                                                                           const common::ErrorCode code,
                                                                           const std::string &message)
{
    core_service::CreateResourceGroupResponse rsp;
    rsp.set_requestid(requestID);
    rsp.set_code(Status::GetPosixErrorCode(code));
    rsp.set_message(message);
    return rsp;
}

inner_service::ForwardKillResponse GenForwardKillResponse(const std::string requestID, const common::ErrorCode code,
                                                          const std::string &message)
{
    inner_service::ForwardKillResponse rsp;
    rsp.set_requestid(requestID);
    rsp.set_code(Status::GetPosixErrorCode(code));
    rsp.set_message(message);
    return rsp;
}

std::string GenBundleID(const std::string &rgName, const std::string requestID, const int index)
{
    // return format {rg_name_length}_{rg_name}_{requestID}_{index}
    return std::to_string(rgName.length()) + BUNDLE_ID_SEPARATOR + rgName + BUNDLE_ID_SEPARATOR + requestID +
           BUNDLE_ID_SEPARATOR + std::to_string(index);
}

void TransResourceGroupInfo(const std::shared_ptr<core_service::CreateResourceGroupRequest> createRequest,
                             const std::shared_ptr<messages::ResourceGroupInfo> resourceGroupInfo)
{
    auto rgSpec = createRequest->rgroupspec();
    resourceGroupInfo->set_requestid(createRequest->requestid());
    resourceGroupInfo->set_traceid(createRequest->traceid());
    resourceGroupInfo->set_name(rgSpec.name());
    resourceGroupInfo->set_tenantid(rgSpec.tenantid());
    resourceGroupInfo->set_owner(rgSpec.owner());
    resourceGroupInfo->set_appid(rgSpec.appid());
    resourceGroupInfo->mutable_status()->set_code(static_cast<int32_t>(ResourceGroupState::PENDING));
    resourceGroupInfo->mutable_opt()->set_priority(createRequest->rgroupspec().opt().priority());
    resourceGroupInfo->mutable_opt()->set_grouppolicy(createRequest->rgroupspec().opt().grouppolicy());
    *resourceGroupInfo->mutable_opt()->mutable_extension() = createRequest->rgroupspec().opt().extension();
    int index = 0;
    for (const auto &bundle : rgSpec.bundles()) {
        auto bundleInfo = resourceGroupInfo->add_bundles();
        bundleInfo->set_bundleid(GenBundleID(rgSpec.name(), resourceGroupInfo->requestid(), index));
        bundleInfo->set_rgroupname(rgSpec.name());
        bundleInfo->set_parentrgroupname(rgSpec.owner());
        bundleInfo->set_tenantid(resourceGroupInfo->tenantid());
        bundleInfo->mutable_status()->set_code(static_cast<int32_t>(ResourceGroupState::PENDING));
        bundleInfo->mutable_labels()->CopyFrom(bundle.labels());
        auto resources = bundleInfo->mutable_resources()->mutable_resources();
        for (const auto &r : bundle.resources()) {
            resource_view::Resource resource;
            resource.set_name(r.first);
            resource.set_type(resource_view::ValueType::Value_Type_SCALAR);
            resource.mutable_scalar()->set_value(r.second);
            (*resources)[r.first] = std::move(resource);
        }
        index++;
    }
}

void AddBundleToGroupRequest(const messages::BundleInfo &bundleInfo,
                             const std::shared_ptr<messages::ResourceGroupInfo> resourceGroupInfo,
                             const std::shared_ptr<messages::GroupInfo> groupInfo, int index)

{
    auto traceID = resourceGroupInfo->traceid();
    auto requestID = resourceGroupInfo->requestid();
    auto scheduleReq = groupInfo->add_requests();
    scheduleReq->set_traceid(traceID);
    scheduleReq->set_requestid(requestID + "-" + std::to_string(index));
    scheduleReq->set_scheduleround(0);
    auto instanceInfo = scheduleReq->mutable_instance();
    instanceInfo->set_instanceid(bundleInfo.bundleid());
    instanceInfo->set_requestid(scheduleReq->requestid());
    instanceInfo->set_groupid(requestID);
    instanceInfo->set_tenantid(bundleInfo.tenantid());
    instanceInfo->set_scheduletimes(1);
    instanceInfo->mutable_scheduleoption()->set_target(::resources::CreateTarget::RESOURCE_GROUP);
    instanceInfo->mutable_scheduleoption()->set_rgroupname(resourceGroupInfo->owner());
    auto labels = instanceInfo->mutable_labels();
    *labels = bundleInfo.labels();
    *instanceInfo->mutable_resources() = bundleInfo.resources();
    GroupBinPackAffinity("rgroup", bundleInfo.rgroupname(), resourceGroupInfo->opt().grouppolicy(), *instanceInfo);
    (*instanceInfo->mutable_kvlabels())["rgroup"] = bundleInfo.rgroupname();
    (*instanceInfo->mutable_kvlabels())["rg_" + bundleInfo.rgroupname() + "_bundle"] = std::to_string(index);
}

void TransGroupRequest(const std::shared_ptr<messages::ResourceGroupInfo> resourceGroupInfo,
                       const std::shared_ptr<messages::GroupInfo> groupInfo)

{
    auto requestID = resourceGroupInfo->name() + "-" + resourceGroupInfo->requestid();
    groupInfo->set_groupid(requestID);
    groupInfo->set_requestid(requestID);
    groupInfo->set_traceid(resourceGroupInfo->traceid());
    groupInfo->set_rgroupname(resourceGroupInfo->owner());
    groupInfo->set_target(::resources::CreateTarget::RESOURCE_GROUP);
    // To do: use timeout filed in request
    groupInfo->mutable_groupopts()->set_timeout(DEFAULT_RG_SCHEDULE_TIMEOUT_SEC);
    groupInfo->mutable_groupopts()->set_grouppolicy(resourceGroupInfo->opt().grouppolicy());
    int index = 0;
    for (const auto &bundleInfo : resourceGroupInfo->bundles()) {
        AddBundleToGroupRequest(bundleInfo, resourceGroupInfo, groupInfo, index);
        index++;
    }
}

void TransGroupRequestForBundle(const std::shared_ptr<messages::ResourceGroupInfo> resourceGroupInfo, int index,
                                const std::shared_ptr<messages::GroupInfo> groupInfo)

{
    auto requestID = resourceGroupInfo->name() + "-" + resourceGroupInfo->requestid();
    groupInfo->set_groupid(requestID);
    groupInfo->set_requestid(requestID);
    groupInfo->set_traceid(resourceGroupInfo->traceid());
    groupInfo->set_rgroupname(resourceGroupInfo->owner());
    groupInfo->set_target(::resources::CreateTarget::RESOURCE_GROUP);
    if (resourceGroupInfo->bundles().size() <= index) {
        return;
    }
    AddBundleToGroupRequest(resourceGroupInfo->bundles().at(index), resourceGroupInfo, groupInfo, index);
}

void ResourceGroupManagerActor::Init()
{
    curStatus_ = SLAVE_BUSINESS;
    businesses_[MASTER_BUSINESS] = std::make_shared<MasterBusiness>(member_, shared_from_this());
    businesses_[SLAVE_BUSINESS] = std::make_shared<SlaveBusiness>(member_, shared_from_this());
    business_ = businesses_[curStatus_];
    member_->globalScheduler->AddLocalSchedAbnormalNotifyCallback(
        "migrate resource group", [aid(GetAID())](const std::string &nodeID) -> litebus::Future<Status> {
            return litebus::Async(aid, &ResourceGroupManagerActor::OnLocalAbnormal, nodeID);
        });
    (void)explorer::Explorer::GetInstance().AddLeaderChangedCallback(
        RESOURCE_GROUP_MANAGER, [aid(GetAID())](const explorer::LeaderInfo &leaderInfo) {
            litebus::Async(aid, &ResourceGroupManagerActor::UpdateLeaderInfo, leaderInfo);
        });
    Receive("ForwardCreateResourceGroup", &ResourceGroupManagerActor::ForwardCreateResourceGroup);
    Receive("ForwardDeleteResourceGroup", &ResourceGroupManagerActor::ForwardDeleteResourceGroup);
    Receive("ForwardReportAgentAbnormal", &ResourceGroupManagerActor::ForwardReportUnitAbnormal);
    Receive("OnRemoveBundle", &ResourceGroupManagerActor::OnRemoveBundle);
    Receive("ForwardQueryResourceGroup", &ResourceGroupManagerActor::ForwardQueryResourceGroupHandler);
    Receive("ForwardQueryResourceGroupResponse", &ResourceGroupManagerActor::ForwardQueryResourceGroupResponseHandler);
}

void ResourceGroupManagerActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    member_->leaderInfo = leaderInfo;
    litebus::AID masterAID(RESOURCE_GROUP_MANAGER, leaderInfo.address);
    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("new status({}) business don't exist", newStatus);
        return;
    }
    business_ = businesses_[newStatus];
    RETURN_IF_NULL(business_);
    business_->OnChange();
    curStatus_ = newStatus;
}

litebus::Future<Status> ResourceGroupManagerActor::Sync()
{
    ASSERT_IF_NULL(groupOperator_);
    YRLOG_INFO("start to sync resource group info.");
    std::unordered_map<std::string, std::unordered_set<std::string>> oldMap;
    for (const auto &iter : member_->resourceGroups) {
        auto tenantID = iter.first;
        for (const auto &cluster : iter.second) {
            auto rgName = cluster.first;
            if (oldMap.find(tenantID) == oldMap.end()) {
                oldMap[tenantID] = {};
            }
            oldMap[tenantID].emplace(rgName);
        }
    }
    return groupOperator_->SyncResourceGroups().Then(
        litebus::Defer(GetAID(), &ResourceGroupManagerActor::OnSyncResourceGroups, std::placeholders::_1, oldMap));
}

litebus::Future<Status> ResourceGroupManagerActor::OnSyncResourceGroups(
    const std::shared_ptr<GetResponse> &getResponse,
    const std::unordered_map<std::string, std::unordered_set<std::string>> &oldMap)
{
    if (getResponse->status.IsError()) {
        YRLOG_WARN("failed to sync resource group info, err is {}", getResponse->status.ToString());
        return Status::OK();
    }

    std::unordered_map<std::string, std::unordered_set<std::string>> newMap;
    auto jsonOpt = google::protobuf::util::JsonParseOptions();
    jsonOpt.ignore_unknown_fields = true;
    jsonOpt.case_insensitive_enum_parsing = true;
    for (auto &kv : getResponse->kvs) {
        auto resourceGroupInfo = std::make_shared<messages::ResourceGroupInfo>();
        if (!google::protobuf::util::JsonStringToMessage(kv.value(), resourceGroupInfo.get(), jsonOpt).ok()) {
            YRLOG_WARN("failed to parse {}", kv.key());
            continue;
        }
        auto oldCluster = GetResourceGroupInfo(resourceGroupInfo->name(), resourceGroupInfo->tenantid());
        if (oldCluster != nullptr) {
            YRLOG_INFO("sync rg({}) from meta store, will be replaced by new", oldCluster->name());
            DeleteResourceGroupInfo(oldCluster);
        }
        YRLOG_INFO("sync rg({}) from meta store, add new rg", resourceGroupInfo->name());
        AddResourceGroupInfo(resourceGroupInfo);
        // add newMap
        if (newMap.find(resourceGroupInfo->tenantid()) == newMap.end()) {
            newMap[resourceGroupInfo->tenantid()] = {};
        }
        newMap[resourceGroupInfo->tenantid()].emplace(resourceGroupInfo->name());
    }
    // delete metastore not exist, but exist in memory
    for (const auto &iter : oldMap) {
        auto tenantID = iter.first;
        for (const auto &rgName : iter.second) {
            if (newMap.find(tenantID) == newMap.end() || newMap[tenantID].find(rgName) == newMap[tenantID].end()) {
                auto resourceGroup = GetResourceGroupInfo(rgName, tenantID);
                if (resourceGroup != nullptr) {
                    YRLOG_INFO("resource group({}) exist in memory, but not in metastore, remove it", rgName);
                    DeleteResourceGroupInfo(resourceGroup);
                }
            }
        }
    }
    return Status::OK();
}

void ResourceGroupManagerActor::ForwardCreateResourceGroup(const litebus::AID &from, std::string &&name,
                                                             std::string &&msg)
{
    auto createRgRequest = std::make_shared<core_service::CreateResourceGroupRequest>();
    if (!createRgRequest->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse create resource group request, from: {}", from.HashString());
        return;
    }
    TransCreateResourceGroupReq(createRgRequest);
    ASSERT_IF_NULL(business_);
    business_->ForwardCreateResourceGroup(from, createRgRequest);
}

void ResourceGroupManagerActor::ForwardDeleteResourceGroup(const litebus::AID &from, std::string &&name,
                                                             std::string &&msg)
{
    auto killRequest = std::make_shared<inner_service::ForwardKillRequest>();
    if (!killRequest->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse delete virtual request, from: {}", from.HashString());
        return;
    }
    ASSERT_IF_NULL(business_);
    business_->ForwardDeleteResourceGroup(from, killRequest);
}

void ResourceGroupManagerActor::ForwardReportUnitAbnormal(const litebus::AID &from, std::string &&name,
                                                           std::string &&msg)
{
    auto reportAbnormalReq = std::make_shared<messages::ReportAgentAbnormalRequest>();
    if (!reportAbnormalReq->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse report abnormal request, from: {}", from.HashString());
        return;
    }
    ASSERT_IF_NULL(business_);
    business_->ForwardReportUnitAbnormal(from, reportAbnormalReq);
}

void ResourceGroupManagerActor::OnRemoveBundle(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::RemoveBundleResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_WARN("invalid {} response from {} msg {}, ignored", std::string(from), name, msg);
        return;
    }
    if (member_->removeReqPromises.find(rsp.requestid()) == member_->removeReqPromises.end()) {
        return;
    }
    if (rsp.status().code() == static_cast<int32_t>(StatusCode::SUCCESS)) {
        member_->removeReqPromises[rsp.requestid()]->SetValue(Status::OK());
    } else {
        member_->removeReqPromises[rsp.requestid()]->SetValue(
            Status(StatusCode::ERR_INNER_SYSTEM_ERROR, rsp.status().message()));
    }
}

litebus::Future<Status> ResourceGroupManagerActor::OnLocalAbnormal(const std::string &abnormalLocal)
{
    ASSERT_IF_NULL(business_);
    return business_->OnLocalAbnormal(abnormalLocal);
}

litebus::Future<messages::QueryResourceGroupResponse> ResourceGroupManagerActor::QueryResourceGroup(
    const std::shared_ptr<messages::QueryResourceGroupRequest> req)
{
    ASSERT_IF_NULL(business_);
    return business_->QueryResourceGroup(req);
}

void ResourceGroupManagerActor::ForwardQueryResourceGroupHandler(const litebus::AID &from, std::string &&name,
                                                                 std::string &&msg)
{
    auto req = std::make_shared<messages::QueryResourceGroupRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_WARN("invalid QueryResourceGroupRequest {}", msg);
        return;
    }
    ASSERT_IF_NULL(business_);
    business_->QueryResourceGroup(req).OnComplete(
        litebus::Defer(GetAID(), &ResourceGroupManagerActor::OnHandleForwardQueryResourceGroup, from,
                       std::placeholders::_1));
}

void ResourceGroupManagerActor::OnHandleForwardQueryResourceGroup(
    const litebus::AID &from, const litebus::Future<messages::QueryResourceGroupResponse> &rsp)
{
    std::string result = "";
    if (rsp.IsOK()) {
        result = rsp.Get().SerializeAsString();
        YRLOG_INFO("Forward query resource group res is ok");
    } else {
        messages::QueryInstancesInfoResponse errRsp;
        errRsp.set_code(common::ErrorCode::ERR_INNER_SYSTEM_ERROR);
        result = errRsp.SerializeAsString();
        YRLOG_ERROR("Forward query resource group res is err {}", rsp.GetErrorCode());
    }
    Send(from, "ForwardQueryInstancesInfoResponse", std::move(result));
}

void ResourceGroupManagerActor::ForwardQueryResourceGroupResponseHandler(const litebus::AID &from, std::string &&name,
                                                                         std::string &&msg)
{
    auto rsp = std::make_shared<messages::QueryResourceGroupResponse>();
    if (!rsp->ParseFromString(msg)) {
        YRLOG_WARN("invalid QueryResourceGroupResponse {}", msg);
        return;
    }
    if (member_->queryResourceGroupPromise) {
        member_->queryResourceGroupPromise->SetValue(*rsp);
        member_->queryResourceGroupPromise = nullptr;
    } else {
        YRLOG_WARN("unknown ForwardQueryResourceGroupResponseHandler({}) received", rsp->requestid());
    }
}

void ResourceGroupManagerActor::HandleForwardCreateResourceGroup(
    const litebus::AID &from, const std::shared_ptr<core_service::CreateResourceGroupRequest> request)
{
    auto requestID = request->requestid();
    if (member_->createRequests.find(requestID) != member_->createRequests.end()) {
        YRLOG_INFO("{}|receive repeated create resource group request");
        return;
    }
    auto rgName = request->rgroupspec().name();
    YRLOG_INFO("{}|{}|receive create resource group request from {}, name is {}", request->traceid(), requestID,
               from.HashString(), rgName);
    auto cacheCluster = GetResourceGroupInfo(rgName, request->rgroupspec().tenantid());
    if (cacheCluster != nullptr) {
        YRLOG_ERROR("{}|{}|resource group name({}) is duplicated", request->traceid(), requestID, rgName);
        SendCreateResourceGroupResponse(
            GenCreateResourceGroupResponse(requestID, common::ErrorCode::ERR_PARAM_INVALID,
                                            "resource group name exists"),
            from);
        return;
    }
    member_->createRequests.emplace(requestID);
    auto rg = std::make_shared<messages::ResourceGroupInfo>();
    TransResourceGroupInfo(request, rg);
    AddResourceGroupInfo(rg);
    auto groupInfo = std::make_shared<messages::GroupInfo>();
    TransGroupRequest(rg, groupInfo);
    auto promise = std::make_shared<litebus::Promise<core_service::CreateResourceGroupResponse>>();
    ASSERT_IF_NULL(groupOperator_);
    groupOperator_->TxnResourceGroup(rg).Then([aid(GetAID()), groupInfo, promise, vname(rg->name()),
                                                  tenantID(rg->tenantid()),
                                                  requestID(rg->requestid())](const Status &txnStatus) {
        if (txnStatus.IsError()) {
            YRLOG_ERROR("{}|failed to put metastore for rg({})", requestID, vname);
            promise->SetValue(
                GenCreateResourceGroupResponse(requestID, common::ErrorCode::ERR_ETCD_OPERATION_ERROR,
                                                "failed to put metastore, err is " + txnStatus.ToString()));
            return Status::OK();
        }
        litebus::Async(aid, &ResourceGroupManagerActor::ScheduleResourceGroup, promise, requestID, vname, tenantID,
                       groupInfo);
        return Status::OK();
    });
    promise->GetFuture().Then(litebus::Defer(GetAID(), &ResourceGroupManagerActor::SendCreateResourceGroupResponse,
                                             std::placeholders::_1, from));
}

void ResourceGroupManagerActor::ScheduleResourceGroup(
    const std::shared_ptr<litebus::Promise<core_service::CreateResourceGroupResponse>> &promise,
    const std::string &requestID, const std::string &name, const std::string &tenantID,
    const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    (void)ForwardGroupSchedule(groupInfo).Then(
        litebus::Defer(GetAID(), &ResourceGroupManagerActor::ForwardGroupScheduleDone, std::placeholders::_1,
                       requestID, name, tenantID, promise));
}

litebus::Future<messages::GroupResponse> ResourceGroupManagerActor::ForwardGroupSchedule(
    const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    ASSERT_IF_NULL(member_->globalScheduler);
    return member_->globalScheduler->GroupSchedule(groupInfo, defaultRescheduleInterval_);
}

litebus::Future<Status> ResourceGroupManagerActor::ForwardGroupScheduleDone(
    const messages::GroupResponse &groupRsp, const std::string &requestID, const std::string &name,
    const std::string &tenantID,
    const std::shared_ptr<litebus::Promise<core_service::CreateResourceGroupResponse>> &promise)
{
    auto resourceGroupInfo = GetResourceGroupInfo(name, tenantID);
    if (resourceGroupInfo == nullptr) {
        YRLOG_ERROR("{}|failed to find resource group info, name:{}", requestID, name);
        member_->toDeleteResourceGroups.erase(tenantID + "_" + name);
        promise->SetValue(GenCreateResourceGroupResponse(requestID, common::ErrorCode::ERR_INNER_SYSTEM_ERROR,
                                                          "failed to find resource group info"));
        return Status::OK();
    }
    if (groupRsp.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("{}|failed to forward schedule group for resource group, code: {}, msg: {}", requestID,
                    groupRsp.code(), groupRsp.message());
        if (const auto &it = member_->toDeleteResourceGroups.find(resourceGroupInfo->tenantid() + "_"
                                                                  + resourceGroupInfo->name());
            it != member_->toDeleteResourceGroups.end()) {
            YRLOG_INFO("{}|Received delete request({}), do deletion directly", requestID,
                       resourceGroupInfo->tenantid() + "_" + resourceGroupInfo->name());
            DoDeleteResourceGroup(resourceGroupInfo, it->second.second, it->second.first);
            member_->toDeleteResourceGroups.erase(resourceGroupInfo->tenantid() + "_" + resourceGroupInfo->name());
            promise->SetValue(GenCreateResourceGroupResponse(requestID, common::ErrorCode::ERR_SCHEDULE_CANCELED,
                "received resource group deletion request, creation is stopped"));
            return Status::OK();
        }
        resourceGroupInfo->mutable_status()->set_code(static_cast<int32_t>(ResourceGroupState::FAILED));
        resourceGroupInfo->mutable_status()->set_message(groupRsp.message());
        (void)groupOperator_->TxnResourceGroup(resourceGroupInfo);
        promise->SetValue(
            GenCreateResourceGroupResponse(requestID, Status::GetPosixErrorCode(groupRsp.code()),
                                            "failed to create resource group,cause: " + groupRsp.message()));
        return Status::OK();
    }
    resourceGroupInfo->mutable_status()->set_code(static_cast<int32_t>(ResourceGroupState::CREATED));
    ASSERT_FS(static_cast<int32_t>(groupRsp.scheduleresults().size())
              == static_cast<int32_t>(resourceGroupInfo->bundles().size()));
    for (auto result : groupRsp.scheduleresults()) {
        auto bundleID = result.first;
        auto nodeID = result.second.nodeid();
        auto bundleIdx = GetBundleIndex(bundleID);
        if (bundleIdx == nullptr) {
            YRLOG_WARN("({})bundleID not found in rg manager", bundleID);
            continue;
        }
        resourceGroupInfo->mutable_bundles(bundleIdx->index)->set_functionproxyid(nodeID);
        resourceGroupInfo->mutable_bundles(bundleIdx->index)->mutable_status()->set_code(
            static_cast<int32_t>(BundleState::CREATED));
        if (member_->proxyID2BundleIDs.find(nodeID) == member_->proxyID2BundleIDs.end()) {
            member_->proxyID2BundleIDs[nodeID] = {};
        }
        member_->proxyID2BundleIDs[nodeID].emplace(bundleID);
    }
    // if delete request is received, interrupt creation process
    if (const auto &it = member_->toDeleteResourceGroups.find(resourceGroupInfo->tenantid() + "_"
                                                              + resourceGroupInfo->name());
        it != member_->toDeleteResourceGroups.end()) {
        YRLOG_INFO("{}|Received delete request({}), interrupt creation process, do deletion", requestID,
                   resourceGroupInfo->tenantid() + "_" + resourceGroupInfo->name());
        DoDeleteResourceGroup(resourceGroupInfo, it->second.second, it->second.first);
        member_->toDeleteResourceGroups.erase(resourceGroupInfo->tenantid() + "_" + resourceGroupInfo->name());
        promise->SetValue(GenCreateResourceGroupResponse(requestID, common::ErrorCode::ERR_SCHEDULE_CANCELED,
            "received resource group deletion request, creation is stopped"));
        return Status::OK();
    }
    ASSERT_IF_NULL(groupOperator_);
    (void)groupOperator_->TxnResourceGroup(resourceGroupInfo)
        .Then([aid(GetAID()), promise, requestID](const Status &txnStatus) {
            if (txnStatus.IsError()) {
                promise->SetValue(GenCreateResourceGroupResponse(
                    requestID, common::ErrorCode::ERR_ETCD_OPERATION_ERROR,
                    "failed to transition to created to meta-store, err is " + txnStatus.ToString()));
                return Status::OK();
            }
            promise->SetValue(GenCreateResourceGroupResponse(requestID, common::ErrorCode::ERR_NONE, ""));
            return Status::OK();
        });
    return Status::OK();
}

void ResourceGroupManagerActor::HandleForwardDeleteResourceGroup(
    const litebus::AID &from, const std::shared_ptr<inner_service::ForwardKillRequest> request)
{
    YRLOG_INFO("{}|receive delete resource group request from {}, name is {}", request->requestid(), from.HashString(),
               request->req().instanceid());
    auto rgName = request->req().instanceid();
    bool isFirst = true;
    bool isExist = false;
    for (const auto &iter : member_->resourceGroups) {
        if (iter.second.find(rgName) != iter.second.end()) {
            isExist = true;
            if (isFirst) {
                DeleteResourceGroupPreCheck(iter.second.at(rgName), from, request);
                isFirst = false;
            } else {
                DeleteResourceGroupPreCheck(iter.second.at(rgName), from, nullptr);
            }
        }
    }
    if (!isExist) {
        SendDeleteResourceGroupResponse(
            GenForwardKillResponse(request->requestid(), common::ErrorCode::ERR_INSTANCE_NOT_FOUND,
                                   "resource group not found"),
            from);
    }
}

void ResourceGroupManagerActor::DeleteResourceGroupPreCheck(
    std::shared_ptr<messages::ResourceGroupInfo> resourceGroupInfo, const litebus::AID &from,
    const std::shared_ptr<inner_service::ForwardKillRequest> request)
{
    if (resourceGroupInfo->mutable_status()->code() == static_cast<int32_t>(ResourceGroupState::PENDING)) {
        member_->toDeleteResourceGroups.emplace(resourceGroupInfo->tenantid() + "_" + resourceGroupInfo->name(),
                                                 std::make_pair(request, from));
        YRLOG_INFO("{}|resource group {} is pending, wait schedule done", request->requestid(),
                   resourceGroupInfo->tenantid() + "_" + resourceGroupInfo->name());
        return;
    }
    DoDeleteResourceGroup(resourceGroupInfo, from, request);
}

void ResourceGroupManagerActor::DoDeleteResourceGroup(
    std::shared_ptr<messages::ResourceGroupInfo> resourceGroupInfo, const litebus::AID &from,
    const std::shared_ptr<inner_service::ForwardKillRequest> request)
{
    std::unordered_set<std::string> proxyIDs;
    for (const auto &bundle : resourceGroupInfo->bundles()) {
        if (!bundle.functionproxyid().empty()) {
            proxyIDs.emplace(bundle.functionproxyid());
        }
    }
    RemoveAllBundles(proxyIDs, resourceGroupInfo->tenantid(), resourceGroupInfo->name())
        .Then(litebus::Defer(GetAID(), &ResourceGroupManagerActor::OnRemoveAllBundles, std::placeholders::_1,
                             resourceGroupInfo, from, request));
}

litebus::Future<Status> ResourceGroupManagerActor::RemoveAllBundles(const std::unordered_set<std::string> proxyIDs,
                                                                     const std::string &tenantID,
                                                                     const std::string &rgName)
{
    if (proxyIDs.empty()) {
        return Status::OK();
    }
    std::list<litebus::Future<Status>> futures;
    for (const auto &nodeID : proxyIDs) {
        auto request = std::make_shared<messages::RemoveBundleRequest>();
        request->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
        request->set_tenantid(tenantID);
        request->set_rgroupname(rgName);
        auto promise = std::make_shared<litebus::Promise<Status>>();
        member_->removeReqPromises[request->requestid()] = promise;
        futures.emplace_back(promise->GetFuture());
        RemoveBundle(request, nodeID);
    }
    auto removePromise = std::make_shared<litebus::Promise<Status>>();
    (void)litebus::Collect(futures).OnComplete([removePromise](const litebus::Future<std::list<Status>> &future) {
        if (future.IsError()) {
            removePromise->SetValue(
                Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to remove bundles on local"));
            return;
        }
        bool isError = false;
        auto result = Status::OK();
        for (auto status : future.Get()) {
            if (status.IsError()) {
                isError = true;
                result.AppendMessage("failed to remove bundle, err is " + status.ToString());
            }
        }
        if (isError) {
            removePromise->SetValue(Status(StatusCode::FAILED, result.GetMessage()));
            return;
        }
        removePromise->SetValue(result);
    });
    return removePromise->GetFuture();
}

void ResourceGroupManagerActor::RemoveBundle(const std::shared_ptr<messages::RemoveBundleRequest> request,
                                              const std::string &nodeID)
{
    (void)member_->globalScheduler->GetLocalAddress(nodeID).Then(
        litebus::Defer(GetAID(), &ResourceGroupManagerActor::RemoveBundleWithLocal, std::placeholders::_1, request));
}

litebus::Future<Status> ResourceGroupManagerActor::RemoveBundleWithLocal(
    const litebus::Option<std::string> &localAddressOpt, const std::shared_ptr<messages::RemoveBundleRequest> request)
{
    if (localAddressOpt.IsNone()) {
        YRLOG_WARN("{}|failed to get local address", request->requestid());
        if (member_->removeReqPromises.find(request->requestid()) != member_->removeReqPromises.end()) {
            member_->removeReqPromises[request->requestid()]->SetValue(Status::OK());
            member_->removeReqPromises.erase(request->requestid());
        }
        return Status::OK();
    }
    auto localAID = litebus::AID("BundleMgrActor", localAddressOpt.Get());
    YRLOG_INFO("{}|send remove bundle to local({}), rg name({})", request->requestid(), localAID.HashString(),
               request->rgroupname());
    (void)Send(localAID, "RemoveBundle", request->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> ResourceGroupManagerActor::OnRemoveAllBundles(
    const Status &status, const std::shared_ptr<messages::ResourceGroupInfo> &resourceGroupInfo,
    const litebus::AID &from, const std::shared_ptr<inner_service::ForwardKillRequest> request)
{
    if (status.IsError()) {
        if (request != nullptr) {
            SendDeleteResourceGroupResponse(
                GenForwardKillResponse(request->requestid(), common::ErrorCode::ERR_INNER_SYSTEM_ERROR,
                                       "failed to delete cluster"),
                from);
        }
        return Status::OK();
    }
    groupOperator_->DeleteResourceGroup(resourceGroupInfo)
        .Then(litebus::Defer(GetAID(), &ResourceGroupManagerActor::OnDeleteResourceGroupFromMetaStore,
                             std::placeholders::_1, resourceGroupInfo, from, request));
    return Status::OK();
}

litebus::Future<Status> ResourceGroupManagerActor::OnDeleteResourceGroupFromMetaStore(
    const Status &status, const std::shared_ptr<messages::ResourceGroupInfo> &resourceGroupInfo,
    const litebus::AID &from, const std::shared_ptr<inner_service::ForwardKillRequest> request)
{
    if (status.IsError()) {
        YRLOG_ERROR("failed to delete rg({}) for metastore, err is {}", resourceGroupInfo->name(), status.ToString());
        if (request != nullptr) {
            SendDeleteResourceGroupResponse(
                GenForwardKillResponse(request->requestid(), common::ErrorCode::ERR_ETCD_OPERATION_ERROR,
                                       "failed to delete cluster info from metastore"),
                from);
        }
        return Status::OK();
    }
    DeleteResourceGroupInfo(resourceGroupInfo);
    YRLOG_INFO("success to delete rg({})", resourceGroupInfo->name());
    if (request != nullptr) {
        SendDeleteResourceGroupResponse(GenForwardKillResponse(request->requestid(), common::ErrorCode::ERR_NONE, ""),
                                         from);
    }
    return Status::OK();
}

void ResourceGroupManagerActor::HandleForwardReportUnitAbnormal(
    const litebus::AID &from, const std::shared_ptr<messages::ReportAgentAbnormalRequest> request)
{
    YRLOG_INFO("{}|receive agent abnormal request from {}", request->requestid(), from.HashString());
    std::unordered_set<std::string> bundleIDs;
    std::unordered_map<std::string, std::shared_ptr<messages::ResourceGroupInfo>> modClusterInfos;
    for (const auto bundleID : request->bundleids()) {
        if (bundleIDs.find(bundleID) != bundleIDs.end()) {
            continue;
        }
        auto bundleIndex = GetBundleIndex(bundleID);
        if (bundleIndex == nullptr) {
            YRLOG_WARN("failed to find bundleID({})", bundleID);
            continue;
        }
        auto bundleInfo = member_->resourceGroups[bundleIndex->tenantID][bundleIndex->groupName]->mutable_bundles(
            bundleIndex->index);
        if (bundleInfo->status().code() != static_cast<int32_t>(BundleState::CREATED)) {
            YRLOG_WARN("bundle status({}) is not created", bundleID);
            continue;
        }
        auto functionProxyID = bundleInfo->functionproxyid();
        bundleInfo->set_functionproxyid("");
        bundleInfo->mutable_status()->set_code(static_cast<int32_t>(BundleState::PENDING));
        bundleIDs.emplace(bundleID);
        YRLOG_INFO("{}|rg({}) bundle({}) on node({}) will be re-scheduled", request->requestid(),
                   bundleInfo->rgroupname(), bundleInfo->bundleid(), functionProxyID);
        if (member_->proxyID2BundleIDs.find(functionProxyID) != member_->proxyID2BundleIDs.end()) {
            member_->proxyID2BundleIDs[functionProxyID].erase(bundleID);
        }
        auto resourceGroupInfo = GetResourceGroupInfo(bundleInfo->rgroupname(), bundleInfo->tenantid());
        auto clusterKey = bundleInfo->tenantid() + "_" + bundleInfo->rgroupname();
        if (modClusterInfos.find(clusterKey) == modClusterInfos.end()) {
            modClusterInfos.emplace(clusterKey, resourceGroupInfo);
        }
    }
    PersistenceAllGroups(modClusterInfos)
        .Then(litebus::Defer(GetAID(), &ResourceGroupManagerActor::OnPersistenceAllGroups, std::placeholders::_1,
                             from, modClusterInfos, request));
}

litebus::Future<Status> ResourceGroupManagerActor::PersistenceAllGroups(
    const std::unordered_map<std::string, std::shared_ptr<messages::ResourceGroupInfo>> resourceGroupInfos)
{
    std::list<litebus::Future<Status>> futures;
    for (auto &iter : resourceGroupInfos) {
        auto future = groupOperator_->TxnResourceGroup(iter.second);
        futures.emplace_back(future);
    }
    if (futures.empty()) {
        YRLOG_WARN("cluster is empty");
        return Status::OK();
    }
    auto promise = std::make_shared<litebus::Promise<Status>>();
    (void)litebus::Collect(futures).OnComplete([promise](const litebus::Future<std::list<Status>> &future) {
        if (future.IsError()) {
            YRLOG_INFO("failed to put cluster info to metastore, err is {}", future.GetErrorCode());
            promise->SetValue(Status(StatusCode::ERR_ETCD_OPERATION_ERROR, "failed to put cluster info to metastore"));
            return;
        }
        bool isError = false;
        auto result = Status::OK();
        for (auto status : future.Get()) {
            if (status.IsError()) {
                isError = true;
                YRLOG_INFO("failed to put cluster info to metastore, err is {}", status.ToString());
                result.AppendMessage("failed to put cluster info to metastore, err is " + status.ToString());
            }
        }
        if (isError) {
            promise->SetValue(Status(StatusCode::ERR_ETCD_OPERATION_ERROR, result.GetMessage()));
            return;
        }
        promise->SetValue(result);
    });
    return promise->GetFuture();
}

litebus::Future<Status> ResourceGroupManagerActor::OnPersistenceAllGroups(
    const Status &status, const litebus::AID &from,
    const std::unordered_map<std::string, std::shared_ptr<messages::ResourceGroupInfo>> resourceGroupInfos,
    const std::shared_ptr<messages::ReportAgentAbnormalRequest> request)
{
    if (!from.Name().empty()) {
        messages::ReportAgentAbnormalResponse rsp;
        rsp.set_requestid(request->requestid());
        rsp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
        if (status.IsError()) {
            rsp.set_code(static_cast<int32_t>(status.StatusCode()));
            rsp.set_message(status.ToString());
        }
        Send(from, "ForwardReportAgentAbnormalResponse", rsp.SerializeAsString());
    }
    for (auto iter : resourceGroupInfos) {
        YRLOG_DEBUG("check re-schedule for rg({})", iter.second->name());
        RescheduleResourceGroup(iter.second->tenantid(), iter.second->name());
    }
    return Status::OK();
}

void ResourceGroupManagerActor::RescheduleResourceGroup(const std::string &tenantID, const std::string &rgName)
{
    auto resourceGroupInfo = GetResourceGroupInfo(rgName, tenantID);
    if (resourceGroupInfo == nullptr) {
        YRLOG_WARN("failed to find rg({}) for re-schedule", rgName);
        return;
    }
    std::list<litebus::Future<messages::GroupResponse>> futures;
    int index = 0;
    for (auto &bundleInfo : resourceGroupInfo->bundles()) {
        if (bundleInfo.status().code() != static_cast<int32_t>(BundleState::PENDING)) {
            index++;
            continue;
        }
        auto groupInfo = std::make_shared<messages::GroupInfo>();
        TransGroupRequestForBundle(resourceGroupInfo, index, groupInfo);
        YRLOG_INFO("start to re-schedule bundle({}) for rg({}) index({})", bundleInfo.bundleid(), rgName, index);
        futures.emplace_back(ForwardGroupSchedule(groupInfo));
        index++;
    }
    if (futures.empty()) {
        return;
    }
    (void)litebus::Collect(futures).OnComplete(litebus::Defer(
        GetAID(), &ResourceGroupManagerActor::OnRescheduleResourceGroup, std::placeholders::_1, tenantID, rgName));
}

litebus::Future<Status> ResourceGroupManagerActor::OnRescheduleResourceGroup(
    const litebus::Future<std::list<messages::GroupResponse>> &future, const std::string &tenantID,
    const std::string &rgName)
{
    if (future.IsError()) {
        YRLOG_ERROR("failed to collect re-schedule resource group response, will retry later");
        litebus::AsyncAfter(defaultRescheduleInterval_, GetAID(), &ResourceGroupManagerActor::RescheduleResourceGroup,
                            tenantID, rgName);
        return Status::OK();
    }
    bool isChanged = false;
    for (auto result : future.Get()) {
        if (result.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
            YRLOG_ERROR("{}|fail to re-schedule bundle for rg({})", result.requestid(), rgName);
            continue;
        }
        for (auto iter : result.scheduleresults()) {
            isChanged = true;
            auto bundleID = iter.first;
            auto nodeID = iter.second.nodeid();
            auto bundleIndex = GetBundleIndex(bundleID);
            if (bundleIndex == nullptr) {
                continue;
            }
            auto bundle = member_->resourceGroups[tenantID][rgName]->mutable_bundles(bundleIndex->index);
            bundle->set_functionproxyid(nodeID);
            bundle->mutable_status()->set_code(static_cast<int32_t>(BundleState::CREATED));
            if (member_->proxyID2BundleIDs.find(nodeID) == member_->proxyID2BundleIDs.end()) {
                member_->proxyID2BundleIDs[nodeID] = {};
            }
            member_->proxyID2BundleIDs[nodeID].emplace(bundleID);
        }
    }
    if (isChanged) {
        auto resourceGroupInfo = GetResourceGroupInfo(rgName, tenantID);
        if (resourceGroupInfo != nullptr) {
            groupOperator_->TxnResourceGroup(resourceGroupInfo)
                .Then([aid(GetAID()), rgName, tenantID](const Status &status) {
                    if (status.IsError()) {
                        YRLOG_WARN("failed to put resource group to meta-store, when rescheduling");
                    }
                    litebus::Async(aid, &ResourceGroupManagerActor::RescheduleResourceGroup, tenantID, rgName);
                    return status;
                });
        }
        return Status::OK();
    }
    litebus::AsyncAfter(defaultRescheduleInterval_, GetAID(), &ResourceGroupManagerActor::RescheduleResourceGroup,
                        tenantID, rgName);
    return Status::OK();
}

litebus::Future<Status> ResourceGroupManagerActor::HandleLocalAbnormal(const std::string &abnormalLocal)
{
    if (member_->proxyID2BundleIDs.find(abnormalLocal) == member_->proxyID2BundleIDs.end()
        || member_->proxyID2BundleIDs[abnormalLocal].empty()) {
        return Status::OK();
    }
    YRLOG_INFO("start to handle local({}) abnormal", abnormalLocal);
    auto proxyBundles = member_->proxyID2BundleIDs[abnormalLocal];
    auto request = std::make_shared<messages::ReportAgentAbnormalRequest>();
    request->set_requestid("proxy-ab-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    for (auto &id : proxyBundles) {
        *(request->add_bundleids()) = id;
    }
    HandleForwardReportUnitAbnormal(litebus::AID(), request);
    return Status::OK();
}

litebus::Future<Status> ResourceGroupManagerActor::SendCreateResourceGroupResponse(
    const core_service::CreateResourceGroupResponse &response, const litebus::AID &from)
{
    YRLOG_INFO("{}|send forward schedule response for resource group", response.requestid());
    Send(from, "OnForwardCreateResourceGroup", response.SerializeAsString());
    member_->createRequests.erase(response.requestid());
    return Status::OK();
}

litebus::Future<Status> ResourceGroupManagerActor::SendDeleteResourceGroupResponse(
    const inner_service::ForwardKillResponse &response, const litebus::AID &from)
{
    YRLOG_INFO("{}|send delete resource group response", response.requestid());
    Send(from, "OnForwardDeleteResourceGroup", response.SerializeAsString());
    return Status::OK();
}

void ResourceGroupManagerActor::MasterBusiness::OnChange()
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->Sync();
}

void ResourceGroupManagerActor::MasterBusiness::ForwardCreateResourceGroup(
    const litebus::AID &from, const std::shared_ptr<core_service::CreateResourceGroupRequest> request)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->HandleForwardCreateResourceGroup(from, request);
}

void ResourceGroupManagerActor::MasterBusiness::ForwardDeleteResourceGroup(
    const litebus::AID &from, const std::shared_ptr<inner_service::ForwardKillRequest> request)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->HandleForwardDeleteResourceGroup(from, request);
}

void ResourceGroupManagerActor::MasterBusiness::ForwardReportUnitAbnormal(
    const litebus::AID &from, const std::shared_ptr<messages::ReportAgentAbnormalRequest> request)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->HandleForwardReportUnitAbnormal(from, request);
}

litebus::Future<Status> ResourceGroupManagerActor::MasterBusiness::OnLocalAbnormal(const std::string &abnormalLocal)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    return actor->HandleLocalAbnormal(abnormalLocal);
}

litebus::Future<messages::QueryResourceGroupResponse> ResourceGroupManagerActor::MasterBusiness::QueryResourceGroup(
    const std::shared_ptr<messages::QueryResourceGroupRequest> req)
{
    messages::QueryResourceGroupResponse rsp;
    rsp.set_requestid(req->requestid());
    for (auto mapIt : member_->resourceGroups) {
        for (auto &it : mapIt.second) {
            if (req->rgroupname().empty()) {
                rsp.mutable_rgroup()->Add(messages::ResourceGroupInfo(*it.second));
            } else if (req->rgroupname() == it.second->name()) {
                rsp.mutable_rgroup()->Add(messages::ResourceGroupInfo(*it.second));
                break;
            }
        }
    }
    YRLOG_INFO("{}|QueryResourceGroup get {} resource groups", rsp.requestid(), static_cast<int>(rsp.rgroup().size()));
    return rsp;
}

void ResourceGroupManagerActor::SlaveBusiness::ForwardCreateResourceGroup(
    const litebus::AID &from, const std::shared_ptr<core_service::CreateResourceGroupRequest> request)
{
    YRLOG_WARN("{}|{}|slave receive create resource group request from {}, name is {}", request->traceid(),
               request->requestid(), from.HashString(), request->rgroupspec().name());
    auto resp = std::make_shared<core_service::CreateResourceGroupResponse>();
    resp->set_requestid(request->requestid());
    resp->set_code(common::ErrorCode::ERR_INNER_COMMUNICATION);
    resp->set_message("failed to create resource group, master is changed");
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->Send(from, "OnForwardCreateResourceGroup", resp->SerializeAsString());
}

void ResourceGroupManagerActor::SlaveBusiness::ForwardDeleteResourceGroup(
    const litebus::AID &from, const std::shared_ptr<inner_service::ForwardKillRequest> request)
{
    YRLOG_WARN("{}|slave receive delete resource group request from {}, name is {}", request->requestid(),
               from.HashString(), request->req().instanceid());
    auto resp = std::make_shared<inner_service::ForwardKillResponse>();
    resp->set_requestid(request->requestid());
    resp->set_code(common::ErrorCode::ERR_INNER_COMMUNICATION);
    resp->set_message("failed to delete resource group, master is changed");
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->Send(from, "OnForwardDeleteResourceGroup", resp->SerializeAsString());
}

void ResourceGroupManagerActor::SlaveBusiness::ForwardReportUnitAbnormal(
    const litebus::AID &from, const std::shared_ptr<messages::ReportAgentAbnormalRequest> request)
{
    YRLOG_WARN("{}|slave receive agent abnormal request from {}", request->requestid(), from.HashString());
}

litebus::Future<Status> ResourceGroupManagerActor::SlaveBusiness::OnLocalAbnormal(const std::string &abnormalLocal)
{
    return Status::OK();
}

void ResourceGroupManagerActor::SlaveBusiness::OnChange()
{
}

litebus::Future<messages::QueryResourceGroupResponse> ResourceGroupManagerActor::SlaveBusiness::QueryResourceGroup(
    const std::shared_ptr<messages::QueryResourceGroupRequest> req)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);

    if (!member_->queryResourceGroupPromise) {
        member_->queryResourceGroupPromise = std::make_shared<litebus::Promise<messages::QueryResourceGroupResponse>>();
        litebus::AID masterAID(RESOURCE_GROUP_MANAGER, member_->leaderInfo.address);
        (void)actor->Send(masterAID, "ForwardQueryResourceGroup", req->SerializeAsString());
        YRLOG_INFO("{}|Slave sends QueryResourceGroup to Master {}", req->requestid(), std::string(masterAID));
    }
    return member_->queryResourceGroupPromise->GetFuture();
}

void ResourceGroupManagerActor::AddResourceGroupInfo(const std::shared_ptr<messages::ResourceGroupInfo> &req)
{
    auto tenantID = req->tenantid();
    auto groupName = req->name();
    if (member_->resourceGroups.find(tenantID) == member_->resourceGroups.end()) {
        member_->resourceGroups[tenantID] = {};
    }
    member_->resourceGroups[tenantID][groupName] = req;
    // add bundle
    auto index = 0;
    for (const auto &bundle : req->bundles()) {
        AddBundleInfo(bundle, index);
        index++;
    }
}

void ResourceGroupManagerActor::DeleteResourceGroupInfo(const std::shared_ptr<messages::ResourceGroupInfo> &req)
{
    auto tenantID = req->tenantid();
    auto groupName = req->name();
    if (member_->resourceGroups.find(tenantID) == member_->resourceGroups.end()
        || member_->resourceGroups[tenantID].find(groupName) == member_->resourceGroups[tenantID].end()) {
        YRLOG_WARN("cannot found resource group {} to delete", groupName);
        return;
    }
    auto resourceGroup = member_->resourceGroups[tenantID][groupName];
    for (const auto &bundle : resourceGroup->bundles()) {
        DeleteBundleInfo(bundle);
    }
    (void)member_->resourceGroups[tenantID].erase(groupName);
    if (member_->resourceGroups[tenantID].empty()) {
        (void)member_->resourceGroups.erase(tenantID);
    }
}

const std::shared_ptr<messages::ResourceGroupInfo> ResourceGroupManagerActor::GetResourceGroupInfo(
    const std::string &name, const std::string &tenantID)
{
    if (member_->resourceGroups.find(tenantID) == member_->resourceGroups.end()
        || member_->resourceGroups[tenantID].find(name) == member_->resourceGroups[tenantID].end()) {
        return nullptr;
    }
    return member_->resourceGroups[tenantID][name];
}

std::shared_ptr<BundleIndex> ResourceGroupManagerActor::GetBundleIndex(const std::string &bundleID)
{
    if (member_->bundleInfos.find(bundleID) == member_->bundleInfos.end()) {
        return nullptr;
    }
    auto bundleIndex = member_->bundleInfos[bundleID];
    YRLOG_DEBUG("get bundle index, rgName({}) index({})", bundleIndex->groupName, bundleIndex->index);
    if (member_->resourceGroups.find(bundleIndex->tenantID) == member_->resourceGroups.end()
        || member_->resourceGroups[bundleIndex->tenantID].find(bundleIndex->groupName)
               == member_->resourceGroups[bundleIndex->tenantID].end()) {
        member_->bundleInfos.erase(bundleID);
        return nullptr;
    }
    auto rg = member_->resourceGroups[bundleIndex->tenantID][bundleIndex->groupName];
    if (rg->bundles().size() <= bundleIndex->index) {
        member_->bundleInfos.erase(bundleID);
        return nullptr;
    }
    if (rg->bundles()[bundleIndex->index].bundleid() != bundleID) {
        member_->bundleInfos.erase(bundleID);
        return nullptr;
    }
    return bundleIndex;
}

void ResourceGroupManagerActor::AddBundleInfo(const messages::BundleInfo &bundle, const int32_t index)
{
    auto bundleIndex = std::make_shared<BundleIndex>();
    bundleIndex->tenantID = bundle.tenantid();
    bundleIndex->groupName = bundle.rgroupname();
    bundleIndex->index = index;
    member_->bundleInfos[bundle.bundleid()] = bundleIndex;
    if (bundle.functionproxyid().empty()) {
        return;
    }
    if (member_->proxyID2BundleIDs.find(bundle.functionproxyid()) == member_->proxyID2BundleIDs.end()) {
        member_->proxyID2BundleIDs[bundle.functionproxyid()] = {};
    }
    member_->proxyID2BundleIDs[bundle.functionproxyid()].emplace(bundle.bundleid());
}

void ResourceGroupManagerActor::DeleteBundleInfo(const messages::BundleInfo &bundle)
{
    (void)member_->bundleInfos.erase(bundle.bundleid());
    if (member_->proxyID2BundleIDs.find(bundle.functionproxyid()) != member_->proxyID2BundleIDs.end()) {
        member_->proxyID2BundleIDs[bundle.functionproxyid()].erase(bundle.bundleid());
        if (member_->proxyID2BundleIDs[bundle.functionproxyid()].empty()) {
            member_->proxyID2BundleIDs.erase(bundle.functionproxyid());
        }
    }
}

litebus::Future<Status> ResourceGroupManagerActor::ResourceGroupOperator::TxnResourceGroup(
    const std::shared_ptr<messages::ResourceGroupInfo> &req)
{
    ASSERT_IF_NULL(metaStoreClient_);
    auto key = RESOURCE_GROUP_KEY_PREFIX + "/" + req->tenantid() + "/" + req->name();
    YRLOG_INFO("begin to transaction resource group, key: {}", key);
    std::string jsonStr;
    if (!google::protobuf::util::MessageToJsonString(*req, &jsonStr).ok()) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR,
                      "failed to trans resource group to json string. name:" + req->name());
    }
    return metaStoreClient_->Put(key, jsonStr, {}).Then([req](const std::shared_ptr<PutResponse> &putResponse) {
        if (putResponse->status.IsError()) {
            return Status(StatusCode::ERR_ETCD_OPERATION_ERROR,
                          "failed to put resource group to etcd. name: " + req->name()
                              + ", err: " + putResponse->status.GetMessage());
        }
        return Status::OK();
    });
}

litebus::Future<std::shared_ptr<GetResponse>> ResourceGroupManagerActor::ResourceGroupOperator::SyncResourceGroups()
{
    YRLOG_INFO("begin to sync resource group info, key-prefix: {}", RESOURCE_GROUP_KEY_PREFIX);
    ASSERT_IF_NULL(metaStoreClient_);
    return metaStoreClient_->Get(RESOURCE_GROUP_KEY_PREFIX, { .prefix = true });
}

litebus::Future<Status> ResourceGroupManagerActor::ResourceGroupOperator::DeleteResourceGroup(
    const std::shared_ptr<messages::ResourceGroupInfo> &resourceGroup)
{
    ASSERT_IF_NULL(metaStoreClient_);
    auto key = RESOURCE_GROUP_KEY_PREFIX + "/" + resourceGroup->tenantid() + "/" + resourceGroup->name();
    YRLOG_INFO("begin to delete resource group, key :{}", resourceGroup->name());
    return metaStoreClient_->Delete(key, {}).Then(
        [name(resourceGroup->name())](
            const std::shared_ptr<DeleteResponse> &deleteResponse) -> litebus::Future<Status> {
            if (deleteResponse->status.IsError()) {
                return Status(StatusCode::ERR_ETCD_OPERATION_ERROR,
                              "failed to delete resource group from etcd. name:" + name
                                  + ", err: " + deleteResponse->status.GetMessage());
            }
            return Status::OK();
        });
}

void ResourceGroupManagerActor::TransCreateResourceGroupReq(std::shared_ptr<CreateResourceGroupRequest> &req)
{
    // empty rg's owner means that rg resource comes from primary ResourceView
    if (req->rgroupspec().owner().empty()) {
        req->mutable_rgroupspec()->set_owner(PRIMARY_TAG);
    }
}
}  // namespace functionsystem::resource_group_manager