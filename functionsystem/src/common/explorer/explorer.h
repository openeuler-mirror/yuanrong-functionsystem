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

#ifndef COMMON_EXPLORER_H
#define COMMON_EXPLORER_H

#include "common/explorer/explorer_actor.h"
#include "common/kube_client/kube_client.h"
#include "meta_store_client/meta_store_client.h"

namespace functionsystem::explorer {

/**
 * Explorer wraps multiple ExplorerActors, which will give you
 *
 */
class Explorer : public Singleton<Explorer> {
public:
    ~Explorer() override = default;
    void BindExplorerActor(const std::string &electionKey, const std::shared_ptr<ExplorerActor> &explorerActor);
    void UnbindExplorerActor(const std::string &electionKey);
    Status AddLeaderChangedCallback(const std::string &cbIdentifier, const CallbackFuncLeaderChange &cbFunc);
    Status RemoveLeaderChangedCallback(const std::string &cbIdentifier);

    void Clear();

    [[maybe_unused]] static void NewEtcdExplorerActorForMaster(const std::string &electionKey,
                                                               const ElectionInfo &electionInfo,
                                                               const std::shared_ptr<MetaStoreClient> &metaStoreClient);

    [[maybe_unused]] static void NewTxnExplorerActorForMaster(const std::string &electionKey,
                                                              const ElectionInfo &electionValue,
                                                              const std::shared_ptr<MetaStoreClient> &metaStoreClient);

    [[maybe_unused]] static void NewK8sExplorerActorForMaster(const std::string &electionKey,
                                                              const ElectionInfo &electionInfo,
                                                              const std::string &k8sNamespace,
                                                              const std::shared_ptr<KubeClient> &kubeClient);

    [[maybe_unused]] static std::shared_ptr<ExplorerActor> NewStandAloneExplorerActorForMaster(
        const ElectionInfo &electionInfo, const LeaderInfo &leaderInfo);

    [[maybe_unused]] static bool CreateExplorer(const ElectionInfo &electionInfo, const LeaderInfo &leaderInfo,
                                                const std::shared_ptr<MetaStoreClient> &metaClient,
                                                const std::shared_ptr<KubeClient> &kubeClient,
                                                const std::string &k8sNamespace);

    // for test
    [[maybe_unused]] std::shared_ptr<ExplorerActor> GetExplorer(const std::string &key);

private:
    std::mutex lock_;
    std::unordered_map<std::string, std::shared_ptr<ExplorerActor>> explorers_;
    std::unordered_map<std::string, LeaderInfo> cachedLeadersInfo_;
};
}  // namespace functionsystem::explorer

#endif  // COMMON_EXPLORER_H
