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

#include "subscription_mgr.h"
#include "subscription_mgr_actor.h"

namespace functionsystem::local_scheduler {

std::shared_ptr<SubscriptionMgr> SubscriptionMgr::Init(const std::string &nodeID, const SubscriptionMgrConfig &config)
{
    auto actor = std::make_shared<SubscriptionMgrActor>(nodeID, config);
    litebus::Spawn(actor);
    return std::make_shared<SubscriptionMgr>(actor);
}

litebus::Future<KillResponse> SubscriptionMgr::Subscribe(const std::string &srcInstanceID,
                                                         const std::shared_ptr<KillRequest> &req)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &SubscriptionMgrActor::Subscribe, srcInstanceID, req);
}

litebus::Future<KillResponse> SubscriptionMgr::Unsubscribe(const std::string &srcInstanceID,
                                                           const std::shared_ptr<KillRequest> &req)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &SubscriptionMgrActor::Unsubscribe, srcInstanceID, req);
}

litebus::Future<Status> SubscriptionMgr::NotifyMasterIPToSubscribers(const std::string &masterIP)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &SubscriptionMgrActor::NotifyMasterIPToSubscribers, masterIP);
}

void SubscriptionMgr::BindInstanceControlView(const std::shared_ptr<InstanceControlView> &view)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &SubscriptionMgrActor::BindInstanceControlView, view);
}

void SubscriptionMgr::BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &SubscriptionMgrActor::BindInstanceCtrl, instanceCtrl);
}

void SubscriptionMgr::BindObserver(const std::shared_ptr<function_proxy::ControlPlaneObserver> &observer)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &SubscriptionMgrActor::BindObserver, observer);
}


void SubscriptionMgr::BindLocalSchedSrv(const std::shared_ptr<LocalSchedSrv> &localSchedSrv)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &SubscriptionMgrActor::BindLocalSchedSrv, localSchedSrv);
}

}