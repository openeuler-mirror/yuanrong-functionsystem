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

#include <atomic>

#include "common/logs/logging.h"
#include "common/utils/actor_driver.h"

namespace functionsystem {

ExecStreamService::ExecStreamService()
{
    // Initialize IOEventActor singleton
    IOEventActor::CreateInstance();
    YRLOG_INFO("ExecStreamService created, IOEventActor initialized");
}

ExecStreamService::~ExecStreamService()
{
    CloseAllSessions();
    // Destroy IOEventActor singleton
    IOEventActor::DestroyInstance();
    YRLOG_INFO("ExecStreamService destroyed");
}

GrpcStatus ExecStreamService::ExecStream(
    ServerContext* context,
    ServerReaderWriter<ExecMessage, ExecMessage>* stream)
{
    YRLOG_INFO("ExecStream connection established");

    // Current session
    litebus::AID sessionAid;
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
                if (!sessionAid.Name().empty()) {
                    YRLOG_WARN("New start request received, closing existing session: {}",
                              currentSessionId);
                    litebus::Async(sessionAid, &ExecSessionActor::DoClose);
                    RemoveSession(currentSessionId);
                }

                // Handle start request
                YRLOG_INFO("Calling HandleStartRequest...");
                auto status = HandleStartRequest(request.start_request(), stream,
                                                  sessionAid, currentSessionId);
                if (!status.ok()) {
                    YRLOG_ERROR("HandleStartRequest failed: {}", status.error_message());
                    SendStatusResponse(stream, "", ExecStatusResponse::ERROR,
                                      0, status.error_message());
                    continue;
                }

                YRLOG_INFO("Session created with ID: {}", currentSessionId);
                AddSession(currentSessionId, sessionAid);

                // Send start success response
                YRLOG_INFO("Sending STARTED status response for session: {}", currentSessionId);
                SendStatusResponse(stream, currentSessionId, ExecStatusResponse::STARTED);
                break;
            }

            case ExecMessage::kInputData: {
                YRLOG_DEBUG("Received kInputData, size: {} bytes",
                           request.input_data().data().size());
                if (sessionAid.Name().empty()) {
                    YRLOG_WARN("Received input data but no session exists");
                    continue;
                }

                auto status = HandleInputData(request.input_data(), sessionAid);
                if (!status.ok()) {
                    YRLOG_ERROR("Failed to handle input data: {}", status.error_message());
                }
                break;
            }

