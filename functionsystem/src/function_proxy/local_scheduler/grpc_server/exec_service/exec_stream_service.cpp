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
#include <mutex>

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

GrpcStatus ExecStreamService::ExecStream(ServerContext *context, ServerReaderWriter<ExecMessage, ExecMessage> *stream)
{
    auto peer = context->peer();
    YRLOG_INFO("ExecStream connection established, peer: {}", peer);

    // Current session
    litebus::AID sessionAid;
    std::string currentSessionId;

    // Stream validity flag (shared with writer callback)
    auto streamValid = std::make_shared<std::atomic<bool>>(true);

    // Create stream context with write mutex for thread safety
    auto streamCtx = std::make_shared<StreamContext>();
    streamCtx->stream = stream;
    streamCtx->valid = streamValid;

    // Main loop: read and process client messages
    ExecMessage request;
    while (stream->Read(&request)) {
        switch (request.payload_case()) {
            case ExecMessage::kStartRequest: {
                // If there's already a session, close it first
                if (!sessionAid.Name().empty()) {
                    YRLOG_INFO("Closing existing session {} before starting new one, peer: {}",
                               currentSessionId, peer);
                    litebus::Async(sessionAid, &ExecSessionActor::DoClose);
                    RemoveSession(currentSessionId);
                }

                // Handle start request
                auto status = HandleStartRequest(request.start_request(), streamCtx, sessionAid, currentSessionId);
                if (!status.ok()) {
                    YRLOG_ERROR("HandleStartRequest failed: {}", status.error_message());
                    SendStatusResponse(stream, "", ExecStatusResponse::ERROR, 0, status.error_message());
                    continue;
                }

                AddSession(currentSessionId, sessionAid);
                SendStatusResponse(stream, currentSessionId, ExecStatusResponse::STARTED);
                YRLOG_INFO("Session {} started, peer: {}", currentSessionId, peer);
                break;
            }

            case ExecMessage::kInputData: {
                if (!sessionAid.Name().empty()) {
                    HandleInputData(request.input_data(), sessionAid);
                }
                break;
            }

            case ExecMessage::kResize: {
                if (!sessionAid.Name().empty()) {
                    HandleResize(request.resize(), sessionAid);
                }
                break;
            }

            default:
                break;
        }
    }

    YRLOG_INFO("ExecStream connection closed, peer: {}", peer);

    // Mark stream as invalid before cleanup
    streamValid->store(false);

    // Cleanup resources
    if (!sessionAid.Name().empty()) {
        YRLOG_INFO("Cleaning up session {}, peer: {}", currentSessionId, peer);
        litebus::Async(sessionAid, &ExecSessionActor::DoClose);
        litebus::Terminate(sessionAid);
        litebus::Await(sessionAid);
        RemoveSession(currentSessionId);
    }

    return GrpcStatus::OK;
}

void ExecStreamService::WriteToStream(StreamContextPtr streamCtx, const std::string &sessionId,
                                      const std::string &data, int exitCode)
{
    if (!streamCtx || !streamCtx->valid || !streamCtx->valid->load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(streamCtx->writeMutex);

    if (!streamCtx->valid->load()) {
        return;
    }

    ExecMessage response;
    response.set_session_id(sessionId);

    if (exitCode >= 0) {
        auto *status = response.mutable_status();
        status->set_status(ExecStatusResponse::EXITED);
        status->set_exit_code(exitCode);
    } else if (!data.empty()) {
        auto *output = response.mutable_output_data();
        output->set_data(data);
        output->set_stream_type(ExecOutputData::STDOUT);
    }

    if (!streamCtx->stream->Write(response)) {
        return;
    }

    if (exitCode >= 0 && !streamCtx->sessionAid.Name().empty()) {
        litebus::Terminate(streamCtx->sessionAid);
        litebus::Await(streamCtx->sessionAid);
        RemoveSession(sessionId);
    }
}

GrpcStatus ExecStreamService::HandleStartRequest(const ExecStartRequest &request,
                                                 StreamContextPtr streamCtx,
                                                 litebus::AID &outSessionAid, std::string &outSessionId)
{
    if (request.container_id().empty()) {
        return GrpcStatus(::grpc::StatusCode::INVALID_ARGUMENT, "container_id is required");
    }

    auto actor = ExecSessionActor::Create({});
    if (!actor) {
        return GrpcStatus(::grpc::StatusCode::INTERNAL, "Failed to create session actor");
    }

    outSessionId = actor->GetSessionId();
    outSessionAid = actor->GetAID();
    streamCtx->sessionAid = outSessionAid;

    actor->SetWriter([this, streamCtx, sessionId = outSessionId](const std::string &data, int exitCode) {
        WriteToStream(streamCtx, sessionId, data, exitCode);
    });

    litebus::Spawn(actor);

    std::vector<std::string> command(request.command().begin(), request.command().end());
    auto env = std::map<std::string, std::string>(request.env().begin(), request.env().end());

    if (request.tty() && env.find("TERM") == env.end()) {
        env["TERM"] = "xterm";
    }

    litebus::Async(actor->GetAID(), &ExecSessionActor::DoStart, request.container_id(), command, env, request.tty(),
                   request.rows(), request.cols());

    return GrpcStatus::OK;
}

GrpcStatus ExecStreamService::HandleInputData(const ExecInputData &input, const litebus::AID &sessionAid)
{
    litebus::Async(sessionAid, &ExecSessionActor::DoInput, input.data());
    return GrpcStatus::OK;
}

GrpcStatus ExecStreamService::HandleResize(const ExecResizeRequest &resize, const litebus::AID &sessionAid)
{
    litebus::Async(sessionAid, &ExecSessionActor::DoResize, resize.rows(), resize.cols());
    return GrpcStatus::OK;
}

void ExecStreamService::SendStatusResponse(ServerReaderWriter<ExecMessage, ExecMessage> *stream,
                                           const std::string &sessionId, ExecStatusResponse::Status status,
                                           int exitCode, const std::string &errorMessage)
{
    ExecMessage response;
    response.set_session_id(sessionId);

    auto *statusResp = response.mutable_status();
    statusResp->set_status(status);
    statusResp->set_exit_code(exitCode);
    if (!errorMessage.empty()) {
        statusResp->set_error_message(errorMessage);
    }

    stream->Write(response);
}

void ExecStreamService::AddSession(const std::string &sessionId, const litebus::AID &sessionAid)
{
    std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
    sessions_[sessionId] = sessionAid;
}

void ExecStreamService::RemoveSession(const std::string &sessionId)
{
    std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
    sessions_.erase(sessionId);
}

size_t ExecStreamService::GetActiveSessionCount() const
{
    std::shared_lock<std::shared_mutex> lock(sessionsMutex_);
    return sessions_.size();
}

void ExecStreamService::CloseAllSessions()
{
    std::vector<litebus::AID> sessionsToClose;
    {
        std::unique_lock<std::shared_mutex> lock(sessionsMutex_);
        for (auto &pair : sessions_) {
            sessionsToClose.push_back(pair.second);
        }
        sessions_.clear();
    }

    for (auto &aid : sessionsToClose) {
        litebus::Async(aid, &ExecSessionActor::DoClose);
    }
}

}  // namespace functionsystem
