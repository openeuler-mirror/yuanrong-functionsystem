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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_MANAGER_DOMAIN_SCHED_MGR_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_MANAGER_DOMAIN_SCHED_MGR_H

#include <async/option.hpp>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "domain_sched_mgr_actor.h"

namespace functionsystem::global_scheduler {

const uint32_t DEFAULT_RETRY_CYCLE = 10000;  // ms

class DomainSchedMgr {
public:
    explicit DomainSchedMgr(const uint32_t heartbeatTimeoutMs);
    explicit DomainSchedMgr(std::shared_ptr<DomainSchedMgrActor> domainSchedMgrActor);

    virtual ~DomainSchedMgr() = default;

    virtual void Start();
    virtual void Stop();

    /**
     * Notify DomainSchedMgrActor to inform DomainScheduler to update topology
     *
     * @param address destination DomainScheduler url
     * @param topology destination DomainScheduler's topology
     */
    virtual void UpdateSchedTopoView(const std::string &name, const std::string &address,
                                     const messages::ScheduleTopology &topology) const;

    /**
     * Inform DomainSchedMgrActor to send registered information
     *
     * @param dst register DomainScheduler
     * @param topology register DomainScheduler's topology
     */
    virtual void Registered(const litebus::AID &dst, const litebus::Option<messages::ScheduleTopology> &topology) const;

    /**
     * add callback for add DomainScheduler
     *
     * @param func callback function
     * @return
     */
    virtual Status AddDomainSchedCallback(const CallbackAddFunc &func) const;

    /**
     * add callback for delete DomainScheduler
     *
     * @param func callback function
     * @return
     */
    virtual Status DelDomainSchedCallback(const CallbackDelFunc &func) const;

    /**
     * add callback for delete LocalScheduler
     *
     * @param func callback function
     * @return
     */
    virtual Status DelLocalSchedCallback(const CallbackDelFunc &func) const;

    /**
     * add call back for worker status change
     *
     * @param func callback function
     * @return
     */
    virtual Status NotifyWorkerStatusCallback(const CallbackWorkerFunc &func) const;

    /**
     * start HeartBeatObserver to ping Top DomainScheduler
     *
     * @param name DomainScheduler name
     * @param address DomainScheduler address
     */
    virtual litebus::Future<Status> Connect(const std::string &name, const std::string &address) const;

    /**
     * stop HeartBeatObserver
     */
    virtual void Disconnect() const;

    /**
     * Send ScheduleRequest to DomainScheduler.
     * @param address The address of the DomainScheduler which will receive the request.
     * @param req Scheduler request.
     * @param retryCycle retry schedule cycle
     * @return Schedule result.
     */
    virtual litebus::Future<Status> Schedule(const std::string &name, const std::string &address,
                                             const std::shared_ptr<messages::ScheduleRequest> &req,
                                             uint32_t retryCycle = DEFAULT_RETRY_CYCLE) const;

    virtual void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    virtual litebus::Future<messages::QueryAgentInfoResponse> QueryAgentInfo(
        const std::string &name, const std::string &address,
        const std::shared_ptr<messages::QueryAgentInfoRequest> &req);
    litebus::Future<messages::QueryInstancesInfoResponse> GetSchedulingQueue(
        const std::string &, const std::string &, const std::shared_ptr<messages::QueryInstancesInfoRequest> &req);

    virtual litebus::Future<messages::QueryResourcesInfoResponse> QueryResourcesInfo(
            const std::string &name, const std::string &address,
            const std::shared_ptr<messages::QueryResourcesInfoRequest> &req);

private:
    std::shared_ptr<DomainSchedMgrActor> domainSchedMgrActor_;
};

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_MANAGER_DOMAIN_SCHED_MGR_H
