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

#include <grpcpp/grpcpp.h>

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "actor/actor.hpp"
#include "actor/aid.hpp"
#include "common/proto/pb/posix/exec_service.grpc.pb.h"
#include "function_proxy/common/exec_session/exec_session_actor.h"
#include "function_proxy/common/exec_session/io_event_actor.h"

#include "local_scheduler/instance_control/instance_ctrl_actor.h"

namespace functionsystem {

using exec_service::ExecInputData;
using exec_service::ExecMessage;
using exec_service::ExecOutputData;
using exec_service::ExecResizeRequest;
using exec_service::ExecService;
using exec_service::ExecStartRequest;
using exec_service::ExecStatusResponse;
using ::grpc::ServerContext;
using ::grpc::ServerReaderWriter;

// Type alias for gRPC Status to avoid conflict with functionsystem::Status
using GrpcStatus = ::grpc::Status;

/**
 * StreamContext holds per-stream state including write mutex for thread safety
 */
struct StreamContext {
    ServerReaderWriter<ExecMessage, ExecMessage>* stream;
    std::mutex writeMutex;
    std::shared_ptr<std::atomic<bool>> valid;
    litebus::AID sessionAid;  // Session Actor ID to terminate on exit
    std::string instanceID;   // Instance ID for session counting
};

// Shared pointer type for StreamContext to ensure proper lifetime management
using StreamContextPtr = std::shared_ptr<StreamContext>;

/**
 * ExecStreamService provides gRPC bidirectional streaming service for container interaction.
 *
 * Responsibilities:
 * 1. Handle gRPC bidirectional stream Read/Write
 * 2. Manage multiple ExecSessionActor instances
 * 3. Route different message types to appropriate actors
 * 4. Notify InstanceCtrlActor to track per-instance session counts
 *
 * Refactored to use Actor model - delegates session management to ExecSessionActor
 */
class ExecStreamService : public ExecService::Service {
public:
    explicit ExecStreamService(const litebus::AID &instanceCtrlAid);
    ~ExecStreamService() override;

    /**
     * ExecStream RPC implementation
     * Establish bidirectional stream connection to container
     */
    GrpcStatus ExecStream(ServerContext *context, ServerReaderWriter<ExecMessage, ExecMessage> *stream) override;

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
    GrpcStatus HandleStartRequest(const std::string &clientSessionId, const ExecStartRequest &request,
                                  StreamContextPtr streamCtx, litebus::AID &outSessionAid,
                                  std::string &outSessionId);

    /**
     * Handle input data
     */
    GrpcStatus HandleInputData(const ExecInputData &input, const litebus::AID &sessionAid);

    /**
     * Handle window resize
     */
    GrpcStatus HandleResize(const ExecResizeRequest &resize, const litebus::AID &sessionAid);

    /**
     * Send status response
     */
    void SendStatusResponse(ServerReaderWriter<ExecMessage, ExecMessage> *stream, const std::string &sessionId,
                            ExecStatusResponse::Status status, int exitCode = 0, const std::string &errorMessage = "");

    /**
     * Add session to manager
     */
    void AddSession(const std::string &sessionId, const litebus::AID &sessionAid);

    /**
     * Remove session
     */
    void RemoveSession(const std::string &sessionId);

    /**
     * Thread-safe write to stream, handles actor termination on exit
     */
    void WriteToStream(StreamContextPtr streamCtx, const std::string &sessionId,
                       const std::string &data, int exitCode);

private:
    // Session management (using read-write lock for concurrent access)
    // Now stores Actor IDs instead of shared_ptr<ExecSession>
    mutable std::shared_mutex sessionsMutex_;
    std::unordered_map<std::string, litebus::AID> sessions_;

    // Callback to InstanceCtrlActor
    litebus::AID instanceCtrlAid_;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_STREAM_SERVICE_H
