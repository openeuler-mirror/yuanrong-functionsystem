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

#include "global_sched_actor.h"

#include <logs/api/provider.h>

#include <algorithm>
#include <async/asyncafter.hpp>
#include <async/defer.hpp>

#include "common/constants/actor_name.h"
#include "common/constants/metastore_keys.h"
#include "common/logs/logging.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::global_scheduler {

// The timeout interval is greater than the default timeout interval of MetaStoreClient.
constexpr int META_STORE_TIMEOUT = 65000;
constexpr int RETRY_PUT_TOPO_INTERVAL = 1000;

using namespace functionsystem::explorer;

GlobalSchedActor::GlobalSchedActor(const std::string &name, std::shared_ptr<MetaStoreClient> metaStoreClient,
                                   std::shared_ptr<DomainActivator> domainActivator,
                                   std::unique_ptr<Tree> &&topologyTree)
    : litebus::ActorBase(name),
      metaStoreClient_(std::move(metaStoreClient)),
      domainActivator_(std::move(domainActivator))
{
    member_ = std::make_shared<Member>();
    member_->topologyTree = std::move(topologyTree);
}

void GlobalSchedActor::Finalize()
{
    (void)domainActivator_->StopDomainSched();
}

void GlobalSchedActor::Init()
{
    YRLOG_DEBUG("init GlobalSchedActor");
    ASSERT_IF_NULL(member_);
    auto masterBusiness = std::make_shared<MasterBusiness>(shared_from_this(), member_);
    auto slaveBusiness = std::make_shared<SlaveBusiness>(shared_from_this(), member_);

    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);
    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);

    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        "GlobalSchedActor", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &GlobalSchedActor::UpdateLeaderInfo, leaderInfo);
        });

    curStatus_ = SLAVE_BUSINESS;
    business_ = slaveBusiness;
    member_->scalerAID.SetName(SCALER_ACTOR);
    member_->scalerAID.SetUrl(GetAID().Url());
    Receive("UpdateNodeTaintsResponse", &GlobalSchedActor::ResponseUpdateTaint);
    // slave --query resources info-> master
    Receive("QueryResourcesInfo", &GlobalSchedActor::QueryResourcesInfo);
    // master --resources info resp-> slave
    Receive("ResponseResourcesInfo", &GlobalSchedActor::ProcessResourcesInfo);
    // check function master running status
    Receive("CheckMasterStatus", &GlobalSchedActor::CheckMasterStatus);
    auto watchOpt = WatchOption{ false, false, 0, true };
    auto watch = [aid(GetAID())](const std::vector<WatchEvent> &event, bool) {
        litebus::Async(aid, &GlobalSchedActor::OnTopologyEvent, event);
        return true;
    };
    auto synced = [](const std::shared_ptr<GetResponse> &) -> litebus::Future<SyncResult> {
        return SyncResult{ Status::OK() };
    };
    (void)metaStoreClient_->Watch(SCHEDULER_TOPOLOGY, watchOpt, watch, synced);
}

void GlobalSchedActor::OnTopologyEvent(const std::vector<WatchEvent> &events)
{
    for (const auto &event : events) {
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                cacheTopo_ = event.kv.value();
                break;
            }
            case EVENT_TYPE_DELETE: {
                YRLOG_WARN("recevied delete topology event.");
                std::string().swap(cacheTopo_);
                break;
            }
        }
    }
}

void GlobalSchedActor::ResponseUpdateTaint(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->ResponseUpdateTaint(from, std::move(name), std::move(msg));
}

Node::TreeNode GlobalSchedActor::AddLocalSched(const std::string &name, const std::string &address)
{
    ASSERT_IF_NULL(member_->topologyTree);

    return member_->topologyTree->AddLeafNode({ name, address });
}

Node::TreeNode GlobalSchedActor::DelLocalSched(const std::string &name)
{
    ASSERT_IF_NULL(member_->topologyTree);

    return member_->topologyTree->RemoveLeafNode(name);
}

Node::TreeNode GlobalSchedActor::AddDomainSched(const std::string &name, const std::string &address)
{
    ASSERT_IF_NULL(member_->topologyTree);
    Node::TreeNode domainSched = nullptr;
    // If there has abnormal DomainScheduler node, the new registered DomainScheduler should replace it first.
    if (!abnormalDomainSched_.empty()) {
        auto abnormalNode = abnormalDomainSched_.front();
        YRLOG_INFO("replace abnormal domain scheduler node {}", abnormalNode);
        domainSched = member_->topologyTree->ReplaceNonLeafNode(abnormalNode, { name, address });
        abnormalDomainSched_.pop_front();
    }

    // If the abnormal node does not need to be replaced or the replacement fails.
    if (domainSched == nullptr) {
        YRLOG_INFO("add domain scheduler to topology tree, name: {}, address: {}", name, address);
        domainSched = member_->topologyTree->AddNonLeafNode({ name, address });
    }
    waitDomainToRegister_ = waitDomainToRegister_ > 0 ? waitDomainToRegister_ - 1 : 0;
    return domainSched;
}

