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

#ifndef FUNCTIONSYSTEM_SUBSCRIPTION_MGR_ACTOR_H
#define FUNCTIONSYSTEM_SUBSCRIPTION_MGR_ACTOR_H

#include "common/status/status.h"
#include "common/utils/actor_driver.h"
#include "common/state_machine/instance_control_view.h"
#include "common/state_machine/instance_state_machine.h"
#include "common/state_machine/instance_context.h"
#include "function_proxy/local_scheduler/instance_control/instance_ctrl.h"
#include "function_proxy/local_scheduler/local_scheduler_service/local_sched_srv.h"

namespace functionsystem::local_scheduler {

class SubscriptionMgrActor : public BasisActor {
public:
    explicit SubscriptionMgrActor(const std::string &nodeID, const SubscriptionMgrConfig &config)
        : BasisActor(nodeID + SUBSCRIPTION_MGR_ACTOR_NAME_POSTFIX), nodeID_(nodeID), config_(config) {}

    ~SubscriptionMgrActor() = default;

    void BindInstanceControlView(const std::shared_ptr<InstanceControlView> &instanceControlView)
    {
        ASSERT_IF_NULL(instanceControlView);
        instanceControlView_ = instanceControlView;
    }

    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
    {
        ASSERT_IF_NULL(instanceCtrl);
        instanceCtrl_ = instanceCtrl;
    }

    void BindObserver(const std::shared_ptr<function_proxy::ControlPlaneObserver> &observer)
    {
        ASSERT_IF_NULL(observer);
        observer_ = observer;
    }

    void BindLocalSchedSrv(const std::shared_ptr<LocalSchedSrv> &localSchedSrv)
    {
        ASSERT_IF_NULL(localSchedSrv);
        localSchedSrv_ = localSchedSrv;
    }

    litebus::Future<KillResponse> Subscribe(const std::string &srcInstanceID,
                                            const std::shared_ptr<KillRequest> &req);

    litebus::Future<KillResponse> Unsubscribe(const std::string &srcInstanceID,
                                              const std::shared_ptr<KillRequest> &req);

    litebus::Future<Status> NotifyMasterIPToSubscribers(const std::string &masterIP);

private:
    void MarkLocalityByNodeMatch(const std::shared_ptr<KillContext>& ctx);

    litebus::Future<Status> TryEnsureInstanceExistence(const std::string &instanceID);

    void CleanupOrphanedSubscription(const std::string &subscriber, const std::string &publisher);

    litebus::Future<Status> RegisterOrphanedSubscriptionCleanup(const std::string &subscriber,
                                                                const std::string &publisher);

    litebus::Future<std::shared_ptr<KillContext>> ValidateInstanceTerminationInfo(
        const std::shared_ptr<KillContext> &ctx, const std::string &instanceID);

    litebus::Future<KillResponse> NotifyInstanceTermination(const std::string &srcInstanceID,
                                                            const std::string &dstInstanceID);

    litebus::Future<KillResponse> SubscribeInstanceTermination(const std::shared_ptr<KillContext> &ctx,
                                                               const std::string &instanceID);

    litebus::Future<KillResponse> UnsubscribeInstanceTermination(const std::shared_ptr<KillContext> &ctx,
                                                                 const std::string &instanceID);

    litebus::Future<KillResponse> SubscribeFunctionMaster(const std::shared_ptr<KillContext> &ctx);

    litebus::Future<Status> OnNotifyMasterToSubscriber(const std::string &masterIP,
                                                             const std::string &subscriberID);

    litebus::Future<KillResponse> UnsubscribeFunctionMaster(const std::shared_ptr<KillContext> &ctx);

    litebus::Future<Status> CleanMasterSubscriber(const std::string &masterSubscriber);

    litebus::Future<Status> TryGetMasterIP(const std::string &subscriberID);

private:
    std::string nodeID_;
    SubscriptionMgrConfig config_;
    std::weak_ptr<InstanceControlView> instanceControlView_;
    std::weak_ptr<InstanceCtrl> instanceCtrl_;
    std::weak_ptr<function_proxy::ControlPlaneObserver> observer_;
    std::weak_ptr<LocalSchedSrv> localSchedSrv_;
    std::set<std::string> masterSubscriberMap_;
};
}

#endif // FUNCTIONSYSTEM_SUBSCRIPTION_MGR_ACTOR_H