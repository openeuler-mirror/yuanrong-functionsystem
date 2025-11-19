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

#ifndef COMMON_RPC_STREAM_POSIX_CONTROL_SERVER_H
#define COMMON_RPC_STREAM_POSIX_CONTROL_SERVER_H
#include <async/future.hpp>

#include "common/proto/pb/posix/runtime_rpc.grpc.pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/rpc/stream/posix_reactor.h"

namespace functionsystem::grpc {
using PosixRuntimeControlHandler = std::function<litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>>(
    const std::shared_ptr<runtime_rpc::StreamingMessage> &)>;
using PosixRuntimeControlHandlers =
    std::unordered_map<runtime_rpc::StreamingMessage::BodyCase, PosixRuntimeControlHandler>;

class ControlServer final : public runtime_rpc::RuntimeRPC::CallbackService {
public:
    ControlServer() = default;
    ~ControlServer() override
    {
        Finish();
        context_ = nullptr;
    }
    ::grpc::ServerBidiReactor<runtime_rpc::StreamingMessage, runtime_rpc::StreamingMessage> *MessageStream(
        ::grpc::CallbackServerContext *context) override;

    void Receiver(const std::shared_ptr<runtime_rpc::StreamingMessage>& recv);
    litebus::Future<runtime_rpc::StreamingMessage> Send(const std::shared_ptr<runtime_rpc::StreamingMessage> &request);
    void Finish() const;

    inline static void RegisterPosixHandler(runtime_rpc::StreamingMessage::BodyCase type,
                                            const PosixRuntimeControlHandler &func)
    {
        handlers_[type] = func;
    }

    void ServerClosedCallback();

    void RegisterUserCallback(const std::function<void()> &userCallback);

    litebus::Future<bool> IsConnected();

    // only for test
    void TryFinish()
    {
        reactor_->TryFinish();
    }

private:
    using ServerReactor =
        PosixReactor<ReactorType::SERVER, runtime_rpc::StreamingMessage, runtime_rpc::StreamingMessage>;
    ::grpc::CallbackServerContext *context_ = nullptr;
    std::shared_ptr<ServerReactor> reactor_;
    std::mutex mut_;
    std::unordered_map<std::string, litebus::Promise<runtime_rpc::StreamingMessage>> promises_;
    inline static PosixRuntimeControlHandlers handlers_;
    std::shared_ptr<litebus::Promise<bool>> isConnected_;
    std::function<void()> userCallback_;
};

class RegisterRuntimeControlHandler {
public:
    RegisterRuntimeControlHandler(runtime_rpc::StreamingMessage::BodyCase type,
                                  const PosixRuntimeControlHandler &func) noexcept
    {
        ControlServer::RegisterPosixHandler(type, func);
    }
    ~RegisterRuntimeControlHandler() = default;
};

#define REGISTER_RUNTIME_CONTROL_POSIX_HANDLER(type, func) \
    static functionsystem::grpc::RegisterRuntimeControlHandler(REGISTER_VAR)(type, func)
}  // namespace functionsystem::grpc

#endif  // COMMON_RPC_STREAM_POSIX_CONTROL_SERVER_H

