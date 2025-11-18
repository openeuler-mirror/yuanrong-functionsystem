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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_MANAGER_LOCAL_SCHED_MGR_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_MANAGER_LOCAL_SCHED_MGR_H

#include <async/option.hpp>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "local_sched_mgr_actor.h"

namespace functionsystem::global_scheduler {

class LocalSchedMgr {
public:
    LocalSchedMgr();

    explicit LocalSchedMgr(std::shared_ptr<LocalSchedMgrActor> localSchedMgrActor);

    virtual ~LocalSchedMgr();

    virtual void Start();
    virtual void Stop();

    /**
     * add callback for add LocalScheduler
     *
     * @param func callback function
     * @return
     */
    virtual Status AddLocalSchedCallback(const CallbackAddFunc &func) const;

    /**
     * add callback for del LocalScheduler
     */
    virtual litebus::Future<Status> DelLocalSchedCallback(const CallbackDelFunc &func) const;

    /**
     * Notify the LocalSchedMgrActor to inform the LocalScheduler update topology
     *
     * @param address destination LocalScheduler url
     * @param topology destination LocalScheduler's topology
     */
    virtual void UpdateSchedTopoView(const std::string &address, const messages::ScheduleTopology &topology);

    /**
     * Inform LocalSchedMgrActor to send registered information
     *
     * @param dst destination LocalScheduler
     * @param topology destination LocalScheduler's topology
     */
    virtual void Registered(const litebus::AID &dst, const litebus::Option<messages::ScheduleTopology> &topology) const;

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    virtual litebus::Future<Status> EvictAgentOnLocal(const std::string &address,
                                                      const std::shared_ptr<messages::EvictAgentRequest> &req);

    void OnLocalAbnormal(const std::string &localID, const std::string &address);

private:
    std::shared_ptr<LocalSchedMgrActor> localSchedMgrActor_;
};

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_MANAGER_LOCAL_SCHED_MGR_H
