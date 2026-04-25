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

    void InitListByFunctionKeyHandler(std::shared_ptr<SnapManagerActor> snapActor)
    {
        auto handler = [snapActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "POST") {
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }

            bool useJsonFormat = request.headers.find("Type") == request.headers.end() ||
                request.headers.find("Type")->second == SNAP_JSON_FORMAT;

            ::messages::ListSnapshotsByFunctionKeyRequest req;
            if (!req.ParseFromString(request.body)) {
                YRLOG_ERROR("failed to parse ListSnapshotsByFunctionKeyRequest");
                return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST);
            }

            const auto &fk = req.functionkey();
            YRLOG_INFO("list snapshots by functionKey: tenantID={}, functionType={}, ns={}",
                       fk.tenantid(), fk.functiontype(), fk.namespace_());

            return litebus::Async(snapActor->GetAID(), &SnapManagerActor::ListCheckpointIDsByFunctionKey,
                                  fk.tenantid(), fk.functiontype(), fk.namespace_())
                .Then([useJsonFormat, req](const std::vector<std::string> &checkpointIDs)
                          -> litebus::Future<litebus::http::Response> {
                    ::messages::ListSnapshotsByFunctionKeyResponse rsp;
                    rsp.set_code(common::ERR_NONE);
                    rsp.set_message("success");
                    rsp.set_requestid(req.requestid());
                    for (const auto &id : checkpointIDs) {
                        rsp.add_checkpointids(id);
                    }
                    if (!useJsonFormat) {
                        return litebus::http::Ok(rsp.SerializeAsString());
                    }
                    google::protobuf::util::JsonOptions options;
                    std::string jsonStr;
                    (void)google::protobuf::util::MessageToJsonString(rsp, &jsonStr, options);
                    return litebus::http::Ok(jsonStr);
                });
        };
        RegisterHandler("/list-snapshots-by-function-key", handler);
    }

    void InitListByTenantHandler(std::shared_ptr<SnapManagerActor> snapActor)
    {
        auto handler = [snapActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "POST") {
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }

            bool useJsonFormat = request.headers.find("Type") == request.headers.end() ||
                request.headers.find("Type")->second == SNAP_JSON_FORMAT;

            ::messages::ListSnapshotsByTenantRequest req;
            if (!req.ParseFromString(request.body)) {
                YRLOG_ERROR("failed to parse ListSnapshotsByTenantRequest");
                return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST);
            }

            YRLOG_INFO("list snapshots by tenant: tenantID={}", req.tenantid());

            return litebus::Async(snapActor->GetAID(), &SnapManagerActor::ListCheckpointIDsByTenant, req.tenantid())
                .Then([useJsonFormat, req](const std::vector<std::string> &checkpointIDs)
                          -> litebus::Future<litebus::http::Response> {
                    ::messages::ListSnapshotsByTenantResponse rsp;
                    rsp.set_code(common::ERR_NONE);
                    rsp.set_message("success");
                    rsp.set_requestid(req.requestid());
                    for (const auto &id : checkpointIDs) {
                        rsp.add_checkpointids(id);
                    }
                    if (!useJsonFormat) {
                        return litebus::http::Ok(rsp.SerializeAsString());
                    }
                    google::protobuf::util::JsonOptions options;
                    std::string jsonStr;
                    (void)google::protobuf::util::MessageToJsonString(rsp, &jsonStr, options);
                    return litebus::http::Ok(jsonStr);
                });
        };
        RegisterHandler("/list-snapshots-by-tenant", handler);
    }

    void InitDeleteSnapshotHandler(std::shared_ptr<SnapManagerActor> snapActor)
    {
        auto handler = [snapActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            if (request.method != "POST") {
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }

            bool useJsonFormat = request.headers.find("Type") == request.headers.end() ||
                request.headers.find("Type")->second == SNAP_JSON_FORMAT;

            ::messages::DeleteSnapshotRequest req;
            if (!req.ParseFromString(request.body)) {
                YRLOG_ERROR("failed to parse DeleteSnapshotRequest");
                return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST);
            }

            YRLOG_INFO("delete snapshot by checkpointID={}", req.checkpointid());

            return litebus::Async(snapActor->GetAID(), &SnapManagerActor::DeleteSnapshot, req.checkpointid())
                .Then([useJsonFormat, req](const Status &status) -> litebus::Future<litebus::http::Response> {
                    ::messages::DeleteSnapshotResponse rsp;
                    rsp.set_requestid(req.requestid());
                    rsp.set_code(static_cast<common::ErrorCode>(status.StatusCode()));
                    rsp.set_message(status.RawMessage());
                    if (!useJsonFormat) {
                        return litebus::http::Ok(rsp.SerializeAsString());
                    }
                    google::protobuf::util::JsonOptions options;
                    std::string jsonStr;
                    (void)google::protobuf::util::MessageToJsonString(rsp, &jsonStr, options);
                    return litebus::http::Ok(jsonStr);
                });
        };
        RegisterHandler("/delete-snapshot", handler);
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
