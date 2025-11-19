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
#ifndef LOCAL_SCHEDULER_BUNDLE_MANAGER_H
#define LOCAL_SCHEDULER_BUNDLE_MANAGER_H

#include "async/future.hpp"
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"
#include "common/utils/actor_driver.h"
#include "litebus.hpp"

namespace functionsystem::local_scheduler {
class BundleMgr : public ActorDriver, public MetaStoreHealthyObserver {
public:
    explicit BundleMgr(const std::shared_ptr<BasisActor> &actor) : ActorDriver(actor), actor_(actor)
    {
    }
    ~BundleMgr() override = default;
    void OnHealthyStatus(const Status &status) override;

    virtual litebus::Future<Status> SyncBundles(const std::string &agentID);

    virtual litebus::Future<Status> SyncFailedBundles(
        const std::unordered_map<std::string, messages::FuncAgentRegisInfo> &agentMap);

    virtual litebus::Future<Status> NotifyFailedAgent(const std::string &failedAgentID);

    virtual void UpdateBundlesStatus(const std::string &agentID, const resource_view::UnitStatus &status);

private:
    std::shared_ptr<BasisActor> actor_;
};
}  // namespace functionsystem::local_scheduler
#endif  // LOCAL_SCHEDULER_BUNDLE_MANAGER_H
