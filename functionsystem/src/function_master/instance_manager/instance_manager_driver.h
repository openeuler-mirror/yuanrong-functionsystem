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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/http/http_server.h"
#include "common/status/status.h"
#include "common/utils/module_driver.h"
#include "function_master/instance_manager/quota_manager/quota_manager_actor.h"
#include "group_manager_actor.h"
#include "instance_manager_actor.h"

namespace functionsystem::instance_manager {

const std::string APP_JSON_FORMAT = "application/json";
const std::string JSON_FORMAT = "json";
const std::string QUERY_INSTANCES_SUMMARY_FIELDS = "summary";
constexpr uint64_t QUERY_INSTANCES_MAX_PAGE_SIZE = 1000;
constexpr auto QUERY_TENANT_INSTANCES_CACHE_TTL = std::chrono::milliseconds(1000);
constexpr size_t QUERY_TENANT_INSTANCES_CACHE_MAX_ENTRIES = 256;
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

struct QueryTenantInstancesCacheEntry {
    std::chrono::steady_clock::time_point expiresAt;
    std::string body;
};

inline std::mutex g_queryTenantInstancesCacheMutex;
inline std::unordered_map<std::string, QueryTenantInstancesCacheEntry> g_queryTenantInstancesCache;

inline std::string JsonEscape(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    const char *digits = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += digits[(ch >> 4) & 0x0F];
                    escaped += digits[ch & 0x0F];
                } else {
                    escaped += static_cast<char>(ch);
                }
                break;
        }
    }
    return escaped;
}

inline void AppendJsonStringField(std::string &body, const std::string &key, const std::string &value,
                                  bool &needComma)
{
    if (needComma) {
        body += ',';
    }
    body += '"';
    body += key;
    body += "\":\"";
    body += JsonEscape(value);
    body += '"';
    needComma = true;
}

inline void AppendJsonUIntField(std::string &body, const std::string &key, uint64_t value, bool &needComma)
{
    if (needComma) {
        body += ',';
    }
    body += '"';
    body += key;
    body += "\":";
    body += std::to_string(value);
    needComma = true;
}

inline void AppendJsonBoolField(std::string &body, const std::string &key, bool value, bool &needComma)
{
    if (needComma) {
        body += ',';
    }
    body += '"';
    body += key;
    body += "\":";
    body += value ? "true" : "false";
    needComma = true;
}

inline std::string BuildQueryTenantInstancesCacheKey(const HttpRequest &request)
{
    std::vector<std::pair<std::string, std::string>> queryItems(request.url.query.begin(), request.url.query.end());
    std::sort(queryItems.begin(), queryItems.end());

    std::string key = request.method;
    key += '|';
    key += request.url.path;
    for (const auto &[queryKey, queryValue] : queryItems) {
        key += '|';
        key += queryKey;
        key += '=';
        key += queryValue;
    }
    return key;
}

inline litebus::Option<std::string> GetQueryTenantInstancesCachedResponse(const std::string &cacheKey)
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> guard(g_queryTenantInstancesCacheMutex);
    auto it = g_queryTenantInstancesCache.find(cacheKey);
    if (it == g_queryTenantInstancesCache.end()) {
        return litebus::None();
    }
    if (now >= it->second.expiresAt) {
        (void)g_queryTenantInstancesCache.erase(it);
        return litebus::None();
    }
    return it->second.body;
}

inline void PutQueryTenantInstancesCachedResponse(const std::string &cacheKey, std::string body)
{
    std::lock_guard<std::mutex> guard(g_queryTenantInstancesCacheMutex);
    if (g_queryTenantInstancesCache.size() >= QUERY_TENANT_INSTANCES_CACHE_MAX_ENTRIES) {
        g_queryTenantInstancesCache.clear();
    }
    g_queryTenantInstancesCache[cacheKey] = QueryTenantInstancesCacheEntry{
        .expiresAt = std::chrono::steady_clock::now() + QUERY_TENANT_INSTANCES_CACHE_TTL,
        .body = std::move(body)
    };
}

