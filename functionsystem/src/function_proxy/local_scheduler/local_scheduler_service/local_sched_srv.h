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

#ifndef LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_LOCAL_SCHED_SRV_H
#define LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_LOCAL_SCHED_SRV_H

#include <actor/actor.hpp>
#include <async/future.hpp>

#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_view_mgr.h"
#include "common/status/status.h"
#include "common/utils/actor_driver.h"
#include "local_sched_srv_actor.h"

namespace functionsystem::local_scheduler {

class LocalSchedSrv : public ActorDriver {
public:
    explicit LocalSchedSrv(std::shared_ptr<LocalSchedSrvActor> &&actor);
    ~LocalSchedSrv() override;

    static std::unique_ptr<LocalSchedSrv> Create(const LocalSchedSrvActor::Param &param);

    void Start(const std::shared_ptr<InstanceCtrl> &instanceCtrl,
               const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr);

    void BindFunctionAgentMgr(const std::shared_ptr<FunctionAgentMgr> &functionAgentMgr);

    virtual void DeletePod(const std::string &agentID, const std::string &reqID, const std::string &msg);

    virtual litebus::Future<messages::ScheduleResponse> ForwardSchedule(
        const std::shared_ptr<messages::ScheduleRequest> &req) const;

    virtual litebus::Future<messages::ForwardKillResponse> ForwardKillToInstanceManager(
        const std::shared_ptr<messages::ForwardKillRequest> &req);

    litebus::Future<Status> NotifyDsHealthy(bool healthy) const;

    virtual void NotifyEvictResult(const std::shared_ptr<messages::EvictAgentResult> &req);

    virtual litebus::Future<messages::GroupResponse> ForwardGroupSchedule(
        const std::shared_ptr<messages::GroupInfo> &groupInfo);
    virtual litebus::Future<Status> KillGroup(const std::shared_ptr<messages::KillGroup> &killReq);

    litebus::Future<Status> TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest);

    virtual litebus::Future<Status> GracefulShutdown();

    virtual litebus::Future<Status> IsRegisteredToGlobal();

    virtual litebus::Future<std::string> QueryMasterIP();

    virtual void BindSubscriptionMgr(const std::shared_ptr<SubscriptionMgr> &subscriptionMgr);

private:
    virtual void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl);
    virtual void BindResourceView(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr);

    std::shared_ptr<LocalSchedSrvActor> actor_;
};
}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_LOCAL_SCHED_SRV_H
