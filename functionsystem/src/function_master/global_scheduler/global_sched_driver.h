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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHEDULER_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHEDULER_H

#include "common/http/http_server.h"
#include "common/status/status.h"
#include "function_master/common/flags/flags.h"
#include "global_sched.h"
#include "global_sched_actor.h"

namespace functionsystem::global_scheduler {

class AgentApiRouter : public ApiRouterRegister {
public:
    void RegisterHandler(const std::string &url, const HttpHandler &handler) const override
    {
        ApiRouterRegister::RegisterHandler(url, handler);
    }

    void InitQueryAgentHandler(const std::shared_ptr<GlobalSched> &globalSched);
    void InitEvictAgentHandler(const std::shared_ptr<GlobalSched> &globalSched);
    void InitQueryAgentCountHandler(const std::shared_ptr<MetaStoreClient> &metaStoreClient);
    void InitGetSchedulingQueueHandler(const std::shared_ptr<GlobalSched> &globalSched);
};

class ResourcesApiRouter : public ApiRouterRegister {
public:
    void RegisterHandler(const std::string &url, const HttpHandler &handler) const override
    {
        ApiRouterRegister::RegisterHandler(url, handler);
    };

    void InitQueryResourcesInfoHandler(const std::shared_ptr<GlobalSched> &globalSched);
};

class GlobalSchedDriver {
public:
    GlobalSchedDriver(std::shared_ptr<GlobalSched> globalSched, const functionsystem::functionmaster::Flags &flags,
                      const std::shared_ptr<MetaStoreClient> &metaStoreClient);
    ~GlobalSchedDriver() = default;

    Status Start();

    Status Stop() const;

    void Await() const;

    std::shared_ptr<GlobalSched> GetGlobalSched() const;

    void BindComponentName(const std::string &componentName);

private:
    std::shared_ptr<GlobalSched> globalSched_ = nullptr;
    std::shared_ptr<HttpServer> httpServer_ = nullptr;
    std::shared_ptr<DefaultHealthyRouter> apiRouteRegister_ = nullptr;
    std::shared_ptr<AgentApiRouter> agentApiRouteRegister_ = nullptr;
    std::shared_ptr<ResourcesApiRouter> resourcesApiRouteRegister_ = nullptr;
    size_t maxLocalSchedPerDomainNode_{ 0 };
    size_t maxDomainSchedPerDomainNode_{ 0 };
    std::string metaStoreAddress_;
    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    std::string globalSchedAddress_;
    std::string schedulePlugins_;
    bool isScheduleTolerateAbnormal_{ false };
    uint32_t heartbeatTimeoutMs_{ DEFAULT_SYSTEM_TIMEOUT };
    uint32_t domainHeartbeatTimeoutMs_{ DEFAULT_DOMAIN_HEARTBEAT_TIMEOUT };
    uint64_t pullResourceInterval_{ DEFAULT_PULL_RESOURCE_INTERVAL };
    uint16_t maxPriority_{ 0 };
    std::string aggregatedStrategy_ {"no_aggregate"};

    bool enableMetrics_{ false };
    bool enablePrintResourceView_{ false };
    int32_t relaxed_ = -1;
    bool enablePreemption_{ false };
    std::string componentName_{};
    bool enableHorizontalScale_{ false };
};

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHEDULER_H
