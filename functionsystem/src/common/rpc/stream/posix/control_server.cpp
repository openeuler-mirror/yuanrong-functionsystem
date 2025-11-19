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
#include "control_server.h"

#include "common/logs/logging.h"
#include "common/status/status.h"

namespace functionsystem::grpc {
using namespace runtime_rpc;
::grpc::ServerBidiReactor<StreamingMessage, StreamingMessage> *ControlServer::MessageStream(
    ::grpc::CallbackServerContext *context)
{
    YRLOG_INFO("server receive MessageStream");
    isConnected_ = std::make_shared<litebus::Promise<bool>>();
    isConnected_->SetValue(true);
    reactor_ = std::make_shared<ServerReactor>();
    reactor_->RegisterClosedCallback([this]() { ServerClosedCallback(); });
    reactor_->RegisterReceiver(std::bind(&ControlServer::Receiver, this, std::placeholders::_1));
    // In future it should be set by reading from grpc metadata
    reactor_->SetID("MessageStreamServer");
    reactor_->Read();
    context_ = context;
    return reactor_.get();
}

void ControlServer::ServerClosedCallback()
{
    {
        std::unique_lock<std::mutex> lock(mut_);
        for (auto &[msgID, promise] : promises_) {
            [[maybe_unused]] const auto& unused = msgID;
            promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_REQUEST_BETWEEN_RUNTIME_BUS));
        }
        promises_.clear();
    }
    if (userCallback_ != nullptr && isConnected_->GetFuture().IsOK()) {
        // reset isConnected_ value and wait for reconnetion MessageStream
        isConnected_ = std::make_shared<litebus::Promise<bool>>();
        userCallback_();
    }
}

void ControlServer::RegisterUserCallback(const std::function<void()> &userCallback)
{
    userCallback_ = userCallback;
}

litebus::Future<StreamingMessage> ControlServer::Send(const std::shared_ptr<StreamingMessage> &request)
{
    auto sendPromise = litebus::Promise<StreamingMessage>();
    if (reactor_ == nullptr || reactor_->IsDone() || context_ == nullptr) {
        YRLOG_ERROR("client connection is already failed or not connected, unable to send msg");
        sendPromise.SetFailed(static_cast<int32_t>(StatusCode::ERR_DISCONNECT_FRONTEND_BUS));
        return sendPromise.GetFuture();
    }
    {
        std::unique_lock<std::mutex> lk(mut_);
        auto emplaceRes = promises_.emplace(request->messageid(), sendPromise);
        if (!emplaceRes.second) {
            YRLOG_DEBUG("duplicate send request's messageID {}, returning previous future", request->messageid());
            return promises_[request->messageid()].GetFuture();
        }
    }
    return reactor_->Write(request, true)
        .Then([this, sendPromise,
               messageID(request->messageid())](const litebus::Future<bool> &fut) -> litebus::Future<StreamingMessage> {
            auto isSuccess = fut.Get();
            if (!isSuccess) {
                YRLOG_ERROR("posix stream connection has been failed! send {} failed", messageID);
                sendPromise.SetFailed(static_cast<int32_t>(StatusCode::ERR_DISCONNECT_FRONTEND_BUS));
                std::unique_lock<std::mutex> lk(mut_);
                (void)promises_.erase(messageID);
            }
            return sendPromise.GetFuture();
        });
}

void ControlServer::Receiver(const std::shared_ptr<StreamingMessage> &recv)
{
    auto messageID = recv->messageid();
    auto bodyType = recv->body_case();
    bool debug = (bodyType != StreamingMessage::kHeartbeatReq) && (bodyType != StreamingMessage::kHeartbeatRsp);
    YRLOG_DEBUG_IF(debug, "server posix stream msg type, body {} messageID {}", fmt::underlying(bodyType), messageID);
    {
        std::unique_lock<std::mutex> lk(mut_);
        if (auto it(promises_.find(messageID)); it != promises_.end()) {
            it->second.SetValue(*recv);
            (void)promises_.erase(it);
            return;
        }
    }

    if (auto iter(handlers_.find(bodyType)); iter == handlers_.end()) {
        YRLOG_WARN("{} invalid posix stream msg type, messageID {}", fmt::underlying(bodyType), messageID);
        return;
    }
    auto future = handlers_[bodyType](recv);
    (void)future.OnComplete(
        [messageID, reactor(this->reactor_), debug](const litebus::Future<std::shared_ptr<StreamingMessage>> &future) {
            auto resp = future.Get();
            resp->set_messageid(messageID);
            (void)reactor->Write(resp, debug).OnComplete([messageID](const litebus::Future<bool> &fut) {
                auto isSuccess = fut.Get();
                if (!isSuccess) {
                    YRLOG_ERROR("server Write failed, recvMsgID {}", messageID);
                }
            });
        });
}

void ControlServer::Finish() const
{
    if (reactor_ == nullptr || reactor_->IsDone()) {
        return;
    }
    if (context_ != nullptr && !context_->IsCancelled()) {
        context_->TryCancel();
    }
}

litebus::Future<bool> ControlServer::IsConnected()
{
    return isConnected_->GetFuture();
}
}  // namespace functionsystem::grpc