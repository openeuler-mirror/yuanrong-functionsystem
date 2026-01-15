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

#include "global_sched_driver.h"

#include <logs/api/provider.h>

#include <memory>
#include <utility>

#include "common/constants/actor_name.h"
#include "common/constants/metastore_keys.h"
#include "common/logs/logging.h"
#include "meta_store_client/meta_store_client.h"
#include "common/scheduler_topology/sched_tree.h"

namespace functionsystem::global_scheduler {

constexpr int MIN_SCHED_PER_DOMAIN_NODE = 2;
constexpr int DEFAULT_LOCAL_SCHED_PER_DOMAIN_NODE = 4005;
constexpr int DEFAULT_DOMAIN_SCHED_PER_DOMAIN_NODE = 1000;
constexpr uint32_t MAX_EVICT_TIMEOUT = 6000;
constexpr uint32_t DEFAULT_EVICT_TIMEOUT = 30;
const uint32_t DEFAULT_HEARTBEAT_TIMES = 12;
const uint32_t DEFAULT_HEARTBEAT_INTERVAL = 1000;
const std::string DEFAULT_META_STORE_ADDRESS = "127.0.0.1:32279";
const std::string GLOBAL_SCHEDULER = "global-scheduler";
const std::string QUERY_AGENTS_URL = "/queryagents";
const std::string GET_SCHEDULING_QUEUE_URL = "/scheduling_queue";
const std::string EVICT_AGENT_URL = "/evictagent";
const std::string QUERY_AGENT_COUNT_URL = "/queryagentcount";
const std::string QUERY_RESOURCES_URL = "/resources";
const std::string JSON_FORMAT = "json";
const std::string PROTOBUF_FORMAT = "protobuf";

std::string EvictResultBody(common::ErrorCode code, const std::string &message)
{
    auto status = messages::FunctionSystemStatus();
    status.set_code(code);
    status.set_message(message);
    std::string rspBody;
    google::protobuf::util::JsonOptions options;
    options.always_print_enums_as_ints = true;
    options.always_print_primitive_fields = true;
    (void)google::protobuf::util::MessageToJsonString(status, &rspBody, options);
    YRLOG_DEBUG("agent evict result: {}", status.DebugString());
    return rspBody;
}

void AgentApiRouter::InitGetSchedulingQueueHandler(const std::shared_ptr<GlobalSched> &globalSched)
{
    auto getSchedulingQueue = [globalSched](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        if (request.method != "GET") {
            YRLOG_ERROR("Invalid request method.");
            return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
        }

        bool useJsonFormat = request.headers.find("Type") == request.headers.end()
                             || request.headers.find("Type")->second == JSON_FORMAT;

        auto req = std::make_shared<messages::QueryInstancesInfoRequest>();
        auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        req->set_requestid(requestID);
        YRLOG_INFO("{}|get scheduling queue", requestID);

        return globalSched->GetSchedulingQueue(req).Then(
            [useJsonFormat](const messages::QueryInstancesInfoResponse &resp) -> litebus::Future<HttpResponse> {
                if (!useJsonFormat) {
                    return litebus::http::Ok(resp.SerializeAsString());
                }

                std::string rspBody;
                google::protobuf::util::JsonOptions options;
                options.always_print_primitive_fields = true;
                (void)google::protobuf::util::MessageToJsonString(resp, &rspBody, options);
                YRLOG_DEBUG("GetSchedulingQueue: size {}", resp.instanceinfos_size());

                return litebus::http::Ok(rspBody);
            });
    };

    RegisterHandler(GET_SCHEDULING_QUEUE_URL, getSchedulingQueue);
}

void AgentApiRouter::InitQueryAgentHandler(const std::shared_ptr<GlobalSched> &globalSched)
{
    auto queryAgentsHandler = [globalSched](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        if (request.method != "GET") {
            YRLOG_ERROR("Invalid request method.");
            return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
        }

        auto req = std::make_shared<messages::QueryAgentInfoRequest>();
        auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        req->set_requestid(requestID);
        YRLOG_INFO("{}|query agentinfo", requestID);
        return globalSched->QueryAgentInfo(req).Then(
            [](const messages::QueryAgentInfoResponse &resp) -> litebus::Future<HttpResponse> {
                std::string rspBody;
                messages::ExternalQueryAgentInfoResponse externResp;
                ConvertQueryAgentInfoResponseToExternal(resp, externResp);
                google::protobuf::util::JsonOptions options;
                options.always_print_primitive_fields = true;
                (void)google::protobuf::util::MessageToJsonString(externResp, &rspBody, options);
                YRLOG_DEBUG("query get agentinfo: size {}", resp.agentinfos_size());
                return litebus::http::Ok(rspBody);
            });
    };
    RegisterHandler(QUERY_AGENTS_URL, queryAgentsHandler);
}

void AgentApiRouter::InitEvictAgentHandler(const std::shared_ptr<GlobalSched> &globalSched)
{
    auto evictAgentHandler = [globalSched](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        if (request.method != "POST") {
            return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
        }
        YRLOG_INFO("received evict agent. body{}", request.body);
        auto req = std::make_shared<messages::EvictAgentRequest>();
        if (!google::protobuf::util::JsonStringToMessage(request.body, req.get()).ok()) {
            YRLOG_ERROR("invalid evict request body. {}", request.body);
            return litebus::http::Ok(EvictResultBody(common::ERR_PARAM_INVALID, "Invalid evict request body."));
        }
        auto externAgentID = req->agentid();
        if (externAgentID.empty()) {
            YRLOG_ERROR("The agentID cannot be empty");
            return litebus::http::Ok(EvictResultBody(common::ERR_PARAM_INVALID, "Empty agentID"));
        }
        if (req->timeoutsec() == 0) {
            YRLOG_WARN("evict timeout may not be set. using default {} sec as timeout.", DEFAULT_EVICT_TIMEOUT);
            req->set_timeoutsec(DEFAULT_EVICT_TIMEOUT);
        }
        if (req->timeoutsec() > MAX_EVICT_TIMEOUT) {
            YRLOG_ERROR("invalid timeout. body {} timeout sec should be range [0 {}] sec.", request.body,
                        MAX_EVICT_TIMEOUT);
            return litebus::http::Ok(EvictResultBody(
                common::ERR_PARAM_INVALID,
                "Invalid timeout request parameters. should be range from 0 to " + std::to_string(MAX_EVICT_TIMEOUT)));
        }
        std::string localID;
        std::string agentID;
        if (!DecodeExternalAgentID(externAgentID, localID, agentID)) {
            YRLOG_ERROR("invalid agentID({}) .", externAgentID);
            return litebus::http::Ok(EvictResultBody(common::ERR_PARAM_INVALID, "Invalid agentID"));
        }
        req->set_agentid(agentID);
        return globalSched->EvictAgent(localID, req)
            .Then([agentID](const Status &status) -> litebus::Future<HttpResponse> {
                return litebus::http::Ok(
                    EvictResultBody(Status::GetPosixErrorCode(status.StatusCode()), status.GetMessage()));
            });
    };
    RegisterHandler(EVICT_AGENT_URL, evictAgentHandler);
}

void AgentApiRouter::InitQueryAgentCountHandler(const std::shared_ptr<MetaStoreClient> &metaStoreClient)
{
    auto queryAgentCountHandler = [metaStoreClient](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        if (request.method != "GET") {
            YRLOG_ERROR("Invalid request method.");
            return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
        }

        return metaStoreClient->Get(READY_AGENT_CNT_KEY, { .prefix = false })
            .Then([](const std::shared_ptr<GetResponse> &resp) -> litebus::Future<HttpResponse> {
                std::string readyAgentCount = "-1";
                if (!resp->status.IsOk()) {
                    YRLOG_WARN("failed to get ready agent count, status: {}", resp->status.ToString());
                    return litebus::http::Ok(readyAgentCount);
                }
                if (resp->kvs.size() != 1) {
                    YRLOG_WARN("unexpected kv count: {}", resp->kvs.size());
                    return litebus::http::Ok(readyAgentCount);
                }
                readyAgentCount = resp->kvs[0].value();
                YRLOG_DEBUG("query get agent count: {}", readyAgentCount);
                return litebus::http::Ok(readyAgentCount);
            });
    };
    RegisterHandler(QUERY_AGENT_COUNT_URL, queryAgentCountHandler);
}

void ResourcesApiRouter::InitQueryResourcesInfoHandler(const std::shared_ptr<GlobalSched> &globalSched)
{
    auto queryResourcesJsonHandler = [globalSched](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        if (request.method != "GET") {
            YRLOG_ERROR("Invalid request method.");
            return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
        }
        // Specifies the format of the response.
        // Can be 'json' for JSON format or 'protobuf' for Protobuf format. Defaults to 'json' if not provided.
        const auto it = request.headers.find("Type");
        if (it != request.headers.end() && it->second != JSON_FORMAT && it->second != PROTOBUF_FORMAT) {
            YRLOG_ERROR("Unsupported Type format: {}", it->second);
            return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST);
        }
        bool useJsonFormat = (it == request.headers.end()) || (it->second == JSON_FORMAT);

        auto req = std::make_shared<messages::QueryResourcesInfoRequest>();
        auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        req->set_requestid(requestID);
        YRLOG_INFO("{}|received a request to query resource info.", requestID);
        return globalSched->QueryResourcesInfo(req).Then(
            [useJsonFormat](const messages::QueryResourcesInfoResponse &resp) -> litebus::Future<HttpResponse> {
                if (!useJsonFormat) {
                    return litebus::http::Ok(resp.SerializeAsString());
                }
                std::string rspBody;
                google::protobuf::util::JsonOptions options;
                options.always_print_primitive_fields = false;
                (void)google::protobuf::util::MessageToJsonString(resp, &rspBody, options);
                return litebus::http::Ok(rspBody);
            });
    };

