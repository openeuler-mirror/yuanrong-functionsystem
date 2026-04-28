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

#ifndef FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_DRIVER_H
#define FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_DRIVER_H

#include "common/http/http_server.h"
#include "common/status/status.h"
#include "common/utils/module_driver.h"
#include "function_master/instance_manager/quota_manager/quota_manager_actor.h"
#include "group_manager_actor.h"
#include "instance_manager_actor.h"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <nlohmann/json.hpp>
#include <vector>

namespace functionsystem::instance_manager {

const std::string APP_JSON_FORMAT = "application/json";
const std::string JSON_FORMAT = "json";
constexpr uint64_t QUERY_INSTANCES_MAX_PAGE_SIZE = 1000;
const std::string JSON_SERIALIZE_ERROR = "{\"error\":\"Failed to serialize response\"}";

struct QueryInstancesPagination {
    bool enabled = false;
    uint64_t page = 1;
    uint64_t pageSize = 10;
    std::string error;
};

struct QueryInstancesPageRange {
    uint64_t start = 0;
    uint64_t end = 0;
};

inline bool IsTenantInstanceMatched(const resources::InstanceInfo &instance, const std::string &tenantID,
                                    const std::string &instanceID, bool isSystemTenant)
{
    if (!isSystemTenant && instance.tenantid() != tenantID) {
        return false;
    }
    return instanceID.empty() || instance.instanceid() == instanceID;
}

inline std::vector<int> CollectSortedTenantInstanceIndexes(
    const google::protobuf::RepeatedPtrField<resources::InstanceInfo> &instances, const std::string &tenantID,
    const std::string &instanceID, bool isSystemTenant)
{
    std::vector<int> matchedIndexes;
    matchedIndexes.reserve(static_cast<size_t>(instances.size()));
    for (int index = 0; index < instances.size(); ++index) {
        const auto &instance = instances.Get(index);
        if (IsTenantInstanceMatched(instance, tenantID, instanceID, isSystemTenant)) {
            matchedIndexes.push_back(index);
        }
    }
    std::sort(matchedIndexes.begin(), matchedIndexes.end(), [&instances](int lhs, int rhs) {
        const auto &lhsInstance = instances.Get(lhs);
        const auto &rhsInstance = instances.Get(rhs);
        if (lhsInstance.instanceid() != rhsInstance.instanceid()) {
            return lhsInstance.instanceid() < rhsInstance.instanceid();
        }
        return lhsInstance.tenantid() < rhsInstance.tenantid();
    });
    return matchedIndexes;
}

inline QueryInstancesPageRange GetQueryInstancesPageRange(uint64_t total, const QueryInstancesPagination &pagination)
{
    QueryInstancesPageRange range{ .start = 0, .end = total };
    if (!pagination.enabled) {
        return range;
    }

    if (pagination.page > 1) {
        range.start = pagination.page - 1 > total / pagination.pageSize
                          ? total
                          : (pagination.page - 1) * pagination.pageSize;
    }
    range.end = range.start < total ? range.start + std::min(pagination.pageSize, total - range.start) : total;
    return range;
}

inline bool ParsePositiveUInt64Param(
    const std::string &value, const std::string &name, uint64_t &out, std::string &error)
{
    try {
        size_t parsed = 0;
        auto result = std::stoull(value, &parsed);
        if (parsed != value.size() || result == 0) {
            error = name + " must be a positive integer";
            return false;
        }
        out = result;
        return true;
    } catch (const std::exception &) {
        error = name + " must be a positive integer";
        return false;
    }
}

template <typename QueryMap>
QueryInstancesPagination ParseQueryInstancesPagination(const QueryMap &query)
{
    QueryInstancesPagination pagination;
    auto pageIt = query.find("page");
    auto pageSizeIt = query.find("page_size");
    bool hasPage = pageIt != query.end() && !pageIt->second.empty();
    bool hasPageSize = pageSizeIt != query.end() && !pageSizeIt->second.empty();
    if (!hasPage && !hasPageSize) {
        return pagination;
    }

    pagination.enabled = true;
    if (hasPage && !ParsePositiveUInt64Param(pageIt->second, "page", pagination.page, pagination.error)) {
        return pagination;
    }
    if (hasPageSize &&
        !ParsePositiveUInt64Param(pageSizeIt->second, "page_size", pagination.pageSize, pagination.error)) {
        return pagination;
    }
    if (pagination.pageSize > QUERY_INSTANCES_MAX_PAGE_SIZE) {
        pagination.error = "page_size exceeds maximum limit";
        return pagination;
    }
    return pagination;
}

inline litebus::Future<litebus::http::Response> JsonResponseOrInternalError(const nlohmann::json &responseJson)
{
    try {
        return litebus::http::Ok(responseJson.dump(), litebus::http::ResponseBodyType::JSON);
    } catch (const std::exception &e) {
        YRLOG_ERROR("Failed to serialize tenant instances response: {}", e.what());
    } catch (...) {
        YRLOG_ERROR("Failed to serialize tenant instances response: unknown exception");
    }
    return HttpResponse(litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                        JSON_SERIALIZE_ERROR,
                        litebus::http::ResponseBodyType::JSON);
}

class InstancesApiRouter : public ApiRouterRegister {
public:
    void RegisterHandler(const std::string &url, const HttpHandler &handler) const override
    {
        ApiRouterRegister::RegisterHandler(url, handler);
    };

