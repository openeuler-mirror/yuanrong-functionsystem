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

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/http/http_server.h"
#include "common/status/status.h"
#include "common/utils/module_driver.h"
#include "snap_manager_actor.h"

namespace functionsystem::snap_manager {

const std::string SNAP_JSON_FORMAT = "json";

class SnapApiRouter : public ApiRouterRegister {
public:
    virtual ~SnapApiRouter() = default;

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

    void InitReusableSnapshotsHandler(std::shared_ptr<SnapManagerActor> snapActor)
    {
        auto handler = [snapActor](const HttpRequest &request) -> litebus::Future<HttpResponse> {
            const auto tenant = request.url.query.find("tenant_id");
            if (tenant == request.url.query.end() || tenant->second.empty()) {
                return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST, "tenant_id is required");
            }
            if (request.method == "GET") {
                const auto snapshot = request.url.query.find("snapshot_id");
                if (snapshot != request.url.query.end() && !snapshot->second.empty()) {
                    ::messages::GetReusableSnapshotRequest get;
                    get.set_tenantid(tenant->second);
                    get.set_snapshotid(snapshot->second);
                    return litebus::Async(snapActor->GetAID(), &SnapManagerActor::GetReusableSnapshot, get)
                        .Then([](const ::messages::GetReusableSnapshotResponse &response)
                                  -> litebus::Future<litebus::http::Response> {
                            if (response.code() != common::ERR_NONE) {
                                return HttpResponse(litebus::http::ResponseCode::NOT_FOUND, response.message());
                            }
                            nlohmann::json body;
                            body["snapshotId"] = response.snapshotinfo().snapshotid();
                            body["names"] = nlohmann::json::array();
                            for (const auto &name : response.snapshotinfo().names()) {
                                body["names"].push_back(name);
                            }
                            return litebus::http::Ok(body.dump(), litebus::http::ResponseBodyType::JSON);
                        });
                }
                ::messages::ListReusableSnapshotsRequest list;
                list.set_tenantid(tenant->second);
                if (const auto name = request.url.query.find("name"); name != request.url.query.end()) {
                    list.set_name(name->second);
                }
                if (const auto token = request.url.query.find("pageToken"); token != request.url.query.end()) {
                    list.set_pagetoken(token->second);
                }
                if (const auto size = request.url.query.find("pageSize"); size != request.url.query.end()) {
                    try {
                        size_t parsed = 0;
                        const auto value = std::stoul(size->second, &parsed);
                        if (parsed != size->second.size() || value > std::numeric_limits<uint32_t>::max()) {
                            return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST, "invalid pageSize");
                        }
                        list.set_pagesize(static_cast<uint32_t>(value));
                    } catch (const std::exception &) {
                        return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST, "invalid pageSize");
                    }
                }
                return litebus::Async(snapActor->GetAID(), &SnapManagerActor::ListReusableSnapshots, list)
                    .Then([](const ::messages::ListReusableSnapshotsResponse &response)
                              -> litebus::Future<litebus::http::Response> {
                        if (response.code() != common::ERR_NONE) {
                            return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST, response.message());
                        }
                        nlohmann::json body;
                        body["items"] = nlohmann::json::array();
                        for (const auto &item : response.snapshotinfos()) {
                            nlohmann::json snapshotInfo;
                            snapshotInfo["snapshotId"] = item.snapshotid();
                            snapshotInfo["names"] = nlohmann::json::array();
                            for (const auto &name : item.names()) {
                                snapshotInfo["names"].push_back(name);
                            }
                            body["items"].push_back(std::move(snapshotInfo));
                        }
                        body["nextPageToken"] = response.nextpagetoken();
                        return litebus::http::Ok(body.dump(), litebus::http::ResponseBodyType::JSON);
                    });
            }
            if (request.method == "DELETE") {
                const auto snapshot = request.url.query.find("snapshot_id");
                const auto requestID = request.url.query.find("request_id");
                if (snapshot == request.url.query.end() || snapshot->second.empty()
                    || requestID == request.url.query.end() || requestID->second.empty()) {
                    return HttpResponse(litebus::http::ResponseCode::BAD_REQUEST,
                                        "snapshot_id and request_id are required");
                }
                ::messages::DeleteReusableSnapshotRequest remove;
                remove.set_requestid(requestID->second);
                remove.set_tenantid(tenant->second);
                remove.set_snapshotid(snapshot->second);
                return litebus::Async(snapActor->GetAID(), &SnapManagerActor::DeleteReusableSnapshot, remove)
                    .Then([](const ::messages::DeleteReusableSnapshotResponse &response)
                              -> litebus::Future<litebus::http::Response> {
                        if (response.code() != common::ERR_NONE) {
                            return HttpResponse(litebus::http::ResponseCode::INTERNAL_SERVER_ERROR,
                                                response.message());
                        }
                        return litebus::http::Ok(nlohmann::json::object().dump(),
                                                 litebus::http::ResponseBodyType::JSON);
                    });
            }
            return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
        };
        RegisterHandler("/reusable-snapshots", handler);
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