Status GlobalSchedActor::RecoverFromString(const std::string &topologyInfo)
{
    ASSERT_IF_NULL(member_->topologyTree);
    if (member_->topologyTree->RecoverFromString(topologyInfo).IsError()) {
        YRLOG_ERROR("failed to recover the topology tree");
        return Status(StatusCode::GS_SCHED_TOPOLOGY_BROKEN);
    }
    return Status::OK();
}

void GlobalSchedActor::UpdateLastUpdatedTopology(const std::string &topologyInfo)
{
    lastUpdatedTopology_ = topologyInfo;
}

Node::TreeNode GlobalSchedActor::DelDomainSched(const std::string &name)
{
    ASSERT_IF_NULL(member_->topologyTree);

    const auto &domainSched = member_->topologyTree->FindNonLeafNode(name);
    if (domainSched == nullptr) {
        YRLOG_WARN("didn't find the domain scheduler {}, can't delete it", name);
        return nullptr;
    }

    // Set the state of abnormal domain scheduler node to BROKEN.
    // And activate a new domain scheduler, wait the domain scheduler to register and replace the abnormal one.
    member_->topologyTree->SetState(domainSched, NodeState::BROKEN);

    // The abnormal scheduler may have been deleted once before but has not been replaced. After the exception times
    // out, the scheduler is deleted again.
    if (std::find(abnormalDomainSched_.begin(), abnormalDomainSched_.end(), name) == abnormalDomainSched_.end()) {
        YRLOG_INFO("add abnormal domain scheduler, name: {}", name);
        abnormalDomainSched_.push_back(name);
    }

    // If failed to activate a new domain scheduler, the abnormal domain scheduler would be detected and delete it
    // again. Then try again to activate the new scheduler and replace the abnormal scheduler.
    hasActivateDomain_ = true;
    if (domainActivator_->StartDomainSched().IsError()) {
        YRLOG_ERROR("failed to activate domain scheduler");
    } else {
        waitDomainToRegister_ += 1;
    }
    return domainSched;
}

Status GlobalSchedActor::RecoverSchedTopology()
{
    ASSERT_IF_NULL(member_->topologyTree);
    if (!cacheTopo_.empty()) {
        YRLOG_INFO("recover scheduler topology tree from cache");
        return RecoverFromString(cacheTopo_);
    }
    YRLOG_INFO("recover scheduler topology tree from MetaStore");
    auto schedulerTopoOpt = metaStoreClient_->Get(SCHEDULER_TOPOLOGY, {}).Get(META_STORE_TIMEOUT);
    if (schedulerTopoOpt.IsNone()) {
        YRLOG_ERROR("failed to get topology info from MetaStore");
        return Status(StatusCode::GS_GET_FROM_METASTORE_FAILED);
    }

    auto schedulerTopo = schedulerTopoOpt.Get();
    if (schedulerTopo->status.IsError()) {
        YRLOG_ERROR("failed to get topology info from MetaStore");
        return Status(StatusCode::GS_GET_FROM_METASTORE_FAILED);
    }
    // If GlobalScheduler is started for the first time, would get nothing from MetaStore.
    if (schedulerTopo->kvs.empty()) {
        YRLOG_INFO("no topology info stored in MetaStore");
        return Status::OK();
    }
    const auto &topologyInfo = schedulerTopo->kvs.front().value();
    return RecoverFromString(topologyInfo);
}

bool GlobalSchedActor::DomainHasActivated()
{
    return hasActivateDomain_;
}

Node::TreeNode GlobalSchedActor::FindRootDomainSched()
{
    ASSERT_IF_NULL(business_);
    return business_->FindRootDomainSched();
}

litebus::Future<Status> GlobalSchedActor::UpdateSchedTopology()
{
    // topo is putting to metastore and no other update is waiting to update
    // Create a new promise to wait for the next update (executed after the request that is being updated is returned).
    if (persisting_ != nullptr && waitToPersistence_ == nullptr) {
        waitToPersistence_ = std::make_shared<litebus::Promise<Status>>();
        return waitToPersistence_->GetFuture();
    }
    // topo is putting to metastore and other update is waiting to update
    // Merge with currently pending updates
    if (waitToPersistence_ != nullptr) {
        return waitToPersistence_->GetFuture();
    }
    persisting_ = std::make_shared<litebus::Promise<Status>>();
    auto future = persisting_->GetFuture();
    PutTopology();
    return future;
}