    void InitQueryNamedInsHandler(std::shared_ptr<InstanceManagerActor> imActor)
    {
        auto namedInsHandler = [imActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "GET") {
                YRLOG_ERROR("Invalid request method.");
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }
            bool useJsonFormat = request.headers.find("Content-Type") == request.headers.end() ||
                                 request.headers.find("Content-Type")->second == APP_JSON_FORMAT;

            auto req = std::make_shared<messages::QueryNamedInsRequest>();
            if (request.body.empty() || !req->ParseFromString(request.body)) {
                auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
                req->set_requestid(requestID);
                YRLOG_WARN("invalid query namedIns request body. use generated requestID({})", requestID);
            }
            YRLOG_INFO("{}|query named ins", req->requestid());
            return litebus::Async(imActor->GetAID(), &InstanceManagerActor::QueryNamedIns, req)
                .Then([useJsonFormat](const messages::QueryNamedInsResponse &rsp)
                          -> litebus::Future<litebus::http::Response> {
                    if (!useJsonFormat) {
                        return litebus::http::Ok(rsp.SerializeAsString());
                    }
                    google::protobuf::util::JsonOptions options;
                    std::string jsonStr;
                    (void)google::protobuf::util::MessageToJsonString(rsp, &jsonStr, options);
                    return litebus::http::Ok(jsonStr);
                });
        };
        RegisterHandler("/named-ins", namedInsHandler);
	}
	
