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

#ifndef COMMON_GRPC_REACTOR_H
#define COMMON_GRPC_REACTOR_H
#include <grpcpp/impl/codegen/client_callback.h>
#include <grpcpp/impl/codegen/server_callback.h>

#include <async/future.hpp>
#include <queue>

#include "common/logs/logging.h"
namespace functionsystem::grpc {

#define REGISTER_VARNAME(base, line) VARNAME_CONCAT(base, line)
#define VARNAME_CONCAT(base, line) base##line
#define REGISTER_VAR REGISTER_VARNAME(Register, __LINE__)

const std::string LAST_WRITE = "LAST-MESSAGE";

enum ReactorType : int { CLIENT, SERVER };
template <ReactorType Type, class Send, class Receive>
class PosixReactor : public std::conditional<Type == ReactorType::CLIENT, ::grpc::ClientBidiReactor<Send, Receive>,
                                             ::grpc::ServerBidiReactor<Send, Receive>>::type {
public:
    PosixReactor() = default;

    ~PosixReactor() override
    {
        if (!IsDone()) {
            std::cerr << "abnormal ~PosixReactor " <<  id_ << std::endl;
            Wait();
        }
    }

    void RegisterReceiver(const std::function<void(std::shared_ptr<Receive>)> &receiver)
    {
        receiver_ = receiver;
    }

    void RegisterClosedCallback(const std::function<void()> &closedCb)
    {
        notifyClosed_ = closedCb;
    }

    litebus::Future<bool> Write(const std::shared_ptr<Send> &msg, bool debug)
    {
        YRLOG_DEBUG_IF(debug, "reactor-{} stream write msg, type {} messageID {}", id_,
                       fmt::underlying(msg->body_case()), msg->messageid());
        auto writePromise = std::make_shared<litebus::Promise<bool>>();
        auto waitToSend = std::make_pair(msg, writePromise);
        {
            std::lock_guard<std::mutex> lock{ mut_ };
            readyToWrite_.push(waitToSend);
        }
        auto expected = false;
        if (writing_.compare_exchange_strong(expected, true)) {
            NextWrite();
        }
        YRLOG_DEBUG_IF(debug, "reactor-{} stream write msg finished, type {} messageID {}", id_,
                       fmt::underlying(msg->body_case()), msg->messageid());
        return waitToSend.second->GetFuture();
    }

    void OnWriteDone(bool ok) override
    {
        std::pair<std::shared_ptr<Send>, std::shared_ptr<litebus::Promise<bool>>> finished;
        {
            std::lock_guard<std::mutex> lock{ mut_ };
            finished = readyToWrite_.front();
            readyToWrite_.pop();
        }
        auto msgID = finished.first->messageid();
        finished.second->SetValue(ok);
        if (!ok) {
            if constexpr (std::is_same<bool, std::conditional_t<Type == ReactorType::CLIENT, bool, void>>::value) {
                YRLOG_DEBUG("client-{} write {} not ok", id_, msgID);
                this->RemoveHold();
            }
            return;
        }
        NextWrite();
    }

    // for client side
    void OnWritesDoneDone(bool)
    {
        YRLOG_DEBUG("client-{} OnWritesDoneDone hold", id_);
    }

    void Read()
    {
        recv_ = std::make_shared<Receive>();
        this->StartRead(recv_.get());
    }
    void OnReadDone(bool ok) override
    {
        if (!ok) {
            if constexpr (std::is_same<bool, std::conditional_t<Type == ReactorType::SERVER, bool, void>>::value) {
                YRLOG_INFO("server-{} read failed", id_);
                TryFinish();
            } else {
                YRLOG_DEBUG("remove client-{} read hold", id_);
                this->RemoveHold();
                auto lastSend = std::make_shared<Send>();
                lastSend->set_messageid("LAST_WRITE");
                (void)this->Write(lastSend, true);
            }
            return;
        }
        receiver_(std::move(recv_));
        recv_ = nullptr;
        Read();
    }
    // for client side
    void OnDone(const ::grpc::Status &s)
    {
        YRLOG_INFO("client - {} OnDone, status {}  message {}", id_, fmt::underlying(s.error_code()),
                   s.error_message());
        auto donePromise = donePromise_;  // error: promise is freed when run callback
        if (donePromise->GetFuture().IsOK()) {
            return;
        }
        if (notifyClosed_ != nullptr && s.error_code() != ::grpc::StatusCode::INVALID_ARGUMENT) {
            notifyClosed_();
        }
        donePromise->SetValue(s);
    }
    // for server side
    void OnDone()
    {
        YRLOG_INFO("server-{} OnDone", id_);
        // error: double free promise.data
        auto donePromise = donePromise_;
        if (notifyClosed_ != nullptr) {
            notifyClosed_();
        }
        donePromise->SetValue(::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "client disconnected"));
    }

    const ::grpc::Status &GetStatus()
    {
        return donePromise_->GetFuture().Get();
    }

    // to check reactor work or not
    bool IsDone() const
    {
        return donePromise_->GetFuture().IsOK();
    }

    void Wait()
    {
        donePromise_->GetFuture().Wait();
    }

    void SetID(const std::string &id)
    {
        id_ = id;
    }

    // for client side
    void TryStop(::grpc::ClientContext &context)
    {
        if constexpr (std::is_same<bool, std::conditional_t<Type == ReactorType::SERVER, bool, void>>::value) {
            return;
        }
        if (IsDone()) {
            return;
        }
        YRLOG_DEBUG("TryStop client-{}", id_);
        context.TryCancel();
        Wait();
    }

    void TryFinish()
    {
        if (!isFinished_) {
            isFinished_ = true;
            this->Finish(::grpc::Status::OK);
        }
    }

private:
    void NextWrite()
    {
        std::pair<std::shared_ptr<Send>, std::shared_ptr<litebus::Promise<bool>>> msgPair;
        {
            std::lock_guard<std::mutex> lock{ mut_ };
            if (readyToWrite_.empty()) {
                writing_ = false;
                return;
            }
            msgPair = readyToWrite_.front();
            if (IsDone() || isFinished_) {
                YRLOG_WARN("reactor-{} maybe closed. {} unable to send", id_, msgPair.first->messageid());
                readyToWrite_.pop();
            }
        }
        if (IsDone() || isFinished_) {
            writing_ = false;
            msgPair.second->SetValue(false);
            return;
        }
        this->StartWrite(msgPair.first.get());
    }
    std::string id_;
    std::shared_ptr<Receive> recv_{ nullptr };
    // receiver callback should be Non-block
    std::function<void(const std::shared_ptr<Receive> &)> receiver_;
    std::function<void()> notifyClosed_;
    std::mutex mut_;
    std::queue<std::pair<std::shared_ptr<Send>, std::shared_ptr<litebus::Promise<bool>>>> readyToWrite_;
    std::atomic<bool> writing_{ false };
    bool isFinished_{ false };

    std::shared_ptr<litebus::Promise<::grpc::Status>> donePromise_ =
        std::make_shared<litebus::Promise<::grpc::Status>>();
};
}  // namespace functionsystem::grpc
#endif  // COMMON_GRPC_REACTOR_H