void GlobalSchedActor::PutTopology()
{
    ASSERT_IF_NULL(member_->topologyTree);
    auto topologyInfo = member_->topologyTree->SerializeAsString();
    if (lastUpdatedTopology_ == topologyInfo) {
        YRLOG_INFO("same topology info has been saved into MetaStore");
        OnTopologyPut(Status::OK());
        return;
    }
    (void)metaStoreClient_->Put(SCHEDULER_TOPOLOGY, topologyInfo, {})
        .Then([aid(GetAID()), topologyInfo](const std::shared_ptr<PutResponse> &putResponse) {
            if (putResponse->status.IsError()) {
                YRLOG_ERROR("failed to save scheduler topology to MetaStore");
                return putResponse->status;
            } else {
                litebus::Async(aid, &GlobalSchedActor::UpdateLastUpdatedTopology, topologyInfo);
                return Status::OK();
            }
        })
        .OnComplete(litebus::Defer(GetAID(), &GlobalSchedActor::OnTopologyPut, std::placeholders::_1));
}

void GlobalSchedActor::OnTopologyPut(const litebus::Future<Status> &future)
{
    if (future.IsError() || future.Get().IsError()) {
        YRLOG_WARN("failed to persist topology, retry to put.");
        // Failure to retry
        litebus::AsyncAfter(RETRY_PUT_TOPO_INTERVAL, GetAID(), &GlobalSchedActor::PutTopology);
    }
    if (persisting_ != nullptr) {
        persisting_->SetValue(Status::OK());
        persisting_ = nullptr;
    }
    if (waitToPersistence_ == nullptr) {
        return;
    }
    // ready to update new topology
    persisting_ = waitToPersistence_;
    waitToPersistence_ = nullptr;
    PutTopology();
}

Status GlobalSchedActor::CacheLocalSched(const litebus::AID &from, const std::string &name, const std::string &address)
{
    // Push the local scheduler info to a cache queue and wait a new domain scheduler to register.
    std::pair<std::string, NodeInfo> localSched = { from, { name, address } };
    ASSERT_IF_NULL(member_->topologyTree);
    if (member_->topologyTree->FindLeafNode(name) != nullptr) {
        YRLOG_INFO("local scheduler[name: {}] already in topology tree, can't add it to cache queue", name);
        return Status(StatusCode::FAILED);
    }

    bool newCachedLocal = false;
    auto iter = std::find_if(
        cachedLocalSched_.begin(), cachedLocalSched_.end(),
        [localSched](const std::pair<std::string, NodeInfo> &cached) { return cached.first == localSched.first; });
    if (iter == cachedLocalSched_.end()) {
        YRLOG_INFO("put local scheduler[name: {}, address: {}] into cache queue", name, address);
        cachedLocalSched_.push_back(localSched);
        newCachedLocal = true;
    }

    if (waitDomainToRegister_ > 0) {
        YRLOG_INFO("wait domain to register");
        return Status::OK();
    }

    // If there is no waiting scheduler to register, activate a new DomainScheduler.
    YRLOG_INFO("activate a new domain scheduler");
    hasActivateDomain_ = true;
    if (auto status(domainActivator_->StartDomainSched()); status.IsError()) {
        // If failed to activate domain scheduler, the local scheduler would fail to register.
        YRLOG_ERROR("failed to activate domain scheduler, error: {}", status.ToString());
        if (newCachedLocal) {
            cachedLocalSched_.pop_back();
        } else {
            cachedLocalSched_.erase(iter);
        }
        return Status(StatusCode::GS_ACTIVATE_DOMAIN_FAILED);
    }
    // If success to activate a new DomainScheduler,
    // wait until the DomainScheduler is registered successfully, then add the local scheduler in the queue again.
    waitDomainToRegister_ += 1;
    return Status::OK();
}

std::unordered_map<std::string, Node::TreeNode> GlobalSchedActor::FindNodes(const uint64_t level)
{
    ASSERT_IF_NULL(member_->topologyTree);
    return member_->topologyTree->FindNodes(level);
}

litebus::Option<std::string> GlobalSchedActor::GetLocalAddress(const std::string &name)
{
    ASSERT_IF_NULL(member_->topologyTree);
    auto localPair = member_->topologyTree->FindLeafNode(name);
    if (member_->topologyTree->FindLeafNode(name) == nullptr) {
        YRLOG_ERROR("failed to find local scheduler({}) in global", name);
        return litebus::None();
    }
    return localPair->GetNodeInfo().address;
}

litebus::Option<NodeInfo> GlobalSchedActor::GetRootDomainInfo()
{
    ASSERT_IF_NULL(member_->topologyTree);
    auto root = member_->topologyTree->GetRootNode();
    if (root == nullptr) {
        YRLOG_ERROR("failed to find root domain in global");
        return litebus::None();
    }
    return root->GetNodeInfo();
}

void GlobalSchedActor::AddLocalSchedHandler(const litebus::AID &from, const std::string &name,
                                            const std::string &address)
{
    checkLocalAbnormalCallbackFunc_(name).OnComplete(
        litebus::Defer(GetAID(), &GlobalSchedActor::AddLocalSched, std::placeholders::_1, from, name, address));
}