    RegisterHandler(QUERY_RESOURCES_URL, queryResourcesJsonHandler);
}

GlobalSchedDriver::GlobalSchedDriver(std::shared_ptr<GlobalSched> globalSched, const functionmaster::Flags &flags,
                                     const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    : globalSched_(std::move(globalSched)),
      maxLocalSchedPerDomainNode_(DEFAULT_LOCAL_SCHED_PER_DOMAIN_NODE),
      maxDomainSchedPerDomainNode_(DEFAULT_DOMAIN_SCHED_PER_DOMAIN_NODE),
      metaStoreAddress_(DEFAULT_META_STORE_ADDRESS),
      metaStoreClient_(metaStoreClient)
{
    if (const auto &d1Flag(flags.GetD1()); d1Flag.IsNone() || d1Flag.Get() < 0) {
        YRLOG_WARN("flag d1 is invalid, set maxLocalSchedPerDomainNode with default value: {}",
                   DEFAULT_LOCAL_SCHED_PER_DOMAIN_NODE);
    } else {
        maxLocalSchedPerDomainNode_ = static_cast<unsigned short>(d1Flag.Get());
    }

    if (const auto &d2Flag(flags.GetD2()); d2Flag.IsNone() || d2Flag.Get() < 0) {
        YRLOG_WARN("flag d2 is invalid, set maxDomainSchedPerDomainNode with default value: {}",
                   DEFAULT_DOMAIN_SCHED_PER_DOMAIN_NODE);
    } else {
        maxDomainSchedPerDomainNode_ = static_cast<unsigned short>(d2Flag.Get());
    }

    metaStoreAddress_ = flags.GetMetaStoreAddress();
    globalSchedAddress_ = flags.GetIP();
    isScheduleTolerateAbnormal_ = flags.GetIsScheduleTolerateAbnormal();
    heartbeatTimeoutMs_ = flags.GetSystemTimeout();
    domainHeartbeatTimeoutMs_ = flags.GetDomainHeartbeatTimeout();
    pullResourceInterval_ = flags.GetPullResourceInterval();
    enableMetrics_ = flags.GetEnableMetrics();
    enablePrintResourceView_ = flags.GetEnablePrintResourceView();
    enableHorizontalScale_ = flags.GetEnableHorizontalScale();
    schedulePlugins_ = flags.GetSchedulePlugins();
    relaxed_ = flags.GetScheduleRelaxed();
    maxPriority_ = flags.GetMaxPriority();
    enablePreemption_ = flags.GetEnablePreemption();
    aggregatedStrategy_ = flags.GetAggregatedStrategy();
    // create http server
    httpServer_ = std::make_shared<HttpServer>(GLOBAL_SCHEDULER);
    apiRouteRegister_ = std::make_shared<DefaultHealthyRouter>(flags.GetNodeID());
    if (auto registerStatus(httpServer_->RegisterRoute(apiRouteRegister_)); registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register health check api router failed.");
    }
    // add agent api route
    agentApiRouteRegister_ = std::make_shared<AgentApiRouter>();
    agentApiRouteRegister_->InitQueryAgentHandler(globalSched_);
    agentApiRouteRegister_->InitEvictAgentHandler(globalSched_);
    agentApiRouteRegister_->InitGetSchedulingQueueHandler(globalSched_);
    agentApiRouteRegister_->InitQueryAgentCountHandler(metaStoreClient_);
    if (auto registerStatus(httpServer_->RegisterRoute(agentApiRouteRegister_));
        registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register agent api router failed.");
    }
    // add resources api route
    resourcesApiRouteRegister_ = std::make_shared<ResourcesApiRouter>();
    resourcesApiRouteRegister_->InitQueryResourcesInfoHandler(globalSched_);
    if (auto registerStatus(httpServer_->RegisterRoute(resourcesApiRouteRegister_));
        registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register resources api router failed.");
    }
}

Status GlobalSchedDriver::Start()
{
    if (maxLocalSchedPerDomainNode_ < MIN_SCHED_PER_DOMAIN_NODE
        || maxDomainSchedPerDomainNode_ < MIN_SCHED_PER_DOMAIN_NODE) {
        YRLOG_ERROR("maxLocalSchedPerDomainNode and maxDomainSchedPerDomainNode can't less than 2");
        return Status(StatusCode::FAILED);
    }
    auto domainSchedMgr = std::make_unique<DomainSchedMgr>(domainHeartbeatTimeoutMs_);
    auto localSchedMgr = std::make_unique<LocalSchedMgr>();

    ASSERT_IF_NULL(globalSched_);
    globalSched_->InitManager(std::move(domainSchedMgr), std::move(localSchedMgr));

    auto domainLauncher =
        std::make_shared<domain_scheduler::DomainSchedulerLauncher>(domain_scheduler::DomainSchedulerParam{
            "InnerDomainScheduler", globalSchedAddress_, metaStoreClient_, heartbeatTimeoutMs_, pullResourceInterval_,
            isScheduleTolerateAbnormal_, maxPriority_, enablePreemption_, relaxed_, enableMetrics_,
            enablePrintResourceView_, schedulePlugins_, aggregatedStrategy_, componentName_, enableHorizontalScale_ });
    auto domainActivator = std::make_shared<DomainActivator>(domainLauncher);
    auto topologyTree = std::make_unique<SchedTree>(maxLocalSchedPerDomainNode_, maxDomainSchedPerDomainNode_);
    auto globalSchedActor = std::make_shared<GlobalSchedActor>(GLOBAL_SCHED_ACTOR_NAME, metaStoreClient_,
                                                               std::move(domainActivator), std::move(topologyTree));
    if (const auto &status(globalSched_->Start(std::move(globalSchedActor))); !status.IsOk()) {
        YRLOG_ERROR("failed to start global scheduler");
        return status;
    }

    if (httpServer_) {
        (void)litebus::Spawn(httpServer_);
    }
    return Status::OK();
}

Status GlobalSchedDriver::Stop() const
{
    if (httpServer_) {
        litebus::Terminate(httpServer_->GetAID());
    }
    ASSERT_IF_NULL(globalSched_);
    return globalSched_->Stop();
}

void GlobalSchedDriver::Await() const
{
    globalSched_->Await();
    if (httpServer_) {
        litebus::Await(httpServer_->GetAID());
    }
}

std::shared_ptr<GlobalSched> GlobalSchedDriver::GetGlobalSched() const
{
    return globalSched_;
}

void GlobalSchedDriver::BindComponentName(const std::string &componentName)
{
    componentName_ = componentName;
}

}  // namespace functionsystem::global_scheduler