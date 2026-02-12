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

#ifndef FUNCTION_MASTER_SNAP_MANAGER_SNAP_MANAGER_DRIVER_H
#define FUNCTION_MASTER_SNAP_MANAGER_SNAP_MANAGER_DRIVER_H

#include "common/http/http_server.h"
#include "common/status/status.h"
#include "common/utils/module_driver.h"
#include "snap_manager_actor.h"

namespace functionsystem::snap_manager {

const std::string SNAP_JSON_FORMAT = "json";

class SnapApiRouter : public ApiRouterRegister {
public:
    void RegisterHandler(const std::string &url, const HttpHandler &handler) const override
    {
        ApiRouterRegister::RegisterHandler(url, handler);
    };

    void InitQuerySnapshotHandler(std::shared_ptr<SnapManagerActor> snapActor)
    {
        auto handler = [snapActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "GET") {
                YRLOG_ERROR("Invalid request method.");
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }

            bool useJsonFormat = request.headers.find("Type") == request.headers.end() ||
                request.headers.find("Type")->second == SNAP_JSON_FORMAT;

            std::string snapshotID = request.body;
            if (snapshotID.empty()) {
                YRLOG_ERROR("query-snapshot: snapshotID is empty");
                return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST);
            }

            YRLOG_INFO("query snapshot: {}", snapshotID);

            return litebus::Async(snapActor->GetAID(), &SnapManagerActor::GetSnapshotMetadata, snapshotID)
                .Then([useJsonFormat](const litebus::Option<SnapshotMetadata> &result)
                          -> litebus::Future<litebus::http::Response> {
                    if (result.IsNone()) {
                        return HttpResponse(litebus::http::ResponseCode::NOT_FOUND);
                    }
                    if (!useJsonFormat) {
                        return litebus::http::Ok(result.Get().SerializeAsString());
                    }
                    google::protobuf::util::JsonOptions options;
                    std::string jsonStr;
                    (void)google::protobuf::util::MessageToJsonString(result.Get(), &jsonStr, options);
                    return litebus::http::Ok(jsonStr);
                });
        };
        RegisterHandler("/query-snapshot", handler);
    }

    void InitListSnapshotsHandler(std::shared_ptr<SnapManagerActor> snapActor)
    {
        auto handler = [snapActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "GET") {
                YRLOG_ERROR("Invalid request method.");
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }

            bool useJsonFormat = request.headers.find("Type") == request.headers.end() ||
                request.headers.find("Type")->second == SNAP_JSON_FORMAT;

            std::string functionID = request.body;
            if (functionID.empty()) {
                YRLOG_ERROR("list-snapshots: functionID is empty");
                return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST);
            }

            YRLOG_INFO("list snapshots for function: {}", functionID);

            return litebus::Async(snapActor->GetAID(), &SnapManagerActor::ListSnapshotsByFunction, functionID)
                .Then([useJsonFormat](const std::vector<SnapshotMetadata> &snapshots)
                          -> litebus::Future<litebus::http::Response> {
                    if (!useJsonFormat) {
                        std::string result;
                        for (const auto &snap : snapshots) {
                            result += snap.SerializeAsString();
                        }
                        return litebus::http::Ok(result);
                    }
                    google::protobuf::util::JsonOptions options;
                    std::string jsonArray = "[";
                    for (size_t i = 0; i < snapshots.size(); ++i) {
                        std::string jsonStr;
                        (void)google::protobuf::util::MessageToJsonString(snapshots[i], &jsonStr, options);
                        if (i > 0) {
                            jsonArray += ",";
                        }
                        jsonArray += jsonStr;
                    }
                    jsonArray += "]";
                    return litebus::http::Ok(jsonArray);
                });
        };
        RegisterHandler("/list-snapshots", handler);
    }
};

class SnapManagerDriver : public ModuleDriver {
public:
    explicit SnapManagerDriver(std::shared_ptr<SnapManagerActor> snapManagerActor);

    ~SnapManagerDriver() override = default;

    Status Start() override;

    Status Stop() override;

    void Await() override;

private:
    std::shared_ptr<SnapManagerActor> snapManagerActor_{ nullptr };

    std::shared_ptr<HttpServer> httpServer_{nullptr};
    std::shared_ptr<SnapApiRouter> snapApiRouteRegister_ = nullptr;
};  // class SnapManagerDriver
}  // namespace functionsystem::snap_manager

#endif  // FUNCTION_MASTER_SNAP_MANAGER_SNAP_MANAGER_DRIVER_H
