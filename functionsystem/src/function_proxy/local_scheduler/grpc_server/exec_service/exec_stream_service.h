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

#include "function_proxy/common/exec_session/exec_session.h"
#include "function_proxy/common/exec_session/stream_writer.h"

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
 * 2. Manage multiple ExecSession instances
 * 3. Route different message types to appropriate handlers
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
        const std::shared_ptr<StreamWriter>& writer,
        std::shared_ptr<ExecSession>& outSession);

    /**
     * Handle input data
     */
    GrpcStatus HandleInputData(
        const ExecInputData& input,
        const std::shared_ptr<ExecSession>& session);

    /**
     * Handle window resize
     */
    GrpcStatus HandleResize(
        const ExecResizeRequest& resize,
        const std::shared_ptr<ExecSession>& session);

    /**
     * Send status response
     */
    void SendStatusResponse(
        const std::shared_ptr<StreamWriter>& writer,
        const std::string& sessionId,
        ExecStatusResponse::Status status,
        int exitCode = 0,
        const std::string& errorMessage = "");

    /**
     * Add session to manager
     */
    void AddSession(const std::string& sessionId, std::shared_ptr<ExecSession> session);

    /**
     * Remove session
     */
    void RemoveSession(const std::string& sessionId);

    /**
     * Get session
     */
    std::shared_ptr<ExecSession> GetSession(const std::string& sessionId) const;

private:
    // Session management (using read-write lock for concurrent access)
    mutable std::shared_mutex sessionsMutex_;
    std::unordered_map<std::string, std::shared_ptr<ExecSession>> sessions_;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_STREAM_SERVICE_H
