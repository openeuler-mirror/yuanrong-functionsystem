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

#ifndef FUNCTIONSYSTEM_SUBSCRIPTION_MGR_H
#define FUNCTIONSYSTEM_SUBSCRIPTION_MGR_H

#include <actor/actor.hpp>
#include <async/future.hpp>

#include "common/utils/actor_driver.h"
#include "common/state_machine/instance_control_view.h"

namespace functionsystem::local_scheduler {
class InstanceCtrl;
class LocalSchedSrv;

struct SubscriptionMgrConfig {
    // is partial watch instances
    bool isPartialWatchInstances {false};
};

class SubscriptionMgr : public ActorDriver {
public:
    explicit SubscriptionMgr(const std::shared_ptr<BasisActor> &actor) : ActorDriver(actor), actor_(actor) {
    }

    ~SubscriptionMgr() override = default;

    static std::shared_ptr<SubscriptionMgr> Init(const std::string &nodeID, const SubscriptionMgrConfig &config);

    virtual litebus::Future<KillResponse>Subscribe(const std::string &srcInstanceID,
                                                   const std::shared_ptr<KillRequest> &req);

    virtual litebus::Future<KillResponse>Unsubscribe(const std::string &srcInstanceID,
                                                     const std::shared_ptr<KillRequest> &req);

    virtual litebus::Future<Status> NotifyMasterIPToSubscribers(const std::string &masterIP);

    void BindInstanceControlView(const std::shared_ptr<InstanceControlView> &view);

    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl);

    void BindObserver(const std::shared_ptr<function_proxy::ControlPlaneObserver> &observer);

    void BindLocalSchedSrv(const std::shared_ptr<LocalSchedSrv> &localSchedSrv);

private:
    std::shared_ptr<BasisActor> actor_;
};
}
#endif // FUNCTIONSYSTEM_SUBSCRIPTION_MGR_H