	void InitQueryInstancesHandler(std::shared_ptr<InstanceManagerActor> imActor)
	{
        auto handler = [imActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "GET") {
                YRLOG_ERROR("Invalid request method.");
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }
            bool useJsonFormat = request.headers.find("Type") == request.headers.end() ||
                request.headers.find("Type")->second == JSON_FORMAT;
 
            auto req = std::make_shared<messages::QueryInstancesInfoRequest>();
            auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
            req->set_requestid(requestID);
 
            YRLOG_INFO("{}|query instanceinfo", requestID);
            return litebus::Async(imActor->GetAID(), &InstanceManagerActor::QueryInstancesInfo, req)
                .Then([useJsonFormat](const messages::QueryInstancesInfoResponse &rsp)
                          -> litebus::Future<litebus::http::Response> {
                    if (!useJsonFormat) {
                        return litebus::http::Ok(rsp.SerializeAsString());
                    }
                    google::protobuf::util::JsonOptions options;
                    std::string jsonStr;
                    (void)google::protobuf::util::MessageToJsonString(rsp, &jsonStr, options);
                    return litebus::http::Ok(jsonStr);
                });
        };
        RegisterHandler("/queryinstances", handler);
    }

    void InitQueryDebugInstancesHandler(std::shared_ptr<InstanceManagerActor> imActor)
    {
        auto handler = [imActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "GET") {
                YRLOG_ERROR("Invalid request method.");
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }

            bool useJsonFormat = request.headers.find("Type") == request.headers.end() ||
                request.headers.find("Type")->second == JSON_FORMAT;
            auto req = std::make_shared<messages::QueryDebugInstanceInfosRequest>();
            auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
            req->set_requestid(requestID);

            YRLOG_INFO("{}|query debuginstanceinfo", requestID);

            return litebus::Async(imActor->GetAID(), &InstanceManagerActor::QueryDebugInstancesInfo, req)
                .Then([useJsonFormat](const messages::QueryDebugInstanceInfosResponse &rsp)
                          -> litebus::Future<litebus::http::Response> {
                    if (!useJsonFormat) {
                        return litebus::http::Ok(rsp.SerializeAsString());
                    }
                    google::protobuf::util::JsonOptions options;
                    std::string jsonStr;
                    (void)google::protobuf::util::MessageToJsonString(rsp, &jsonStr, options);
                    return litebus::http::Ok(jsonStr);
                });
        };
        RegisterHandler("/query-debug-instances", handler);
    }

    // Query tenant instances with containerID and proxyGrpcAddress
    // If tenant_id is system tenant (configurable), returns all tenants' instances
    void InitQueryTenantInstancesHandler(std::shared_ptr<InstanceManagerActor> imActor)
    {
        auto handler = [imActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "GET") {
                YRLOG_ERROR("Invalid request method for tenant instances query.");
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED,
                                    "Only GET method is allowed",
                                    litebus::http::ResponseBodyType::JSON);
            }

            // Parse tenant_id from query parameters (required)
            std::string tenantID;
            auto tenantIt = request.url.query.find("tenant_id");
            if (tenantIt == request.url.query.end() || tenantIt->second.empty()) {
                YRLOG_ERROR("Missing tenant_id parameter in query tenant instances request.");
                return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST,
                                    "{\"error\": \"Missing tenant_id parameter\"}",
                                    litebus::http::ResponseBodyType::JSON);
            }
            tenantID = tenantIt->second;

            // Parse instance_id from query parameters (optional)
            std::string instanceID;
            auto instanceIt = request.url.query.find("instance_id");
            if (instanceIt != request.url.query.end() && !instanceIt->second.empty()) {
                instanceID = instanceIt->second;
            }

            auto pagination = ParseQueryInstancesPagination(request.url.query);
            if (!pagination.error.empty()) {
                YRLOG_ERROR("Invalid pagination parameter: {}", pagination.error);
                nlohmann::json errorJson;
                errorJson["error"] = pagination.error;
                try {
                    return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST,
                                        errorJson.dump(),
                                        litebus::http::ResponseBodyType::JSON);
                } catch (const std::exception &e) {
                    YRLOG_ERROR("Failed to serialize pagination error response: {}", e.what());
                } catch (...) {
                    YRLOG_ERROR("Failed to serialize pagination error response: unknown exception");
                }
                return HttpResponse(litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                                    JSON_SERIALIZE_ERROR,
                                    litebus::http::ResponseBodyType::JSON);
            }

            // Check if this is a system tenant query
            bool isSystemTenant = (tenantID == imActor->GetSystemTenantID());

            auto req = std::make_shared<messages::QueryInstancesInfoRequest>();
            req->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

            // Capture tenantID, instanceID, isSystemTenant, and pagination for the lambda
            return litebus::Async(imActor->GetAID(), &InstanceManagerActor::QueryInstancesInfo, req)
                .Then([tenantID, instanceID, isSystemTenant, pagination](
                          const messages::QueryInstancesInfoResponse &rsp)
                          -> litebus::Future<litebus::http::Response> {
                    auto matchedIndexes =
                        CollectSortedTenantInstanceIndexes(rsp.instanceinfos(), tenantID, instanceID, isSystemTenant);
                    auto totalCount = matchedIndexes.size();
                    auto range = GetQueryInstancesPageRange(static_cast<uint64_t>(totalCount), pagination);

                    nlohmann::json instancesArray = nlohmann::json::array();
                    for (uint64_t index = range.start; index < range.end; ++index) {
                        const auto &instance = rsp.instanceinfos().Get(matchedIndexes[static_cast<size_t>(index)]);
                        google::protobuf::util::JsonOptions options;
                        options.add_whitespace = false;
                        std::string instanceJsonStr;
                        auto status = google::protobuf::util::MessageToJsonString(instance, &instanceJsonStr, options);
                        if (status.ok()) {
                            nlohmann::json instanceJson = nlohmann::json::parse(instanceJsonStr);
                            instancesArray.push_back(instanceJson);
                        } else {
                            YRLOG_WARN("Failed to convert instance to JSON: {}", status.ToString());
                        }
                    }

                    nlohmann::json responseJson;
                    responseJson["instances"] = instancesArray;
                    responseJson["count"] = totalCount;
                    responseJson["tenantID"] = tenantID;
                    if (pagination.enabled) {
                        responseJson["page"] = pagination.page;
                        responseJson["pageSize"] = pagination.pageSize;
                    }
                    
                    // Add a flag to indicate if this is a system tenant query
                    if (isSystemTenant) {
                        responseJson["isSystemTenant"] = true;
                    }

                    // Add instanceID to response if it was specified in request
                    if (!instanceID.empty()) {
                        responseJson["instanceID"] = instanceID;
                    }

                    return JsonResponseOrInternalError(responseJson);
                });
        };
        RegisterHandler("/query-tenant-instances", handler);
    }
};

class InstanceManagerDriver : public ModuleDriver {
public:
    explicit InstanceManagerDriver(std::shared_ptr<InstanceManagerActor> instanceManagerActor,
                                   std::shared_ptr<GroupManagerActor> groupManagerActor,
                                   std::shared_ptr<function_master::QuotaManagerActor> quotaManagerActor = nullptr);

    ~InstanceManagerDriver() override = default;

    Status Start() override;

    Status Stop() override;

    void Await() override;

private:
    std::shared_ptr<InstanceManagerActor> instanceManagerActor_{ nullptr };
    std::shared_ptr<GroupManagerActor> groupManagerActor_{ nullptr };
    std::shared_ptr<function_master::QuotaManagerActor> quotaManagerActor_{ nullptr };

    std::shared_ptr<HttpServer> httpServer_{nullptr};
    std::shared_ptr<InstancesApiRouter> instanceApiRouteRegister_ = nullptr;
};  // class InstanceManagerDriver
}  // namespace functionsystem::instance_manager

#endif  // FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_DRIVER_H
