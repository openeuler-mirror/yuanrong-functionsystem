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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHED_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHED_H

#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "common/status/status.h"
#include "global_sched_actor.h"
#include "scheduler_manager/domain_sched_mgr.h"
#include "scheduler_manager/local_sched_mgr.h"

namespace functionsystem::global_scheduler {

class GlobalSched : public MetaStoreHealthyObserver {
public:
    GlobalSched() = default;
    ~GlobalSched() override = default;

    virtual Status Start(std::shared_ptr<GlobalSchedActor> globalSchedActor);

    virtual Status Stop() const;

    void Await() const;

    virtual void InitManager(std::unique_ptr<DomainSchedMgr> domainSchedMgr,
                             std::unique_ptr<LocalSchedMgr> localSchedMgr);

    virtual litebus::Future<Status> Schedule(const std::shared_ptr<messages::ScheduleRequest> &req);

    virtual litebus::Future<litebus::Option<std::string>> GetLocalAddress(const std::string &name);

    virtual litebus::Future<litebus::Option<NodeInfo>> GetRootDomainInfo();

    virtual void LocalSchedAbnormalCallback(const LocalSchedAbnormalCallbackFunc &func);

    virtual void BindCheckLocalAbnormalCallback(const CheckLocalAbnormalCallbackFunc &func);

    virtual void AddLocalSchedAbnormalNotifyCallback(const std::string &name,
                                                     const LocalSchedAbnormalCallbackFunc &func);

    virtual void BindLocalDeleteCallback(const LocalDeleteCallbackFunc &func);

    virtual void BindLocalAddCallback(const LocalAddCallbackFunc &func);

    virtual litebus::Future<messages::QueryAgentInfoResponse> QueryAgentInfo(
        const std::shared_ptr<messages::QueryAgentInfoRequest> &req);

    virtual litebus::Future<messages::QueryInstancesInfoResponse> GetSchedulingQueue(
        const std::shared_ptr<messages::QueryInstancesInfoRequest> &req);

    virtual litebus::Future<messages::QueryResourcesInfoResponse> QueryResourcesInfo(
        const std::shared_ptr<messages::QueryResourcesInfoRequest> &req);

    virtual litebus::Future<Status> EvictAgent(const std::string &localID,
                                               const std::shared_ptr<messages::EvictAgentRequest> &req);

    virtual litebus::Future<std::unordered_set<std::string>> QueryNodes();

    void OnHealthyStatus(const Status &status) override;

private:
    std::shared_ptr<GlobalSchedActor> globalSchedActor_ = nullptr;
    std::shared_ptr<DomainSchedMgr> domainSchedMgr_ = nullptr;
    std::shared_ptr<LocalSchedMgr> localSchedMgr_ = nullptr;
};

void EncodeExternalAgentID(std::string &externalAgentID, const std::string &localID, const std::string &agentID);

bool DecodeExternalAgentID(const std::string &externalAgentID, std::string &localID, std::string &agentID);

void ConvertQueryAgentInfoResponseToExternal(const messages::QueryAgentInfoResponse &resp,
                                             messages::ExternalQueryAgentInfoResponse &externResp);

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHED_H