void GlobalSchedActor::AddLocalSched(const litebus::Future<bool> &isLocalAbnormal, const litebus::AID &from,
                                     const std::string &name, const std::string &address)
{
    if (isLocalAbnormal.IsError() || isLocalAbnormal.Get()) {
        YRLOG_ERROR("failed to register, local({}) is abnormal", name);
        return;
    }

    YRLOG_INFO("add local scheduler name: {}, address: {}", name, address);
    // Add LocalScheduler to the Scheduler Tree.
    auto localSched = AddLocalSched(name, address);
    // If added successfully, update the topology view of the leader of the local scheduler, and return the registered
    // message to the local scheduler.
    ASSERT_IF_NULL(member_->domainSchedMgr);
    ASSERT_IF_NULL(member_->topologyTree);
    ASSERT_IF_NULL(localSchedMgr_);
    if (localSched != nullptr && localSched->GetParent() != nullptr) {
        const auto &leader = localSched->GetParent();
        YRLOG_INFO("add local scheduler {}-{} to domain scheduler {}-{}", name, address, leader->GetNodeInfo().name,
                   leader->GetNodeInfo().address);
        messages::ScheduleTopology schedTopology = leader->GetTopologyView();
        const auto &domainInfo = leader->GetNodeInfo();
        member_->domainSchedMgr->UpdateSchedTopoView(domainInfo.name, domainInfo.address, schedTopology);
        schedTopology = localSched->GetTopologyView();
        localSchedMgr_->Registered(from, schedTopology);
        // Save the topology view to MetaStore.
        // future should consider the
        (void)UpdateSchedTopology();
        if (localAddCallback_ != nullptr) {
            localAddCallback_(name);
        }
        return;
    }
    // If localSched is nullptr, means no DomainScheduler is available in topology tree, the number of domain schedulers
    // needs to be dynamically expanded. Cache the info of LocalScheduler register into a queue. If failed, means that
    // new domain scheduler can not be activated. Return registered failed message to the local scheduler. Dynamically
    // expanding capabilities depends on the function of starting processes on Scaler. Currently, this function is not
    // provided.
    auto status = CacheLocalSched(from, name, address);
    if (status.IsError()) {
        YRLOG_ERROR("failed to add local scheduler name: {}, address: {}", name, address);
        localSchedMgr_->Registered(from, litebus::None());
    }
}

void GlobalSchedActor::DelLocalSchedHandler(const std::string &name, LocalExitType exitType)
{
    YRLOG_INFO("delete local scheduler name: {} type: {}", name, static_cast<int>(exitType));
    ASSERT_IF_NULL(localSchedMgr_);
    auto address = GetLocalAddress(name);
    if (address.IsSome()) {
        localSchedMgr_->OnLocalAbnormal(name, address.Get());
    }
    auto leader = DelLocalSched(name);
    ASSERT_IF_NULL(member_->domainSchedMgr);
    if (leader == nullptr) {
        YRLOG_ERROR("failed to delete local scheduler {}, didn't find its leader scheduler", name);
        // The parent scheduler of the local scheduler may not receive the message indicating that the local node is
        // deleted. All domain schedulers that manage the local schedulers need to be notified again.
        auto leaders = FindNodes(1);
        for (auto &[nodeName, node] : leaders) {
            YRLOG_DEBUG("update the topology view of domain {}", nodeName);
            const auto &domainInfo = node->GetNodeInfo();
            member_->domainSchedMgr->UpdateSchedTopoView(domainInfo.name, domainInfo.address, node->GetTopologyView());
        }
        return;
    }

    // Update the topology view of the leader scheduler of the deleted local scheduler.
    const auto &domainInfo = leader->GetNodeInfo();
    member_->domainSchedMgr->UpdateSchedTopoView(domainInfo.name, domainInfo.address, leader->GetTopologyView());
    if (leader->GetChildren().empty()) {
        // If a DomainScheduler does not have sub-scheduler to be managed,
        // the DomainScheduler needs to be deleted with a delay.
        // To implement.
        YRLOG_INFO("domain scheduler {} has no sub-scheduler", leader->GetNodeInfo().name);
    }
    switch (exitType) {
        case LocalExitType::ABNORMAL: {
            return OnLocalAbnormal(name);
        }
        case LocalExitType::UNREGISTER: {
            return OnLocalExit(name);
        }
    }
}

void GlobalSchedActor::OnLocalExit(const std::string &name)
{
    (void)UpdateSchedTopology();
    if (localDeleteCallback_ == nullptr) {
        return;
    }
    localDeleteCallback_(name);
}

