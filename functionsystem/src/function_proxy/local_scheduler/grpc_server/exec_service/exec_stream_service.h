/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_STREAM_SERVICE_H
#define FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_STREAM_SERVICE_H

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>
#include "common/proto/pb/posix/exec_service.grpc.pb.h"

#include "actor/actor.hpp"
#include "actor/aid.hpp"
#include "function_proxy/common/exec_session/exec_session_actor.h"
#include "function_proxy/common/exec_session/io_event_actor.h"

namespace functionsystem {

using exec_service::ExecService;
using exec_service::ExecMessage;
using exec_service::ExecStartRequest;
using exec_service::ExecInputData;
using exec_service::ExecResizeRequest;
using exec_service::ExecOutputData;
using exec_service::ExecStatusResponse;
using ::grpc::ServerContext;
using ::grpc::ServerReaderWriter;

// Type alias for gRPC Status to avoid conflict with functionsystem::Status
using GrpcStatus = ::grpc::Status;

/**
 * ExecStreamService provides gRPC bidirectional streaming service for container interaction.
 *
 * Responsibilities:
 * 1. Handle gRPC bidirectional stream Read/Write
 * 2. Manage multiple ExecSessionActor instances
 * 3. Route different message types to appropriate actors
 *
 * Refactored to use Actor model - delegates session management to ExecSessionActor
 */
class ExecStreamService : public ExecService::Service {
public:
    ExecStreamService();
    ~ExecStreamService() override;

    /**
     * ExecStream RPC implementation
     * Establish bidirectional stream connection to container
     */
    GrpcStatus ExecStream(
        ServerContext* context,
        ServerReaderWriter<ExecMessage, ExecMessage>* stream) override;

    /**
     * Get current active session count
     */
    size_t GetActiveSessionCount() const;

    /**
     * Force close all sessions (for service shutdown)
     */
    void CloseAllSessions();

private:
    /**
     * Handle start request
     */
    GrpcStatus HandleStartRequest(
        const ExecStartRequest& request,
        ServerReaderWriter<ExecMessage, ExecMessage>* stream,
        litebus::AID& outSessionAid,
        std::string& outSessionId);

    /**
     * Handle input data
     */
    GrpcStatus HandleInputData(
        const ExecInputData& input,
        const litebus::AID& sessionAid);

    /**
     * Handle window resize
     */
    GrpcStatus HandleResize(
        const ExecResizeRequest& resize,
        const litebus::AID& sessionAid);

    /**
     * Send status response
     */
    void SendStatusResponse(
        ServerReaderWriter<ExecMessage, ExecMessage>* stream,
        const std::string& sessionId,
        ExecStatusResponse::Status status,
        int exitCode = 0,
        const std::string& errorMessage = "");

    /**
     * Add session to manager
     */
    void AddSession(const std::string& sessionId, const litebus::AID& sessionAid);

    /**
     * Remove session
     */
    void RemoveSession(const std::string& sessionId);

private:
    // Session management (using read-write lock for concurrent access)
    // Now stores Actor IDs instead of shared_ptr<ExecSession>
    mutable std::shared_mutex sessionsMutex_;
    std::unordered_map<std::string, litebus::AID> sessions_;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_STREAM_SERVICE_H
