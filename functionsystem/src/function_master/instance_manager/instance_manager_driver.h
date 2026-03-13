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
#include <nlohmann/json.hpp>

namespace functionsystem::instance_manager {

const std::string APP_JSON_FORMAT = "application/json";
const std::string JSON_FORMAT = "json";
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

            // Check if this is a system tenant query
            bool isSystemTenant = (tenantID == imActor->GetSystemTenantID());

            auto req = std::make_shared<messages::QueryInstancesInfoRequest>();
            req->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

            // Capture tenantID, instanceID, and isSystemTenant for the lambda
            return litebus::Async(imActor->GetAID(), &InstanceManagerActor::QueryInstancesInfo, req)
                .Then([tenantID, instanceID, isSystemTenant](const messages::QueryInstancesInfoResponse &rsp)
                          -> litebus::Future<litebus::http::Response> {
                    // Filter instances based on tenantID
                    // If isSystemTenant is true, return all instances; otherwise filter by tenantID
                    nlohmann::json instancesArray = nlohmann::json::array();

                    for (const auto &instance : rsp.instanceinfos()) {
                        // If system tenant, return all instances; otherwise filter by tenantID
                        if (isSystemTenant || instance.tenantid() == tenantID) {
                            // If instanceID is specified, also filter by instanceID
                            if (!instanceID.empty() && instance.instanceid() != instanceID) {
                                continue;
                            }

                            // Use protobuf's JSON converter
                            google::protobuf::util::JsonOptions options;
                            options.add_whitespace = false;
                            std::string instanceJsonStr;
                            auto status = google::protobuf::util::MessageToJsonString(instance, &instanceJsonStr, options);
                            if (status.ok()) {
                                // Parse the JSON string and add to array
                                nlohmann::json instanceJson = nlohmann::json::parse(instanceJsonStr);
                                instancesArray.push_back(instanceJson);
                            } else {
                                YRLOG_WARN("Failed to convert instance to JSON: {}", status.ToString());
                            }
                        }
                    }

                    nlohmann::json responseJson;
                    responseJson["instances"] = instancesArray;
                    responseJson["count"] = instancesArray.size();
                    responseJson["tenantID"] = tenantID;
                    
                    // Add a flag to indicate if this is a system tenant query
                    if (isSystemTenant) {
                        responseJson["isSystemTenant"] = true;
                    }

                    // Add instanceID to response if it was specified in request
                    if (!instanceID.empty()) {
                        responseJson["instanceID"] = instanceID;
                    }

                    std::string jsonStr = responseJson.dump();
                    return litebus::http::Ok(jsonStr, litebus::http::ResponseBodyType::JSON);
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