inline bool IsTenantInstanceMatched(const resources::InstanceInfo &instance, const std::string &tenantID,
                                    const std::string &instanceID, const std::string &nodeID, bool isSystemTenant)
{
    if (!isSystemTenant && instance.tenantid() != tenantID) {
        return false;
    }
    if (!instanceID.empty() && instance.instanceid() != instanceID) {
        return false;
    }
    return nodeID.empty() || instance.functionproxyid() == nodeID;
}

inline std::vector<int> CollectSortedTenantInstanceIndexes(
    const google::protobuf::RepeatedPtrField<resources::InstanceInfo> &instances, const std::string &tenantID,
    const std::string &instanceID, const std::string &nodeID, bool isSystemTenant)
{
    std::vector<int> matchedIndexes;
    matchedIndexes.reserve(static_cast<size_t>(instances.size()));
    for (int index = 0; index < instances.size(); ++index) {
        const auto &instance = instances.Get(index);
        if (IsTenantInstanceMatched(instance, tenantID, instanceID, nodeID, isSystemTenant)) {
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

inline double GetInstanceResourceValue(const resources::InstanceInfo &instance, const std::string &resourceName)
{
    const auto &resources = instance.resources().resources();
    auto it = resources.find(resourceName);
    if (it == resources.end()) {
        return 0;
    }
    return it->second.scalar().value();
}

inline nlohmann::json BuildTenantInstanceSummaryJson(const resources::InstanceInfo &instance)
{
    nlohmann::json statusJson;
    statusJson["code"] = instance.instancestatus().code();
    statusJson["exitCode"] = instance.instancestatus().exitcode();
    statusJson["msg"] = instance.instancestatus().msg();
    statusJson["type"] = instance.instancestatus().type();
    statusJson["errCode"] = instance.instancestatus().errcode();

    nlohmann::json instanceJson;
    instanceJson["instanceID"] = instance.instanceid();
    instanceJson["tenantID"] = instance.tenantid();
    instanceJson["function"] = instance.function();
    instanceJson["instanceStatus"] = statusJson;
    instanceJson["startTime"] = instance.starttime();
    instanceJson["required_cpu"] = GetInstanceResourceValue(instance, "CPU");
    instanceJson["required_mem"] = GetInstanceResourceValue(instance, "Memory");
    instanceJson["required_gpu"] = GetInstanceResourceValue(instance, "GPU");
    instanceJson["required_npu"] = GetInstanceResourceValue(instance, "NPU/.+/count");
    return instanceJson;
}

inline Status ConvertTenantInstanceToJsonString(
    const resources::InstanceInfo &instance, bool useSummaryFields, std::string &instanceJson)
{
    if (useSummaryFields) {
        try {
            instanceJson = BuildTenantInstanceSummaryJson(instance).dump();
        } catch (const std::exception &e) {
            return Status(StatusCode::FAILED, e.what());
        }
        return Status::OK();
    }

    google::protobuf::util::JsonOptions options;
    options.add_whitespace = false;
    auto status = google::protobuf::util::MessageToJsonString(instance, &instanceJson, options);
    if (!status.ok()) {
        return Status(StatusCode::FAILED, status.ToString());
    }
    return Status::OK();
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

inline Status BuildTenantInstancesResponseBody(const messages::QueryInstancesInfoResponse &rsp,
                                               const std::string &tenantID,
                                               const std::string &instanceID,
                                               const std::string &nodeID,
                                               bool isSystemTenant,
                                               const QueryInstancesPagination &pagination,
                                               bool useSummaryFields,
                                               std::string &body)
{
    body.clear();
    body.reserve(static_cast<size_t>(rsp.ByteSizeLong()) + 256);
    body += "{\"instances\":[";

    bool needInstanceComma = false;
    for (const auto &instance : rsp.instanceinfos()) {
        std::string instanceJson;
        auto status = ConvertTenantInstanceToJsonString(instance, useSummaryFields, instanceJson);
        if (!status.IsOk()) {
            YRLOG_WARN("Failed to convert instance to JSON: {}", status.ToString());
            continue;
        }
        if (needInstanceComma) {
            body += ',';
        }
        body += instanceJson;
        needInstanceComma = true;
    }

    body += ']';
    bool needComma = true;
    auto totalCount = rsp.totalcount();
    if (totalCount == 0 && rsp.instanceinfos_size() > 0) {
        totalCount = static_cast<uint64_t>(rsp.instanceinfos_size());
    }
    AppendJsonUIntField(body, "count", totalCount, needComma);
    AppendJsonStringField(body, "tenantID", tenantID, needComma);
    if (pagination.enabled) {
        AppendJsonUIntField(body, "page", rsp.page() == 0 ? pagination.page : rsp.page(), needComma);
        AppendJsonUIntField(body, "pageSize", rsp.pagesize() == 0 ? pagination.pageSize : rsp.pagesize(), needComma);
    }
    if (isSystemTenant) {
        AppendJsonBoolField(body, "isSystemTenant", true, needComma);
    }
    if (!instanceID.empty()) {
        AppendJsonStringField(body, "instanceID", instanceID, needComma);
    }
    if (!nodeID.empty()) {
        AppendJsonStringField(body, "nodeID", nodeID, needComma);
    }
    body += '}';
    return Status::OK();
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

            std::string nodeID;
            auto nodeIt = request.url.query.find("node_id");
            if (nodeIt != request.url.query.end() && !nodeIt->second.empty()) {
                nodeID = nodeIt->second;
            }
            bool useSummaryFields = false;
            auto fieldsIt = request.url.query.find("fields");
            if (fieldsIt != request.url.query.end() && fieldsIt->second == QUERY_INSTANCES_SUMMARY_FIELDS) {
                useSummaryFields = true;
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
            const auto cacheKey = BuildQueryTenantInstancesCacheKey(request);
            auto cachedResponse = GetQueryTenantInstancesCachedResponse(cacheKey);
            if (cachedResponse.IsSome()) {
                YRLOG_DEBUG("query tenant instances cache hit, key: {}", cacheKey);
                return litebus::http::Ok(cachedResponse.Get(), litebus::http::ResponseBodyType::JSON);
            }

            auto req = std::make_shared<messages::QueryInstancesInfoRequest>();
            req->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
            req->set_tenantid(tenantID);
            req->set_instanceid(instanceID);
            req->set_nodeid(nodeID);
            req->set_includealltenants(isSystemTenant);
            if (useSummaryFields) {
                req->set_fields(QUERY_INSTANCES_SUMMARY_FIELDS);
            }
            if (pagination.enabled) {
                req->set_paginationenabled(true);
                req->set_page(pagination.page);
                req->set_pagesize(pagination.pageSize);
            }

            // Filtering, summary field trimming, and pagination are pushed down to master.
            return litebus::Async(imActor->GetAID(), &InstanceManagerActor::QueryInstancesInfo, req)
                .Then([tenantID, instanceID, nodeID, isSystemTenant, pagination, useSummaryFields, cacheKey](
                          const messages::QueryInstancesInfoResponse &rsp)
                          -> litebus::Future<litebus::http::Response> {
                    std::string responseBody;
                    auto status = BuildTenantInstancesResponseBody(rsp, tenantID, instanceID, nodeID, isSystemTenant,
                                                                    pagination, useSummaryFields, responseBody);
                    if (!status.IsOk()) {
                        YRLOG_ERROR("Failed to serialize tenant instances response: {}", status.ToString());
                        return HttpResponse(litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                                            JSON_SERIALIZE_ERROR,
                                            litebus::http::ResponseBodyType::JSON);
                    }

                    PutQueryTenantInstancesCachedResponse(cacheKey, responseBody);
                    YRLOG_DEBUG("query tenant instances cache stored, key: {}, instances: {}, totalCount: {}",
                                cacheKey, rsp.instanceinfos_size(), rsp.totalcount());
                    return litebus::http::Ok(std::move(responseBody), litebus::http::ResponseBodyType::JSON);
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