void GlobalSchedActor::OnLocalAbnormal(const std::string &name)
{
    if (localSchedAbnormalCallback_ == nullptr) {
        YRLOG_WARN("failed to execute local scheduler abnormal callback, callback func is null");
        (void)UpdateSchedTopology();
    } else {
        (void)localSchedAbnormalCallback_(name).OnComplete(
            litebus::Defer(GetAID(), &GlobalSchedActor::UpdateSchedTopology));
    }
    for (const auto &callBack : localSchedAbnormalNotifyCallbacks_) {
        if (callBack.second != nullptr) {
            callBack.second(name);
        }
    }
}

void GlobalSchedActor::AddDomainSchedHandler(const litebus::AID &from, const std::string &name,
                                             const std::string &address)
{
    YRLOG_INFO("add domain scheduler name: {}, address: {}", name, address);

    Node::TreeNode domainSched = AddDomainSched(name, address);
    ASSERT_IF_NULL(member_->domainSchedMgr);
    if (domainSched == nullptr) {
        YRLOG_WARN("failed to add domain scheduler name: {}, address: {}", name, address);
        member_->domainSchedMgr->Registered(from, litebus::None());
        return;
    }
    if (domainSched->GetNodeInfo().address != address) {
        YRLOG_WARN("failed to add domain scheduler name: {}, address: {}, already exist one {}", name, address,
                   domainSched->GetNodeInfo().address);
        member_->domainSchedMgr->Registered(from, litebus::None());
        return;
    }
    YRLOG_INFO("succeed to add domain scheduler name: {}, address: {}", name, address);

    messages::ScheduleTopology schedTopology;
    const auto &leader = domainSched->GetParent();
    if (leader == nullptr) {
        // The newly added DomainScheduler becomes root DomainScheduler.
        member_->domainSchedMgr->Disconnect();
        YRLOG_INFO("connect to new root DomainScheduler {}", domainSched->GetNodeInfo().name);
        (void)member_->domainSchedMgr->Connect(domainSched->GetNodeInfo().name, domainSched->GetNodeInfo().address);
    } else {
        // If the newly added DomainScheduler has parent node, update its parent node's topology view.
        YRLOG_INFO("add domain scheduler {}-{} to domain scheduler {}-{}", name, address, leader->GetNodeInfo().name,
                   leader->GetNodeInfo().address);
        schedTopology = leader->GetTopologyView();
        const auto &domainInfo = leader->GetNodeInfo();
        member_->domainSchedMgr->UpdateSchedTopoView(domainInfo.name, domainInfo.address, schedTopology);
    }

    // Return registered message to the newly added DomainScheduler.
    schedTopology = domainSched->GetTopologyView();
    member_->domainSchedMgr->Registered(from, schedTopology);
    // Update the topology view of the children node of the newly added DomainScheduler.
    for (const auto &[nodeName, childNode] : domainSched->GetChildren()) {
        YRLOG_INFO("scheduler {} parent node changes to {}", nodeName, domainSched->GetNodeInfo().name);
        schedTopology = childNode->GetTopologyView();
        if (childNode->IsLeaf()) {
            ASSERT_IF_NULL(localSchedMgr_);
            localSchedMgr_->UpdateSchedTopoView(childNode->GetNodeInfo().address, schedTopology);
        } else {
            member_->domainSchedMgr->UpdateSchedTopoView(childNode->GetNodeInfo().name,
                                                         childNode->GetNodeInfo().address, schedTopology);
        }
    }

    (void)UpdateSchedTopology();

    // Get local schedulers in the cache queue and add them to the topology tree again.
    while (!cachedLocalSched_.empty()) {
        auto &localSchedToAdd = cachedLocalSched_.front();
        AddLocalSchedHandler(localSchedToAdd.first, localSchedToAdd.second.name, localSchedToAdd.second.address);
        cachedLocalSched_.pop_front();
    }
}

void GlobalSchedActor::DelDomainSchedHandler(const std::string &name)
{
    YRLOG_INFO("delete domain scheduler name: {}", name);

    auto brokenDomainSched = DelDomainSched(name);
    if (brokenDomainSched == nullptr) {
        return;
    }

    YRLOG_INFO("domain scheduler {} is waiting to be replaced", name);
    // The topology view of the upstream and downstream schedulers of the deleted DomainScheduler does not need to
    // be updated now.
    // The topology view will be updated after the deleted domain scheduler is replaced by a new DomainScheduler.

    // The topology information stored in MetaStore needs to be updated. Because the state of the scheduler has changed.
    (void)UpdateSchedTopology();
}

void GlobalSchedActor::UpdateNodeTaintsHandler(const std::string &ip, const std::string &key, const bool healthy)
{
    messages::UpdateNodeTaintRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_key(key);
    req.set_healthy(healthy);
    req.set_ip(ip);
    YRLOG_INFO("{}|send update node({}) taints({}) healthy({}) request", req.requestid(), req.ip(), req.key(),
               req.healthy());
    Send(member_->scalerAID, "UpdateNodeTaints", req.SerializeAsString());
}

