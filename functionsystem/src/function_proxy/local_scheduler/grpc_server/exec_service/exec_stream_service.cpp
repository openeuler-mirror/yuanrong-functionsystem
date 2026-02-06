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

#include "exec_stream_service.h"

#include "common/logs/logging.h"

namespace functionsystem {

ExecStreamService::ExecStreamService()
{
    YRLOG_INFO("ExecStreamService created");
}

ExecStreamService::~ExecStreamService()
{
    CloseAllSessions();
    YRLOG_INFO("ExecStreamService destroyed");
}

GrpcStatus ExecStreamService::ExecStream(
    ServerContext* context,
    ServerReaderWriter<ExecMessage, ExecMessage>* stream)
{
    YRLOG_INFO("ExecStream connection established");

    // Create StreamWriter for thread-safe writing
    auto writer = std::make_shared<StreamWriter>(stream);
    YRLOG_INFO("StreamWriter created for ExecStream");

    // Current session
    std::shared_ptr<ExecSession> session;
    std::string currentSessionId;

    // Main loop: read and process client messages
    ExecMessage request;
    int messageCount = 0;
    while (stream->Read(&request)) {
        messageCount++;
        YRLOG_INFO("Received message #{}, payload_case: {}, session_id: {}",
                   messageCount,
                   static_cast<int>(request.payload_case()),
                   request.session_id());

        switch (request.payload_case()) {
            case ExecMessage::kStartRequest: {
                YRLOG_INFO("Handling kStartRequest, container_id: {}",
                          request.start_request().container_id());
                // If there's already a session, close it first
                if (session) {
                    YRLOG_WARN("New start request received, closing existing session: {}",
                              currentSessionId);
                    session->Close();
                    RemoveSession(currentSessionId);
                }

                // Handle start request
                YRLOG_INFO("Calling HandleStartRequest...");
                auto status = HandleStartRequest(request.start_request(), writer, session);
                if (!status.ok()) {
                    YRLOG_ERROR("HandleStartRequest failed: {}", status.error_message());
                    SendStatusResponse(writer, "", ExecStatusResponse::ERROR,
                                      0, status.error_message());
                    continue;
                }

                currentSessionId = session->GetSessionId();
                YRLOG_INFO("Session created with ID: {}", currentSessionId);
                AddSession(currentSessionId, session);

                // Send start success response
                YRLOG_INFO("Sending STARTED status response for session: {}", currentSessionId);
                SendStatusResponse(writer, currentSessionId, ExecStatusResponse::STARTED);
                break;
            }

            case ExecMessage::kInputData: {
                YRLOG_DEBUG("Received kInputData, size: {} bytes",
                           request.input_data().data().size());
                if (!session) {
                    YRLOG_WARN("Received input data but no session exists");
                    continue;
                }

                auto status = HandleInputData(request.input_data(), session);
                if (!status.ok()) {
                    YRLOG_ERROR("Failed to handle input data: {}", status.error_message());
                }
                break;
            }

            case ExecMessage::kResize: {
                YRLOG_DEBUG("Received kResize, rows: {}, cols: {}",
                           request.resize().rows(), request.resize().cols());
                if (!session) {
                    YRLOG_WARN("Received resize request but no session exists");
                    continue;
                }

                auto status = HandleResize(request.resize(), session);
                if (!status.ok()) {
                    YRLOG_WARN("Failed to handle resize: {}", status.error_message());
                }
                break;
            }

            default: {
                YRLOG_WARN("Unknown or unexpected message type: {}",
                          static_cast<int>(request.payload_case()));
                break;
            }
        }
    }

    YRLOG_INFO("ExecStream connection closed, total messages received: {}", messageCount);

    // Cleanup resources
    if (session) {
        session->Close();
        RemoveSession(currentSessionId);
    }
    writer->Stop();

    return GrpcStatus::OK;
}

GrpcStatus ExecStreamService::HandleStartRequest(
    const ExecStartRequest& request,
    const std::shared_ptr<StreamWriter>& writer,
    std::shared_ptr<ExecSession>& outSession)
{
    YRLOG_INFO("HandleStartRequest: container_id={}, tty={}, rows={}, cols={}",
              request.container_id(), request.tty(), request.rows(), request.cols());

    // Validate parameters
    if (request.container_id().empty()) {
        YRLOG_ERROR("container_id is empty");
        return GrpcStatus(::grpc::StatusCode::INVALID_ARGUMENT, "container_id is required");
    }

    // Build creation parameters
    ExecSession::CreateParams params;
    params.containerId = request.container_id();
    params.tty = request.tty();
    params.rows = request.rows() > 0 ? request.rows() : 24;
    params.cols = request.cols() > 0 ? request.cols() : 80;

    // Command
    for (const auto& cmd : request.command()) {
        params.command.push_back(cmd);
    }
    if (params.command.empty()) {
        params.command = {"/bin/sh"};
    }
    YRLOG_INFO("Command to execute: {}", params.command[0]);

    // Environment variables
    for (const auto& env : request.env()) {
        params.env[env.first] = env.second;
    }

    // Create session
    YRLOG_INFO("Creating ExecSession...");
    auto session = ExecSession::Create(params);
    if (!session) {
        YRLOG_ERROR("Failed to create ExecSession");
        return GrpcStatus(::grpc::StatusCode::INTERNAL, "Failed to create session");
    }
    YRLOG_INFO("ExecSession created successfully");

    // Register output callback
    YRLOG_INFO("Registering output callback...");
    session->OnOutput([writer, sessionId = session->GetSessionId()]
                      (const std::string& data, bool isStderr) {
        YRLOG_DEBUG("Output callback triggered: sessionId={}, size={}, isStderr={}",
                   sessionId, data.size(), isStderr);
        ExecMessage response;
        response.set_session_id(sessionId);

        auto* output = response.mutable_output_data();
        output->set_data(data);
        output->set_stream_type(isStderr ?
            ExecOutputData::STDERR : ExecOutputData::STDOUT);

        YRLOG_DEBUG("Enqueuing output message, size: {}", data.size());
        writer->Enqueue(std::move(response));
        YRLOG_DEBUG("Output message enqueued");
    });
    YRLOG_INFO("Output callback registered");

    // Register exit callback
    session->OnExit([writer, sessionId = session->GetSessionId()](int exitCode) {
        YRLOG_INFO("Session exited, sessionId: {}, exitCode: {}", sessionId, exitCode);

        ExecMessage response;
        response.set_session_id(sessionId);

        auto* status = response.mutable_status();
        status->set_status(ExecStatusResponse::EXITED);
        status->set_exit_code(exitCode);

        writer->Enqueue(std::move(response));
    });

    // Start session
    YRLOG_INFO("Starting ExecSession...");
    auto status = session->Start();
    if (!status.IsOk()) {
        YRLOG_ERROR("Failed to start ExecSession: {}", status.GetMessage());
        return GrpcStatus(::grpc::StatusCode::INTERNAL,
                           "Failed to start session: " + status.GetMessage());
    }
    YRLOG_INFO("ExecSession started successfully");

    outSession = session;
    return GrpcStatus::OK;
}

GrpcStatus ExecStreamService::HandleInputData(
    const ExecInputData& input,
    const std::shared_ptr<ExecSession>& session)
{
    if (!session->IsRunning()) {
        return GrpcStatus(::grpc::StatusCode::FAILED_PRECONDITION, "Session is not running");
    }

    auto status = session->WriteInput(input.data());
    if (!status.IsOk()) {
        return GrpcStatus(::grpc::StatusCode::INTERNAL, status.GetMessage());
    }

    return GrpcStatus::OK;
}

GrpcStatus ExecStreamService::HandleResize(
    const ExecResizeRequest& resize,
    const std::shared_ptr<ExecSession>& session)
{
    if (!session->IsRunning()) {
        return GrpcStatus(::grpc::StatusCode::FAILED_PRECONDITION, "Session is not running");
    }

    auto status = session->Resize(resize.rows(), resize.cols());
    if (!status.IsOk()) {
        return GrpcStatus(::grpc::StatusCode::INTERNAL, status.GetMessage());
    }

    return GrpcStatus::OK;
}

void ExecStreamService::SendStatusResponse(
    const std::shared_ptr<StreamWriter>& writer,
    const std::string& sessionId,
    ExecStatusResponse::Status status,
    int exitCode,
    const std::string& errorMessage)
{
    ExecMessage response;
    response.set_session_id(sessionId);

    auto* statusResp = response.mutable_status();
    statusResp->set_status(status);
    statusResp->set_exit_code(exitCode);
    if (!errorMessage.empty()) {
        statusResp->set_error_message(errorMessage);
    }

    writer->Enqueue(std::move(response));
}

void ExecStreamService::AddSession(const std::string& sessionId,
                                   std::shared_ptr<ExecSession> session)
{
    std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
    sessions_[sessionId] = std::move(session);
    YRLOG_DEBUG("Session added, sessionId: {}, total: {}", sessionId, sessions_.size());
}

void ExecStreamService::RemoveSession(const std::string& sessionId)
{
    std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
    sessions_.erase(sessionId);
    YRLOG_DEBUG("Session removed, sessionId: {}, total: {}", sessionId, sessions_.size());
}

std::shared_ptr<ExecSession> ExecStreamService::GetSession(const std::string& sessionId) const
{
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    auto it = sessions_.find(sessionId);
    return (it != sessions_.end()) ? it->second : nullptr;
}

size_t ExecStreamService::GetActiveSessionCount() const
{
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    return sessions_.size();
}

void ExecStreamService::CloseAllSessions()
{
    YRLOG_INFO("Closing all sessions");

    std::vector<std::shared_ptr<ExecSession>> sessionsToClose;
    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        for (auto& pair : sessions_) {
            sessionsToClose.push_back(pair.second);
        }
        sessions_.clear();
    }

    for (auto& session : sessionsToClose) {
        session->Close();
    }

    YRLOG_INFO("All sessions closed");
}

}  // namespace functionsystem
