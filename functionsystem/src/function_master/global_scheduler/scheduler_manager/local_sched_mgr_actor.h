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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_LOCAL_SCHED_MGR_ACTOR_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_LOCAL_SCHED_MGR_ACTOR_H

#include <actor/actor.hpp>
#include <async/async.hpp>
#include <async/future_base.hpp>
#include <functional>

#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "common/utils/request_sync_helper.h"
#include "domain_sched_mgr_actor.h"

namespace functionsystem::global_scheduler {

class LocalSchedMgrActor : public litebus::ActorBase, public std::enable_shared_from_this<LocalSchedMgrActor> {
public:
    explicit LocalSchedMgrActor(const std::string &name);

    ~LocalSchedMgrActor() override = default;

    void Register(const litebus::AID &from, std::string &&name, std::string &&msg);

    void UnRegister(const litebus::AID &from, std::string &&name, std::string &&msg);

    void Registered(const litebus::AID &dst, const litebus::Option<messages::ScheduleTopology> &topology);

    Status AddLocalSchedCallback(const CallbackAddFunc &func);

    Status DelLocalSchedCallback(const CallbackDelFunc &func);

    void UpdateSchedTopoView(const std::string &address, const messages::ScheduleTopology &topology);

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    litebus::Future<Status> EvictAgentOnLocal(const std::string &address,
                                              const std::shared_ptr<messages::EvictAgentRequest> &req);

    void OnLocalAbnormal(const std::string &localID, const std::string &address);

    void EvictAck(const litebus::AID &from, std::string &&name, std::string &&msg);

    void NotifyEvictResult(const litebus::AID &from, std::string &&name, std::string &&msg);

protected:
    void Init() override;

private:
    struct EvictContext {
        std::string agentID;
        std::shared_ptr<litebus::Promise<Status>> resultPromise;
        litebus::Timer ackRetryTimer;
    };

    void SendEvict(const std::shared_ptr<EvictContext> &ctx,
                   const std::string &address,
                   const std::shared_ptr<messages::EvictAgentRequest> &req);

    class Business : public leader::BusinessPolicy {
    public:
        explicit Business(const std::shared_ptr<LocalSchedMgrActor> &actor) : actor_(actor)
        {
        }
        ~Business() override = default;
        virtual void Register(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        virtual void UnRegister(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        void OnChange() override{};

    protected:
        std::weak_ptr<LocalSchedMgrActor> actor_;
    };

    class MasterBusiness : public Business {
    public:
        explicit MasterBusiness(const std::shared_ptr<LocalSchedMgrActor> &actor) : Business(actor)
        {
        }
        ~MasterBusiness() override = default;

        void Register(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void UnRegister(const litebus::AID &from, std::string &&name, std::string &&msg) override;
    };

    class SlaveBusiness : public Business {
    public:
        explicit SlaveBusiness(const std::shared_ptr<LocalSchedMgrActor> &actor) : Business(actor)
        {
        }
        ~SlaveBusiness() override = default;
        void Register(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void UnRegister(const litebus::AID &from, std::string &&name, std::string &&msg) override {};
    };

    CallbackAddFunc addLocalSchedCallback_;
    CallbackDelFunc delLocalSchedCallback_;

    std::string curStatus_;
    std::shared_ptr<Business> business_ {nullptr};
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    const uint32_t evictAckTimeout_ = 5000;
    REQUEST_SYNC_HELPER(LocalSchedMgrActor, Status, evictAckTimeout_, evictAckSync_);
    using AgentEvictResultContexts = std::unordered_map<std::string, std::shared_ptr<EvictContext>>;
    // key is local address
    std::unordered_map<std::string, AgentEvictResultContexts> evictCtxs_;
};

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_LOCAL_SCHED_MGR_ACTOR_H