void GlobalSchedActor::BindDomainSchedMgr(const std::shared_ptr<DomainSchedMgr> &domainSchedMgr)
{
    member_->domainSchedMgr = domainSchedMgr;
}

void GlobalSchedActor::BindLocalSchedMgr(const std::shared_ptr<LocalSchedMgr> &localSchedMgr)
{
    localSchedMgr_ = localSchedMgr;
}

void GlobalSchedActor::BindLocalSchedAbnormalCallback(const LocalSchedAbnormalCallbackFunc &func)
{
    localSchedAbnormalCallback_ = func;
}

void GlobalSchedActor::BindCheckLocalAbnormalCallback(const CheckLocalAbnormalCallbackFunc &func)
{
    checkLocalAbnormalCallbackFunc_ = func;
}

void GlobalSchedActor::AddLocalSchedAbnormalNotifyCallback(const std::string &name,
                                                           const LocalSchedAbnormalCallbackFunc &func)
{
    localSchedAbnormalNotifyCallbacks_[name] = func;
}

void GlobalSchedActor::BindLocalDeleteCallback(const LocalDeleteCallbackFunc &func)
{
    localDeleteCallback_ = func;
}

void GlobalSchedActor::BindLocalAddCallback(const LocalAddCallbackFunc &func)
{
    localAddCallback_ = func;
}

litebus::Future<Status> GlobalSchedActor::DoSchedule(const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto rootDomain = FindRootDomainSched();
    if (rootDomain == nullptr) {
        YRLOG_ERROR("{}|root domain not exist, can't schedule instance({}).", req->requestid(),
                    req->instance().instanceid());
        return Status(StatusCode::FAILED);
    }
    ASSERT_IF_NULL(member_->domainSchedMgr);
    return member_->domainSchedMgr->Schedule(rootDomain->GetNodeInfo().name, rootDomain->GetNodeInfo().address, req);
}

litebus::Future<messages::GroupResponse> GlobalSchedActor::GroupSchedule(
    const std::shared_ptr<messages::GroupInfo> &groupInfo, uint32_t retryCycle)
{
    YRLOG_DEBUG("{}|start to forward group schedule for rg({}), groupName({})", groupInfo->requestid(),
                groupInfo->rgroupname(), groupInfo->groupid());
    auto promise = std::make_shared<litebus::Promise<messages::GroupResponse>>();
    DoGroupSchedule(groupInfo, promise, retryCycle);
    return promise->GetFuture();
}

void GlobalSchedActor::DoGroupSchedule(
    const std::shared_ptr<messages::GroupInfo> &groupInfo,
    const std::shared_ptr<litebus::Promise<messages::GroupResponse>> &promise, uint32_t retryCycle)
{
    auto rootDomain = FindRootDomainSched();
    if (rootDomain == nullptr) {
        YRLOG_ERROR("{}|root domain not exist, can't schedule group({}). defer ({}) to retry", groupInfo->requestid(),
                    groupInfo->groupid());
        litebus::AsyncAfter(retryCycle, GetAID(), &GlobalSchedActor::DoGroupSchedule, groupInfo, promise, retryCycle);
        return;
    }
    ASSERT_IF_NULL(member_->domainSchedMgr);
    member_->domainSchedMgr->GroupSchedule(rootDomain->GetNodeInfo().name, rootDomain->GetNodeInfo().address, groupInfo)
        .OnComplete(
            [retryCycle, promise, groupInfo, aid(GetAID())](const litebus::Future<messages::GroupResponse> &future) {
                if (future.IsError()) {
                    YRLOG_WARN("{}|{}|forward schedule request for resource group({}), request timeout.",
                               groupInfo->traceid(), groupInfo->requestid(), groupInfo->rgroupname());
                    litebus::Async(aid, &GlobalSchedActor::DoGroupSchedule, groupInfo, promise, retryCycle);
                    return;
                }
                promise->SetValue(future.Get());
            });
}

void GlobalSchedActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    litebus::AID masterAID(GLOBAL_SCHED_ACTOR_NAME, leaderInfo.address);
    business_->UpdateLeaderInfo(leaderInfo);
    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("new status({}) business don't exist for GlobalSchedActor", newStatus);
        return;
    }
    business_ = businesses_[newStatus];
    business_->UpdateLeaderInfo(leaderInfo);
    business_->OnChange();
    curStatus_ = newStatus;
    ASSERT_IF_NULL(member_->domainSchedMgr);
    member_->domainSchedMgr->UpdateLeaderInfo(leaderInfo);
    ASSERT_IF_NULL(localSchedMgr_);
    localSchedMgr_->UpdateLeaderInfo(leaderInfo);
}

