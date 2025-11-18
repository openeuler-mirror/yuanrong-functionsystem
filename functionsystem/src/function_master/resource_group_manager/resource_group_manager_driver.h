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

#ifndef FUNCTION_MASTER_RESOURCE_GROUP_MANAGER_DRIVER_ACTOR_H
#define FUNCTION_MASTER_RESOURCE_GROUP_MANAGER_DRIVER_ACTOR_H

#include "common/http/http_server.h"
#include "common/utils/module_driver.h"
#include "resource_group_manager_actor.h"

namespace functionsystem::resource_group_manager {
const std::string JSON_FORMAT = "json";

class ResourceGroupApiRouter : public ApiRouterRegister {
public:
    void RegisterHandler(const std::string &url, const HttpHandler &handler) const override
    {
        ApiRouterRegister::RegisterHandler(url, handler);
    };

    void InitQueryRGroupHandler(std::shared_ptr<ResourceGroupManagerActor> rgActor)
    {
        auto queryRGroupHandler = [rgActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "POST") {
                YRLOG_ERROR("Invalid query resource group request method({}), which should be POST", request.method);
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }
            bool useJsonFormat = request.headers.find("Type") == request.headers.end() ||
                                 request.headers.find("Type")->second == JSON_FORMAT;

            auto req = std::make_shared<messages::QueryResourceGroupRequest>();
            auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
            if (useJsonFormat) {
                try {
                    auto reqJson = nlohmann::json::parse(request.body);
                    req->set_requestid(reqJson.value("requestID", requestID));
                    req->set_rgroupname(reqJson.value("rGroupName", ""));
                } catch (std::exception &e) {
                    req->set_requestid(requestID);
                    YRLOG_ERROR("parse query resource group req body failed, error: {}", e.what());
                }
            } else {
                if (!req->ParseFromString(request.body)) {
                    req->set_requestid(requestID);
                    YRLOG_WARN("invalid query resource group request body. use generated requestID({}) "
                        "and return all resource groups", requestID);
                }
            }
            YRLOG_INFO("{}|query resource group, name({}), useJson({})", req->requestid(), req->rgroupname(),
                       useJsonFormat);
            return litebus::Async(rgActor->GetAID(), &ResourceGroupManagerActor::QueryResourceGroup, req)
                .Then([useJsonFormat](const messages::QueryResourceGroupResponse &rsp)
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
        RegisterHandler("/rgroup", queryRGroupHandler);
    }
};

class ResourceGroupManagerDriver : public ModuleDriver {
public:
    explicit ResourceGroupManagerDriver(std::shared_ptr<ResourceGroupManagerActor> resourceGroupManagerActor)
        : resourceGroupManagerActor_(resourceGroupManagerActor)
    {
    }

    ~ResourceGroupManagerDriver() override = default;

    Status Start() override;

    Status Stop() override;

    void Await() override;

private:
    std::shared_ptr<ResourceGroupManagerActor> resourceGroupManagerActor_;

    std::shared_ptr<HttpServer> httpServer_{nullptr};
    std::shared_ptr<ResourceGroupApiRouter> rGroupApiRouteRegister_ = nullptr;
};
}  // namespace functionsystem::resource_group_manager

#endif  // FUNCTION_MASTER_RESOURCE_GROUP_MANAGER_DRIVER_ACTOR_H
