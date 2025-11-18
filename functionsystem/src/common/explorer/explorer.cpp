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

#include "explorer.h"

#include <async/async.hpp>

#include "common/status/status.h"
#include "etcd_explorer_actor.h"
#include "k8s_explorer_actor.h"
#include "txn_explorer_actor.h"

namespace functionsystem::explorer {
void Explorer::BindExplorerActor(const std::string &electionKey, const std::shared_ptr<ExplorerActor> &explorerActor)
{
    RETURN_IF_NULL(explorerActor);
    std::lock_guard<std::mutex> guard(lock_);
    YRLOG_INFO("Bind explorer actor on {}", electionKey);
    explorers_[electionKey] = explorerActor;
}

void Explorer::UnbindExplorerActor(const std::string &electionKey)
{
    std::lock_guard<std::mutex> guard(lock_);
    YRLOG_INFO("Unbind explorer actor on {}", electionKey);
    (void)explorers_.erase(electionKey);
}

[[maybe_unused]] std::shared_ptr<ExplorerActor> Explorer::NewStandAloneExplorerActorForMaster(
    const ElectionInfo &electionInfo, const LeaderInfo &leaderInfo)
{
    litebus::Option<LeaderInfo> leaderInfoOpt(leaderInfo);
    auto actor = std::make_shared<EtcdExplorerActor>(leaderInfo.name, electionInfo, leaderInfoOpt, nullptr);
    (void)litebus::Spawn(actor);
    Explorer::GetInstance().BindExplorerActor(leaderInfo.name, actor);
    return actor;
}

[[maybe_unused]] void Explorer::NewEtcdExplorerActorForMaster(const std::string &electionKey,
                                                              const ElectionInfo &electionInfo,
                                                              const std::shared_ptr<MetaStoreClient> &metaStoreClient)
{
    litebus::Option<LeaderInfo> leaderInfoOpt;
    auto actor = std::make_shared<EtcdExplorerActor>(electionKey, electionInfo, leaderInfoOpt, metaStoreClient);
    (void)litebus::Spawn(actor);
    Explorer::GetInstance().BindExplorerActor(electionKey, actor);
}

[[maybe_unused]] void Explorer::NewTxnExplorerActorForMaster(const std::string &electionKey,
                                                             const ElectionInfo &electionValue,
                                                             const std::shared_ptr<MetaStoreClient> &metaStoreClient)
{
    auto actor =
        std::make_shared<TxnExplorerActor>(electionKey, electionValue, litebus::Option<LeaderInfo>{}, metaStoreClient);
    (void)litebus::Spawn(actor);
    Explorer::GetInstance().BindExplorerActor(electionKey, actor);
}

void Explorer::NewK8sExplorerActorForMaster(const std::string &electionKey, const ElectionInfo &electionInfo,
                                            const std::string &k8sNamespace,
                                            const std::shared_ptr<KubeClient> &kubeClient)
{
    litebus::Option<LeaderInfo> leaderInfoOpt;
    auto actor = std::make_shared<K8sExplorerActor>(electionKey, electionInfo, leaderInfoOpt, kubeClient, k8sNamespace);
    (void)litebus::Spawn(actor);
    Explorer::GetInstance().BindExplorerActor(electionKey, actor);
}

[[maybe_unused]] bool Explorer::CreateExplorer(const ElectionInfo &electionInfo, const LeaderInfo &leaderInfo,
                                               const std::shared_ptr<MetaStoreClient> &metaClient,
                                               const std::shared_ptr<KubeClient> &kubeClient,
                                               const std::string &k8sNamespace)
{
    YRLOG_INFO("create explore, election mode: {}", electionInfo.mode);
    if (electionInfo.mode == STANDALONE_MODE) {
        explorer::Explorer::NewStandAloneExplorerActorForMaster(electionInfo, leaderInfo);
    } else if (electionInfo.mode == ETCD_ELECTION_MODE) {
        explorer::Explorer::NewEtcdExplorerActorForMaster(leaderInfo.name, electionInfo, metaClient);
    } else if (electionInfo.mode == TXN_ELECTION_MODE) {
        explorer::Explorer::NewTxnExplorerActorForMaster(leaderInfo.name, electionInfo, metaClient);
    } else if (electionInfo.mode == K8S_ELECTION_MODE) {
        explorer::Explorer::NewK8sExplorerActorForMaster(leaderInfo.name, electionInfo, k8sNamespace, kubeClient);
    } else {
        return false;
    }
    return true;
}

[[maybe_unused]] std::shared_ptr<ExplorerActor> Explorer::GetExplorer(const std::string &key)
{
    std::lock_guard<std::mutex> guard(lock_);
    if (explorers_.find(key) == explorers_.end()) {
        return nullptr;
    }
    return explorers_[key];
}

void Explorer::Clear()
{
    std::lock_guard<std::mutex> guard(lock_);
    for (auto &actorIt : explorers_) {
        auto aid = actorIt.second->GetAID();
        litebus::Terminate(aid);
        litebus::Await(aid);
    }
    explorers_.clear();
    cachedLeadersInfo_.clear();
}

Status Explorer::AddLeaderChangedCallback(const std::string &cbIdentifier, const CallbackFuncLeaderChange &cbFunc)
{
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto &explorer : explorers_) {
        YRLOG_INFO("register leader change callback on {}, callback identifier: {}", explorer.first, cbIdentifier);
        litebus::Async(explorer.second->GetAID(), &ExplorerActor::RegisterLeaderChangedCallback, cbIdentifier, cbFunc);
    }
    return Status::OK();
}

Status Explorer::RemoveLeaderChangedCallback(const std::string &cbIdentifier)
{
    std::lock_guard<std::mutex> guard(lock_);
    for (const auto &explorer : explorers_) {
        YRLOG_INFO("unregister leader change callback on {}, callback identifier: {}", explorer.first, cbIdentifier);
        litebus::Async(explorer.second->GetAID(), &ExplorerActor::UnregisterLeaderChangedCallback, cbIdentifier);
    }
    return Status::OK();
}
}  // namespace functionsystem::explorer