litebus::Future<Status> GlobalSchedActor::EvictAgent(const std::string &localID,
                                                     const std::shared_ptr<messages::EvictAgentRequest> &req)
{
    auto address = GetLocalAddress(localID);
    if (address.IsNone()) {
        YRLOG_ERROR("failed to evict agent({}), reason: local({}) not found", req->agentid(), localID);
        return Status(StatusCode::PARAMETER_ERROR, "Invalid agentID");
    }
    ASSERT_IF_NULL(localSchedMgr_);
    return localSchedMgr_->EvictAgentOnLocal(address.Get(), req);
}

litebus::Future<messages::QueryAgentInfoResponse> GlobalSchedActor::QueryAgentInfo(
    const std::shared_ptr<messages::QueryAgentInfoRequest> &req)
{
    auto rootDomain = FindRootDomainSched();
    if (rootDomain == nullptr) {
        YRLOG_ERROR("root domain not exist, can't query agentinfo.");
        return messages::QueryAgentInfoResponse{};
    }
    ASSERT_IF_NULL(member_->domainSchedMgr);
    return member_->domainSchedMgr->QueryAgentInfo(rootDomain->GetNodeInfo().name, rootDomain->GetNodeInfo().address,
                                                   req);
}

litebus::Future<messages::QueryInstancesInfoResponse> GlobalSchedActor::GetSchedulingQueue(
    const std::shared_ptr<messages::QueryInstancesInfoRequest> &req)
{
    auto rootDomain = FindRootDomainSched();
    if (rootDomain == nullptr) {
        YRLOG_ERROR("root domain not exist, can't GetSchedulingQueue.");
        return messages::QueryInstancesInfoResponse{};
    }

    ASSERT_IF_NULL(member_->domainSchedMgr);
    return member_->domainSchedMgr->GetSchedulingQueue(rootDomain->GetNodeInfo().name,
                                                       rootDomain->GetNodeInfo().address, req);
}

void GlobalSchedActor::QueryResourcesInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::QueryResourcesInfoRequest>();
    if (msg.empty() || !req->ParseFromString(msg)) {
        YRLOG_WARN("invalid QueryResourcesInfoRequest {}", msg);
        return;
    }
    (void)HandleQueryResourcesInfo(req).OnComplete(
        litebus::Defer(GetAID(), &GlobalSchedActor::OnQueryResourcesInfo, std::placeholders::_1, from));
}

void GlobalSchedActor::CheckMasterStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_DEBUG("received check master status request from {}", from.HashString());
    Send(from, "CheckedMasterStatus", std::move(msg));
}

void GlobalSchedActor::OnQueryResourcesInfo(const litebus::Future<messages::QueryResourcesInfoResponse> &future,
                                            const litebus::AID &to)
{
    if (future.IsError()) {
        YRLOG_WARN("failed to query resources info");
        Send(to, "ResponseResourcesInfo", "");
        return;
    }
    auto queryResp = future.Get();
    Send(to, "ResponseResourcesInfo", queryResp.SerializeAsString());
}

void GlobalSchedActor::ProcessResourcesInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto resp = messages::QueryResourcesInfoResponse();
    if (msg.empty() || !resp.ParseFromString(msg)) {
        YRLOG_WARN("invalid QueryResourcesInfoResponse {}", msg);
        return;
    }
    business_->HandleResourceInfoResponse(resp);
}

litebus::Future<messages::QueryResourcesInfoResponse> GlobalSchedActor::HandleQueryResourcesInfo(
    const std::shared_ptr<messages::QueryResourcesInfoRequest> &req)
{
    return business_->QueryResourcesInfo(req);
}

void GlobalSchedActor::OnHealthyStatus(const Status &status)
{
    // Prevent the topology from being overwritten during active/standby switching.
    if (topoRecovered.GetFuture().IsInit()) {
        YRLOG_WARN("topo is not recovered, ignore it");
        return;
    }
    business_->OnHealthyStatus(status);
}

void GlobalSchedActor::SetTopoRecovered(bool isRecovered)
{
    if (isRecovered) {
        topoRecovered.SetValue(isRecovered);
        return;
    }
    topoRecovered = litebus::Promise<bool>();
}

litebus::Future<std::unordered_set<std::string>> GlobalSchedActor::QueryNodes()
{
    auto future = topoRecovered.GetFuture();
    if (future.IsInit()) {
        YRLOG_WARN("topology is not recovered, defer to query.");
        return future.Then(litebus::Defer(GetAID(), &GlobalSchedActor::QueryNodes));
    }
    std::unordered_set<std::string> nodes;
    ASSERT_IF_NULL(member_->topologyTree);
    auto leafNodes = member_->topologyTree->FindNodes(0);
    for (auto node : leafNodes) {
        nodes.insert(node.first);
    }
    return nodes;
}