            case ExecMessage::kResize: {
                YRLOG_DEBUG("Received kResize, rows: {}, cols: {}",
                           request.resize().rows(), request.resize().cols());
                if (sessionAid.Name().empty()) {
                    YRLOG_WARN("Received resize request but no session exists");
                    continue;
                }

                auto status = HandleResize(request.resize(), sessionAid);
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
    if (!sessionAid.Name().empty()) {
        litebus::Async(sessionAid, &ExecSessionActor::DoClose);
        RemoveSession(currentSessionId);
    }

    return GrpcStatus::OK;
}

GrpcStatus ExecStreamService::HandleStartRequest(
    const ExecStartRequest& request,
    ServerReaderWriter<ExecMessage, ExecMessage>* stream,
    litebus::AID& outSessionAid,
    std::string& outSessionId)
{
    YRLOG_INFO("HandleStartRequest: container_id={}, tty={}, rows={}, cols={}",
              request.container_id(), request.tty(), request.rows(), request.cols());

    // Validate parameters
    if (request.container_id().empty()) {
        YRLOG_ERROR("container_id is empty");
        return GrpcStatus(::grpc::StatusCode::INVALID_ARGUMENT, "container_id is required");
    }

    // Create stream writer callback (captures stream pointer)
    // Note: This is called from Actor context, so it's serial
    auto writer = [stream, sessionIdPtr = &outSessionId]
                  (const std::string& data, int exitCode) {
        ExecMessage response;
        response.set_session_id(*sessionIdPtr);

        if (exitCode >= 0) {
            // Exit message
            YRLOG_INFO("Sending exit message, sessionId: {}, exitCode: {}", *sessionIdPtr, exitCode);
            auto* status = response.mutable_status();
            status->set_status(ExecStatusResponse::EXITED);
            status->set_exit_code(exitCode);
        } else if (!data.empty()) {
            // Normal output data
            YRLOG_DEBUG("Sending output data, sessionId: {}, size: {}", *sessionIdPtr, data.size());
            auto* output = response.mutable_output_data();
            output->set_data(data);
            output->set_stream_type(ExecOutputData::STDOUT);
        }

        stream->Write(response);
    };

    // Create ExecSessionActor
    YRLOG_INFO("Creating ExecSessionActor...");
    ExecSessionActor::CreateParams params;
    params.writer = writer;

    auto actor = ExecSessionActor::Create(params);
    if (!actor) {
        YRLOG_ERROR("Failed to create ExecSessionActor");
        return GrpcStatus(::grpc::StatusCode::INTERNAL, "Failed to create session actor");
    }
    YRLOG_INFO("ExecSessionActor created successfully");

    // Spawn the actor to start it
    YRLOG_INFO("Spawning ExecSessionActor...");
    litebus::Spawn(actor);
    YRLOG_INFO("ExecSessionActor spawned successfully");

    // Send Start message to actor using Async (call DoStart directly)
    // Prepare command from request
    std::vector<std::string> command(request.command().begin(), request.command().end());

    // Prepare environment variables from request
    auto env = std::map<std::string, std::string>(request.env().begin(), request.env().end());

    // Ensure TERM is set for TTY mode
    if (request.tty() && env.find("TERM") == env.end()) {
        env["TERM"] = "xterm";
    }

    litebus::Async(actor->GetAID(), &ExecSessionActor::DoStart,
                   request.container_id(), command, env,
                   request.tty(), request.rows(), request.cols());

    outSessionAid = actor->GetAID();

    return GrpcStatus::OK;
}

GrpcStatus ExecStreamService::HandleInputData(
    const ExecInputData& input,
    const litebus::AID& sessionAid)
{
    // Send Input message to actor using Async (call DoInput directly)
    litebus::Async(sessionAid, &ExecSessionActor::DoInput, input.data());
    return GrpcStatus::OK;
}

GrpcStatus ExecStreamService::HandleResize(
    const ExecResizeRequest& resize,
    const litebus::AID& sessionAid)
{
    // Send Resize message to actor using Async (call DoResize directly)
    litebus::Async(sessionAid, &ExecSessionActor::DoResize, resize.rows(), resize.cols());
    return GrpcStatus::OK;
}

void ExecStreamService::SendStatusResponse(
    ServerReaderWriter<ExecMessage, ExecMessage>* stream,
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

    stream->Write(response);
}

void ExecStreamService::AddSession(const std::string& sessionId, const litebus::AID& sessionAid)
{
    std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
    sessions_[sessionId] = sessionAid;
    YRLOG_DEBUG("Session added, sessionId: {}, aid: {}, total: {}",
               sessionId, sessionAid.Name(), sessions_.size());
}

void ExecStreamService::RemoveSession(const std::string& sessionId)
{
    std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
    sessions_.erase(sessionId);
    YRLOG_DEBUG("Session removed, sessionId: {}, total: {}", sessionId, sessions_.size());
}

size_t ExecStreamService::GetActiveSessionCount() const
{
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    return sessions_.size();
}

void ExecStreamService::CloseAllSessions()
{
    YRLOG_INFO("Closing all sessions");

    std::vector<litebus::AID> sessionsToClose;
    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        for (auto& pair : sessions_) {
            sessionsToClose.push_back(pair.second);
        }
        sessions_.clear();
    }

    for (auto& aid : sessionsToClose) {
        litebus::Async(aid, &ExecSessionActor::DoClose);
    }

    YRLOG_INFO("All sessions closed");
}

}  // namespace functionsystem
