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

#include "subscription_mgr_actor.h"

#include <actor/actor.hpp>
#include <async/future.hpp>
#include <async/defer.hpp>

#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/utils/generate_message.h"
#include "common/constants/signal.h"

namespace functionsystem::local_scheduler {
void SubscriptionMgrActor::MarkLocalityByNodeMatch(const std::shared_ptr<KillContext>& ctx)
{
    ctx->isLocal = (ctx->instanceContext->GetInstanceInfo().functionproxyid() == nodeID_);
}

litebus::Future<Status> SubscriptionMgrActor::TryEnsureInstanceExistence(const std::string &instanceID)
{
    if (!config_.isPartialWatchInstances || instanceID.empty()) {
        return Status::OK();
    }

    litebus::Promise<Status> instanceExistStatus;
    auto observer = observer_.lock();
    ASSERT_IF_NULL(observer);
    observer->GetAndWatchInstance(instanceID)
        .OnComplete([instanceExistStatus](const litebus::Future<resource_view::InstanceInfo> &future) {
            // make sure instance is already updated in instance control view
            instanceExistStatus.SetValue(Status::OK());
        });
    return instanceExistStatus.GetFuture();
}

litebus::Future<KillResponse> SubscriptionMgrActor::NotifyInstanceTermination(const std::string &srcInstanceID,
                                                                              const std::string &dstInstanceID)
{
    common::NotificationPayload notificationPayload;
    notificationPayload.mutable_instancetermination()->set_instanceid(srcInstanceID);
    std::string serializedPayload;
    notificationPayload.SerializeToString(&serializedPayload);

    auto notifyReq = GenKillRequest(dstInstanceID, NOTIFY_SIGNAL);
    notifyReq->set_payload(std::move(serializedPayload));
    YRLOG_INFO("[event=instance_termination]|send a notification request: src_instance({}), dst_instance({}).",
               srcInstanceID, dstInstanceID);
    auto instanceCtrl = instanceCtrl_.lock();
    ASSERT_IF_NULL(instanceCtrl);
    return instanceCtrl->Kill(srcInstanceID, notifyReq);
}

litebus::Future<Status> SubscriptionMgrActor::OnNotifyMasterToSubscriber(const std::string &masterIP,
                                                                         const std::string &subscriberID)
{
    if (masterIP.empty()) {
        YRLOG_WARN("[event=subscribe_master]|master ip is empty.");
        return Status(StatusCode::FAILED, "master ip is empty");
    }

    common::NotificationPayload notificationPayload;
    notificationPayload.mutable_functionmasterevent()->set_address(masterIP);
    std::string serializedPayload;
    notificationPayload.SerializeToString(&serializedPayload);
    auto notifyReq = GenKillRequest(subscriberID, NOTIFY_SIGNAL);
    notifyReq->set_payload(std::move(serializedPayload));
    auto instanceCtrl = instanceCtrl_.lock();
    ASSERT_IF_NULL(instanceCtrl);
    (void)instanceCtrl->Kill(subscriberID, notifyReq);
    return Status::OK();
}

litebus::Future<Status> SubscriptionMgrActor::NotifyMasterIPToSubscribers(const std::string &masterIP)
{
    if (masterIP.empty()) {
        YRLOG_WARN("[event=subscribe_master]|master ip is empty.");
        return Status(StatusCode::FAILED, "master ip is empty");
    }

    YRLOG_INFO("[event=subscribe_master]|master ip is update to {}, and notify all subscriber.", masterIP);
    for (auto &subscriberID : masterSubscriberMap_) {
        OnNotifyMasterToSubscriber(masterIP, subscriberID);
    }
    return Status::OK();
}

litebus::Future<Status> SubscriptionMgrActor::CleanMasterSubscriber(const std::string &masterSubscriber)
{
    YRLOG_DEBUG("[event=subscribe_master]subscriber({}) is exited, clean it.", masterSubscriber);
    masterSubscriberMap_.erase(masterSubscriber);
    return Status::OK();
}

litebus::Future<Status> SubscriptionMgrActor::TryGetMasterIP(const std::string &subscriberID)
{
    YRLOG_DEBUG("[event=subscribe_master]subscriber({}) try to get master IP.", subscriberID);
    auto localSchedSrv = localSchedSrv_.lock();
    ASSERT_IF_NULL(localSchedSrv);
    return localSchedSrv->QueryMasterIP().Then(litebus::Defer(
        GetAID(), &SubscriptionMgrActor::OnNotifyMasterToSubscriber, std::placeholders::_1, subscriberID));
}

void SubscriptionMgrActor::CleanupOrphanedSubscription(const std::string &subscriber,
                                                       const std::string &publisher)
{
    common::UnsubscriptionPayload unsubscriptionPayload;
    unsubscriptionPayload.mutable_instancetermination()->set_instanceid(publisher);
    std::string serializedPayload;
    unsubscriptionPayload.SerializeToString(&serializedPayload);

    auto UnscribeReq = GenKillRequest(subscriber, UNSUBSCRIBE_SIGNAL);
    UnscribeReq->set_payload(std::move(serializedPayload));
    (void)Unsubscribe(subscriber, UnscribeReq);
}

litebus::Future<Status> SubscriptionMgrActor::RegisterOrphanedSubscriptionCleanup(const std::string &subscriber,
                                                                                  const std::string &publisher)
{
    auto instanceControlView = instanceControlView_.lock();
    ASSERT_IF_NULL(instanceControlView);
    auto instanceMachine = instanceControlView->GetInstance(subscriber);
    if (instanceMachine == nullptr) {
        YRLOG_WARN(
            "[event=instance_termination]|"
            "Failed to register orphaned subscription cleanup: subscriber instance({}) not found.",
            subscriber);
        CleanupOrphanedSubscription(subscriber, publisher);
        return Status::OK();
    }
    std::string key = "cleanup_Orphaned_Subscription_" + publisher;
    instanceMachine->AddStateChangeCallback(
        { InstanceState::EXITED },
        [dstInstanceID(publisher), aid(GetAID())](const resources::InstanceInfo &instanceInfo) {
            (void)litebus::Async(aid, &SubscriptionMgrActor::CleanupOrphanedSubscription, instanceInfo.instanceid(),
                                 dstInstanceID);
        },
        key);

    return Status::OK();
}

litebus::Future<KillResponse> SubscriptionMgrActor::SubscribeInstanceTermination(
    const std::shared_ptr<KillContext> &ctx, const std::string &instanceID)
{
    auto &rsp = ctx->killRsp;
    if (instanceID.empty()) {
        YRLOG_WARN("[event=instance_termination]|subscribed instanceID is empty.", instanceID);
        rsp = GenKillResponse(common::ErrorCode::ERR_PARAM_INVALID, "subscribed instanceID is empty");
        return rsp;
    }

    auto instanceControlView = instanceControlView_.lock();
    ASSERT_IF_NULL(instanceControlView);
    auto instanceMachine = instanceControlView->GetInstance(instanceID);
    if (instanceMachine == nullptr) {
        YRLOG_WARN("[event=instance_termination]|Subscribe failed: subscribed instance({}) not found.", instanceID);
        rsp = GenKillResponse(common::ErrorCode::ERR_INSTANCE_NOT_FOUND, "subscribed instance not found.");
        return rsp;
    }

    if (IsTerminalStatus(instanceMachine->GetInstanceState())) {
        YRLOG_WARN("[event=instance_termination]|Subscribe failed: subscribed instance({}) is already terminating.",
                   instanceID);
        rsp = GenKillResponse(common::ErrorCode::ERR_SUB_STATE_INVALID, "subscribed instance is already terminating");
        return rsp;
    }

    ctx->instanceContext = instanceMachine->GetInstanceContextCopy();
    MarkLocalityByNodeMatch(ctx);
    if (!ctx->isLocal) {
        YRLOG_DEBUG("[event=instance_termination]|Non-local subscription, handled remotely, "
                   "src_instance({}), dst_instance({})", ctx->srcInstanceID, instanceID);
        auto instanceCtrl = instanceCtrl_.lock();
        ASSERT_IF_NULL(instanceCtrl);
        return instanceCtrl->ForwardSubscriptionEvent(ctx);
    }

    std::string subscribeKey = "subscribe_instance_termination_" + ctx->srcInstanceID;
    if (instanceMachine->HasStateChangeCallback(subscribeKey)) {
        YRLOG_DEBUG("[event=instance_termination]|Subscribe success:"
                    " duplicate subscription, src_instance({}), dst_instance({}).",
                    ctx->srcInstanceID, instanceID);
        return rsp;
    }

    instanceMachine->AddStateChangeCallback(
        TERMINAL_INSTANCE_STATES,
        [dstInstanceID(ctx->srcInstanceID), aid(GetAID())](const resources::InstanceInfo &instanceInfo) {
            (void)litebus::Async(aid, &SubscriptionMgrActor::NotifyInstanceTermination, instanceInfo.instanceid(),
                                 dstInstanceID);
        }, subscribeKey);

    // Register cleanup callback for the subscription in case the subscriber dies, to prevent orphaned subscriptions.
    (void)TryEnsureInstanceExistence(ctx->srcInstanceID)
        .Then(litebus::Defer(GetAID(), &SubscriptionMgrActor::RegisterOrphanedSubscriptionCleanup,
                             ctx->srcInstanceID, instanceID));

    YRLOG_INFO("[event=instance_termination]|Subscribe success: src_instance({}), dst_instance({}).",
               ctx->srcInstanceID, instanceID);
    return rsp;
}

litebus::Future<KillResponse> SubscriptionMgrActor::SubscribeFunctionMaster(const std::shared_ptr<KillContext> &ctx)
{
    auto &rsp = ctx->killRsp;
    auto &subscriberID = ctx->srcInstanceID;

    auto instanceControlView = instanceControlView_.lock();
    ASSERT_IF_NULL(instanceControlView);
    auto instanceMachine = instanceControlView->GetInstance(subscriberID);
    if (instanceMachine == nullptr) {
        YRLOG_WARN("[event=subscribe_master]|Subscribe failed: subscriber({})  not found.", subscriberID);
        rsp = GenKillResponse(common::ErrorCode::ERR_INSTANCE_NOT_FOUND, "subscriber not found.");
        return rsp;
    }

    if (IsTerminalStatus(instanceMachine->GetInstanceState())) {
        YRLOG_WARN("[event=subscribe_master]|Subscribe failed: subscriber({}) is already terminating.", subscriberID);
        rsp = GenKillResponse(common::ErrorCode::ERR_SUB_STATE_INVALID, "subscriber is already terminating");
        return rsp;
    }

    std::string subscribeKey = "subscribe_master_" + subscriberID;
    if (instanceMachine->HasStateChangeCallback(subscribeKey)) {
        YRLOG_DEBUG("[event=subscribe_master]|Subscribe success: duplicate subscription, subscriber({}).",
                    subscriberID);
        (void)TryGetMasterIP(subscriberID);
        return rsp;
    }

    instanceMachine->AddStateChangeCallback(
        TERMINAL_INSTANCE_STATES,
        [dstInstanceID(subscriberID), aid(GetAID())](const resources::InstanceInfo &instanceInfo) {
            (void)litebus::Async(aid, &SubscriptionMgrActor::CleanMasterSubscriber, dstInstanceID);
        },
        subscribeKey);

    // cache need to clean while subscriber was exited
    masterSubscriberMap_.insert(subscriberID);
    YRLOG_INFO("[event=subscribe_master]|Subscribe success: subscriber({}).", subscriberID);

    (void)TryGetMasterIP(subscriberID);

    return rsp;
}

litebus::Future<KillResponse> SubscriptionMgrActor::Subscribe(const std::string &srcInstanceID,
                                                              const std::shared_ptr<KillRequest> &req)
{
    auto ctx = std::make_shared<KillContext>();
    ctx->srcInstanceID = srcInstanceID;
    ctx->killRequest = req;
    auto &rsp = ctx->killRsp;
    rsp = GenKillResponse(common::ErrorCode::ERR_NONE, "");

    common::SubscriptionPayload subscriptionPayload;
    if (!subscriptionPayload.ParseFromString(ctx->killRequest->payload())) {
        YRLOG_ERROR("Subscribe failed: failed to parse subscriptionPayload from {}.", ctx->srcInstanceID);
        rsp = GenKillResponse(common::ErrorCode::ERR_PARAM_INVALID, "failed to parse subscriptionPayload.");
        return rsp;
    }

    if (subscriptionPayload.Content_case() == common::SubscriptionPayload::CONTENT_NOT_SET) {
        YRLOG_WARN("Subscribe failed: empty subscription payload from {}.", ctx->srcInstanceID);
        rsp = GenKillResponse(common::ErrorCode::ERR_PARAM_INVALID, "empty subscription payload.");
        return rsp;
    }

    switch (subscriptionPayload.Content_case()) {
        case common::SubscriptionPayload::kInstanceTermination:
            return TryEnsureInstanceExistence(subscriptionPayload.instancetermination().instanceid())
                .Then(litebus::Defer(GetAID(), &SubscriptionMgrActor::SubscribeInstanceTermination,
                                     ctx, subscriptionPayload.instancetermination().instanceid()));
        case common::SubscriptionPayload::kFunctionMaster:
            return SubscribeFunctionMaster(ctx);
        default:
            YRLOG_WARN("Subscribe failed: Unsupported subscription type from {}.", ctx->srcInstanceID);
            rsp = GenKillResponse(common::ErrorCode::ERR_PARAM_INVALID, "Unsupported subscription type.");
            break;
    }

    return rsp;
}

litebus::Future<KillResponse> SubscriptionMgrActor::UnsubscribeInstanceTermination(
    const std::shared_ptr<KillContext> &ctx, const std::string &instanceID)
{
    auto &rsp = ctx->killRsp;
    if (instanceID.empty()) {
        YRLOG_WARN("[event=instance_termination]|subscribed instanceID is empty.", instanceID);
        rsp = GenKillResponse(common::ErrorCode::ERR_PARAM_INVALID, "subscribed instanceID is empty");
        return rsp;
    }

    auto instanceControlView = instanceControlView_.lock();
    ASSERT_IF_NULL(instanceControlView);
    auto instanceMachine = instanceControlView->GetInstance(instanceID);
    if (instanceMachine == nullptr) {
        YRLOG_DEBUG("[event=instance_termination]|Unsubscribe Success: "
                    "subscribed instance({}) not found, treat as unsubscription succeeded.", instanceID);
        return rsp;
    }

    ctx->instanceContext = instanceMachine->GetInstanceContextCopy();
    MarkLocalityByNodeMatch(ctx);
    if (!ctx->isLocal) {
        YRLOG_DEBUG("[event=instance_termination]|Non-local unsubscription, handled remotely, "
                    "src_instance({}), dst_instance({})", ctx->srcInstanceID, instanceID);
        auto instanceCtrl = instanceCtrl_.lock();
        ASSERT_IF_NULL(instanceCtrl);
        return instanceCtrl->ForwardSubscriptionEvent(ctx);
    }

    std::string subscribeKey = "subscribe_instance_termination_" + ctx->srcInstanceID;
    instanceMachine->DeleteStateChangeCallback(subscribeKey);
    YRLOG_INFO("[event=instance_termination]|Unsubscribe Success: "
               "src_instance({}), dst_instance({}).", ctx->srcInstanceID, instanceID);
    return rsp;
}

litebus::Future<KillResponse> SubscriptionMgrActor::UnsubscribeFunctionMaster(const std::shared_ptr<KillContext> &ctx)
{
    masterSubscriberMap_.erase(ctx->srcInstanceID);
    YRLOG_INFO("[event=subscribe_master]|Unsubscribe Success: subscriber({}).", ctx->srcInstanceID);
    return ctx->killRsp;
}

litebus::Future<KillResponse> SubscriptionMgrActor::Unsubscribe(const std::string &srcInstanceID,
                                                                const std::shared_ptr<KillRequest> &req)
{
    auto ctx = std::make_shared<KillContext>();
    ctx->srcInstanceID = srcInstanceID;
    ctx->killRequest = req;
    auto &rsp = ctx->killRsp;
    rsp = GenKillResponse(common::ErrorCode::ERR_NONE, "");

    common::UnsubscriptionPayload unsubscriptionPayload;
    if (!unsubscriptionPayload.ParseFromString(ctx->killRequest->payload())) {
        YRLOG_ERROR("Unsubscribe failed: failed to parse unsubscriptionPayload from {}.", ctx->srcInstanceID);
        rsp = GenKillResponse(common::ErrorCode::ERR_PARAM_INVALID, "failed to parse unsubscriptionPayload.");
        return rsp;
    }

    if (unsubscriptionPayload.Content_case() == common::UnsubscriptionPayload::CONTENT_NOT_SET) {
        YRLOG_WARN("Unsubscribe failed: empty unsubscription payload from {}.", ctx->srcInstanceID);
        rsp = GenKillResponse(common::ErrorCode::ERR_PARAM_INVALID, "empty unsubscription payload.");
        return rsp;
    }

    switch (unsubscriptionPayload.Content_case()) {
        case common::UnsubscriptionPayload::kInstanceTermination:
            return TryEnsureInstanceExistence(unsubscriptionPayload.instancetermination().instanceid())
                .Then(litebus::Defer(GetAID(), &SubscriptionMgrActor::UnsubscribeInstanceTermination,
                                     ctx, unsubscriptionPayload.instancetermination().instanceid()));
        case common::UnsubscriptionPayload::kFunctionMaster:
            return UnsubscribeFunctionMaster(ctx);
        default:
            YRLOG_ERROR("Unsubscribe failed: Unsupported unsubscription type from {}.", ctx->srcInstanceID);
            rsp = GenKillResponse(common::ErrorCode::ERR_PARAM_INVALID, "Unsupported unsubscription type.");
            break;
    }

    return rsp;
}
}