void GlobalSchedActor::MasterBusiness::OnChange()
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->SetTopoRecovered(true);
    YRLOG_INFO("aid({}) change status to master", std::string(actor->GetAID()));
    if (auto status(actor->RecoverSchedTopology()); status.IsError()) {
        YRLOG_ERROR("failed to recover scheduler topology view");
        return;
    }
    auto rootDomain = actor->FindRootDomainSched();
    if (rootDomain) {
        if (!actor->DomainHasActivated() && rootDomain->GetNodeInfo().address == actor->GetAID().Url()) {
            YRLOG_INFO("domain has not activated and root domain is self, delete root domain");
            (void)actor->DelDomainSched(rootDomain->GetNodeInfo().name);
            return;
        }
        YRLOG_INFO("topo have root domain, try to connect");
        ASSERT_IF_NULL(member_->domainSchedMgr);
        (void)member_->domainSchedMgr->Connect(rootDomain->GetNodeInfo().name, rootDomain->GetNodeInfo().address);
    }
}

Node::TreeNode GlobalSchedActor::MasterBusiness::FindRootDomainSched()
{
    ASSERT_IF_NULL(member_->topologyTree);
    return member_->topologyTree->GetRootNode();
}

void GlobalSchedActor::MasterBusiness::ResponseUpdateTaint(const litebus::AID &from, std::string &&name,
                                                           std::string &&msg)
{
    messages::UpdateNodeTaintResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_WARN("update node taint response it invalid");
        return;
    }
    YRLOG_INFO("{}|receive update taint response message from: {}, name: {}", rsp.requestid(), std::string(from), name);
}

void GlobalSchedActor::MasterBusiness::OnHealthyStatus(const Status &status)
{
    if (!status.IsOk()) {
        return;
    }
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    YRLOG_INFO("metastore is recovered, refresh the topology");
    (void)actor->UpdateSchedTopology();
}

litebus::Future<messages::QueryResourcesInfoResponse> GlobalSchedActor::MasterBusiness::QueryResourcesInfo(
    const std::shared_ptr<messages::QueryResourcesInfoRequest> &req)
{
    ASSERT_IF_NULL(member_->domainSchedMgr);
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    auto rootDomain = actor->FindRootDomainSched();
    if (rootDomain == nullptr) {
        YRLOG_ERROR("root domain not exist, can't query resource info.");
        return messages::QueryResourcesInfoResponse{};
    }
    YRLOG_INFO("{}|master received a request to query resource info.", req->requestid());
    return member_->domainSchedMgr->QueryResourcesInfo(rootDomain->GetNodeInfo().name,
                                                       rootDomain->GetNodeInfo().address, req);
}

void GlobalSchedActor::MasterBusiness::HandleResourceInfoResponse(const messages::QueryResourcesInfoResponse &rsp)
{
}

void GlobalSchedActor::MasterBusiness::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    leaderInfo_ = leaderInfo;
}

void GlobalSchedActor::SlaveBusiness::OnChange()
{
    ASSERT_IF_NULL(member_->domainSchedMgr);
    YRLOG_INFO("change status to slave, disconnect to domain scheduler");
    member_->domainSchedMgr->Disconnect();
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->SetTopoRecovered(false);
}
Node::TreeNode GlobalSchedActor::SlaveBusiness::FindRootDomainSched()
{
    return Node::TreeNode();
}

void GlobalSchedActor::SlaveBusiness::ResponseUpdateTaint(const litebus::AID &, std::string &&, std::string &&)
{
}

litebus::Future<messages::QueryResourcesInfoResponse> GlobalSchedActor::SlaveBusiness::QueryResourcesInfo(
    const std::shared_ptr<messages::QueryResourcesInfoRequest> &req)
{
    if (queryResourcesInfoPromise_) {
        YRLOG_INFO("{}|another resource query is in progress.", req->requestid());
        return queryResourcesInfoPromise_->GetFuture();
    }
    YRLOG_INFO("{}|slave received a request to query resource info.", req->requestid());
    queryResourcesInfoPromise_ = std::make_shared<litebus::Promise<messages::QueryResourcesInfoResponse>>();
    auto future = queryResourcesInfoPromise_->GetFuture();
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->Send(litebus::AID(GLOBAL_SCHED_ACTOR_NAME, leaderInfo_.address), "QueryResourcesInfo",
                req->SerializeAsString());
    return future;
}

void GlobalSchedActor::SlaveBusiness::HandleResourceInfoResponse(const messages::QueryResourcesInfoResponse &rsp)
{
    if (queryResourcesInfoPromise_ == nullptr) {
        YRLOG_WARN("{}|No task exists for querying resource information.", rsp.requestid());
        return;
    }
    YRLOG_DEBUG("{}|slave received a response from the master for querying resource info.", rsp.requestid());
    queryResourcesInfoPromise_->SetValue(rsp);
    queryResourcesInfoPromise_ = nullptr;
}

void GlobalSchedActor::SlaveBusiness::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    leaderInfo_ = leaderInfo;
}

}  // namespace functionsystem::global_scheduler