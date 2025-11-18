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

#ifndef FUNCTIONSYSTEM_RPC_CLIENT_GRPC_CLIENT_H
#define FUNCTIONSYSTEM_RPC_CLIENT_GRPC_CLIENT_H

#include <grpcpp/grpcpp.h>

#include <functional>
#include <iostream>
#include <unordered_set>

#include "async/async.hpp"
#include "async/future.hpp"
#include "async/try.hpp"
#include "async/uuid_generator.hpp"
#include "common/common_flags/common_flags.h"
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/certs_utils.h"
#include "common/utils/param_check.h"
#include "common/utils/sensitive_value.h"
#include "common/utils/singleton.h"
#include "grpc_call_obj.h"
#include "litebus.hpp"

namespace functionsystem {
const uint32_t WAIT_FOR_STAGE_CHANGE_TIMEOUT_SEC = 5;
const uint32_t WAIT_FOR_CONNECT_TIMEOUT_SEC = 1;
const uint32_t RECONNECT_INTERVAL = 5;
const uint32_t MAX_READY_COUNT = 3;
const int32_t INITIAL_RECONNECT_BACKOFF_MS = 1000;
const int32_t MAX_RECONNECT_BACKOFF_MS = 6000;
const int32_t MIN_RECONNECT_BACKOFF_MS = 1000;
const int32_t GRPC_LB_FALLBACK_TIMEOUT = 30000;
const int32_t GRPC_KEEPALIVE_TIME_MS = 30000;
const int32_t GRPC_KEEPALIVE_TIMEOUT_MS = 5000;

struct GrpcSslConfig {
    std::shared_ptr<::grpc::ChannelCredentials> sslCredentials = ::grpc::InsecureChannelCredentials();
    std::string targetName = "";
};

GrpcSslConfig GetGrpcSSLConfig(const CommonFlags &flags);

TLSConfig GetGrpcTLSConfig(const CommonFlags &flags);

class GrpcClientActor : public litebus::ActorBase {
public:
    explicit GrpcClientActor(const std::string &aid) : litebus::ActorBase(aid)
    {
    }
    ~GrpcClientActor() override = default;

    Status Call(const std::function<::grpc::Status(::grpc::ClientContext *context)> &grpcCallFunc,
                const ::grpc::string &method, const std::string &addr, const uint32_t &timeoutSeconds);
};

class GrpcCQThread : public Singleton<GrpcCQThread> {
public:
    GrpcCQThread()
    {
        StartThread();
    }

    ~GrpcCQThread() override
    {
        try {
            std::cout << "enter GrpcCQThread destruct.\n";
            shutdown_->store(true);
            cq_->Shutdown();
            (void)exit_->GetFuture().Get();
            std::cout << "GrpcCQThread destructed.\n";
        } catch (std::exception &e) {
            std::cout << "~GrpcCQThread e.what = " << e.what() << std::endl;
        }
    }

    ::grpc::CompletionQueue *GetCQ()
    {
        return shutdown_->load() ? nullptr : cq_.get();
    }

private:
    void StartThread() const
    {
        auto cqThread = [shutdown(shutdown_), cq(cq_), e(exit_)]() {
            std::cout << "start GrpcCQThread.\n";
            void *tag = nullptr;
            bool ok = false;
            while (!shutdown->load() && cq->Next(&tag, &ok)) {
                auto *callbackTag = static_cast<GrpcCQTag *>(tag);
                if (!callbackTag || shutdown->load()) {
                    break;
                }
                callbackTag->OnCompleted(ok);
            }
            e->SetValue(true);
            std::cout << "GrpcCQThread exited.\n";
        };
        auto t = std::thread(cqThread);
        t.detach();
    }

    std::shared_ptr<::grpc::CompletionQueue> cq_ = std::make_shared<::grpc::CompletionQueue>();
    std::shared_ptr<litebus::Promise<bool>> exit_ = std::make_shared<litebus::Promise<bool>>();
    // Whether the client has shutdown.
    std::shared_ptr<std::atomic<bool>> shutdown_ = std::make_shared<std::atomic<bool>>(false);
};

template <class T>
class GrpcClient {
public:
    /**
     * @brief Create a grpc client.
     * @param[in] Grpc server address, like 127.0.0.1:1234.
     * @return Grpc Client
     */
    static std::unique_ptr<GrpcClient<T>> CreateGrpcClient(const std::string &addr, const GrpcSslConfig &sslConfig = {})
    {
        YRLOG_DEBUG("enter CreateGrpcClient");
        ::grpc::ChannelArguments args;
        args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, GRPC_KEEPALIVE_TIME_MS);
        args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, GRPC_KEEPALIVE_TIMEOUT_MS);
        args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, INITIAL_RECONNECT_BACKOFF_MS);
        args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, MIN_RECONNECT_BACKOFF_MS);
        args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, MAX_RECONNECT_BACKOFF_MS);
        args.SetMaxSendMessageSize(std::numeric_limits<int>::max());
        args.SetMaxReceiveMessageSize(std::numeric_limits<int>::max());
        std::string prefix;
        if (IsAddressesValid(addr)) {
            prefix = "ipv4:///";
        }
        if (!sslConfig.targetName.empty()) {
            args.SetSslTargetNameOverride(sslConfig.targetName);
        }
        auto channel = ::grpc::CreateCustomChannel(prefix + addr, sslConfig.sslCredentials, args);
        auto stub = T::NewStub(static_cast<const std::shared_ptr<::grpc::ChannelInterface> &>(channel));
        if (!stub) {
            YRLOG_ERROR("Create grpc client stub failed, addr = {}", addr);
            return nullptr;
        }
        return std::move(std::make_unique<GrpcClient<T>>(addr, std::move(stub), std::move(channel)));
    }

    void CheckChannelAndWaitForReconnect(const std::atomic<bool> &running)
    {
        if (channel_ == nullptr) {
            YRLOG_ERROR("Channel to address ({}) is not initialized, failed to check.", addr_);
            return;
        }
        auto state = channel_->GetState(true);
        YRLOG_WARN("Channel to ({}) reconnection starting state is: ({})", addr_, fmt::underlying(state));
        // Three consecutive times of ready are considered as successful.
        uint32_t readyCount = 0;
        while (state != GRPC_CHANNEL_READY && running && readyCount < MAX_READY_COUNT) {
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_INTERVAL));
            isConnected_.store(false);
            auto tmout = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), stageChangeTimeoutSpec_);
            bool isConnected = channel_->WaitForConnected(tmout);
            state = channel_->GetState(true);
            if (!isConnected) {
                readyCount = 0;
                YRLOG_INFO("Channel to ({}) still disconnected, state: ({})", addr_, fmt::underlying(state));
                continue;
            }
            readyCount++;
        }
        if (state == GRPC_CHANNEL_READY) {
            YRLOG_INFO("Channel to ({}) reconnects successfully.", addr_);
            isConnected_.store(true);
        }
    }

    std::shared_ptr<::grpc::Channel> GetChannel()
    {
        if (channel_ == nullptr) {
            YRLOG_ERROR("Channel to address ({}) is not initialized.", addr_);
            return nullptr;
        }
        return channel_;
    }

    bool IsConnected()
    {
        return isConnected_.load();
    }

    GrpcClient(const std::string &addr, std::unique_ptr<typename T::Stub> stub,
               std::shared_ptr<::grpc::Channel> &&channel)
        : addr_(addr), stub_(std::move(stub)), channel_(channel), isConnected_(true)
    {
        const std::string aid = "grpc_client_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        clientActor_ = std::make_shared<GrpcClientActor>(aid);
        litebus::Spawn(clientActor_);
        YRLOG_DEBUG("Create grpc client Actor : {}, dst addr is {}", std::string(clientActor_->GetAID()), addr_);
        auto tmout = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), connectTimeoutSpec_);
        isConnected_.store(channel_->WaitForConnected(tmout));
    }

    ~GrpcClient()
    {
        litebus::Terminate(clientActor_->GetAID());
        litebus::Await(clientActor_);
    }

    /**
     * @brief Send RPC messages synchronously.
     * @param[in] methodName Method name.
     * @param[in] req Request proto msg.
     * @param[in] rsp Response proto msg.
     * @param[in] stubFunc Grpc func.
     * @param[in] timeoutSeconds Timeout, in seconds. The default value is 0.
     * @return Grpc status of the RPC call.
     */
    template <typename Request, typename Response,
              typename StubFunc = ::grpc::Status (*)(::grpc::ClientContext *, const Request &, Response *)>
    Status Call(const ::grpc::string &method, const Request &req, Response &rsp, StubFunc stubFunc,
                uint32_t timeoutSeconds = 0)
    {
        auto stubPtr = stub_.get();
        std::function<::grpc::Status(::grpc::ClientContext * context)> grpcCallFunc =
            [stubPtr, &req, &rsp, &stubFunc](::grpc::ClientContext *context) -> ::grpc::Status {
            return (stubPtr->*stubFunc)(context, req, &rsp);
        };
        auto status =
            litebus::Async(clientActor_->GetAID(), &GrpcClientActor::Call, grpcCallFunc, method, addr_, timeoutSeconds);
        auto s = status.Get();
        if (s.IsError()) {
            YRLOG_ERROR("Sync GRPC Call Failed : {} ", s.ToString());
        }

        return s;
    }

    /**
     * @brief Send RPC messages synchronously through asynchronous to synchronous way.
     * @param[in] methodName Method name.
     * @param[in] req Request proto msg.
     * @param[in] rsp Response proto msg.
     * @param[in] stubFunc Grpc func.
     * @param[in] timeoutSeconds Timeout, in seconds. The default value is 0.
     * @return Grpc status of the RPC call.
     */
    template <typename Request, typename Response,
              typename StubFunc = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface<Response>> (*)(
                  ::grpc::ClientContext *, const Request &, ::grpc::CompletionQueue *)>
    Status CallAsync(const ::grpc::string &method, const Request &req, Response &rsp, StubFunc stubFunc,
                     uint32_t timeoutSeconds = 0)
    {
        auto cq = GrpcCQThread::GetInstance().GetCQ();
        if (cq == nullptr) {
            YRLOG_ERROR("ASync GRPC Call Failed : CompletionQueue is shutdown.");
            Status s = Status(GRPC_CALL_OBJ_ERROR);
            return s;
        }
        litebus::Promise<Status> donePromise;

        auto doneCB = [&donePromise](const Status &s) {
            YRLOG_DEBUG("grpc call done");
            donePromise.SetValue(s);
        };
        auto obj = new (std::nothrow) GrpcCallObj<typename T::Stub, Request, Response, StubFunc>(
            stub_.get(), cq, method, &req, &rsp, stubFunc, doneCB, timeoutSeconds * 1000);
        if (obj == nullptr) {
            YRLOG_ERROR("ASync GRPC Call Failed : can not allocate GrpcCallObj memory.");
            Status s = Status(GRPC_CALL_OBJ_ERROR);
            return s;
        }

        auto s = donePromise.GetFuture().Get();
        if (s.IsError()) {
            YRLOG_ERROR("ASync GRPC Call Failed : {} ", s.ToString());
        }
        return s;
    }

    /**
     * @brief Send RPC messages Asynchronously.
     * @param[in] methodName Method name.
     * @param[in] req Request proto msg.
     * @param[in] rsp Response proto msg.
     * @param[in] stubFunc Grpc func.
     * @param[in] done callback for notify call status.
     * @param[in] timeoutSeconds Timeout, in seconds. The default value is 0.
     * @return Whether the function call was successful.
     */
    template <typename Request, typename Response,
              typename StubFunc = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface<Response>> (*)(
                  ::grpc::ClientContext *, const Request &, ::grpc::CompletionQueue *)>
    bool CallAsync(const ::grpc::string &method, const Request &req, Response &rsp, StubFunc stubFunc,
                   StatusCallback done, uint32_t timeoutSeconds = 0)
    {
        auto cq = GrpcCQThread::GetInstance().GetCQ();
        if (cq == nullptr) {
            YRLOG_ERROR("ASync GRPC Call Failed : CompletionQueue is shutdown.");
            Status s = Status(GRPC_CALL_OBJ_ERROR);
            done(s);
            return false;
        }
        auto obj = new (std::nothrow) GrpcCallObj<typename T::Stub, Request, Response, StubFunc>(
            stub_.get(), cq, method, &req, &rsp, stubFunc, done, timeoutSeconds * 1000);
        if (obj == nullptr) {
            YRLOG_ERROR("ASync GRPC Call Failed : can not allocate GrpcCallObj memory.");
            Status s = Status(GRPC_CALL_OBJ_ERROR);
            done(s);
            return false;
        }
        return true;
    }

    /**
     * @brief Send RPC messages Asynchronously.
     * @param[in] methodName Method name.
     * @param[in] request Request proto msg.
     * @param[in] response Response proto msg.
     * @param[in] stubFunc Grpc func.
     * @param[in] done callback for notify call status.
     * @param[in] timeoutSeconds Timeout, in seconds. The default value is 0.
     * @return the future of response
     */
    template <typename Request, typename Response,
              typename StubFunc = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface<Response>> (*)(
                  ::grpc::ClientContext *, const Request &, ::grpc::CompletionQueue *)>
    litebus::Future<functionsystem::Status> CallAsyncX(const ::grpc::string &method, const Request &request,
                                                       Response *response, StubFunc stubFunc,
                                                       uint32_t timeoutSeconds = 0)
    {
        if (!isConnected_.load()) {
            YRLOG_ERROR("ASync GRPC Call Failed: client is not connected.");
            return functionsystem::Status(GRPC_UNAVAILABLE, "ASync GRPC Call Failed: client is not connected.");
        }

        auto cq = GrpcCQThread::GetInstance().GetCQ();
        if (cq == nullptr) {
            YRLOG_ERROR("ASync GRPC Call Failed: CompletionQueue is shutdown.");
            return functionsystem::Status(GRPC_UNAVAILABLE, "ASync GRPC Call Failed: CompletionQueue is shutdown.");
        }

        auto donePromise = std::make_shared<litebus::Promise<functionsystem::Status>>();
        auto doneCB = [donePromise](const functionsystem::Status &status) {
            donePromise->SetValue(status);
        };

        auto obj = new (std::nothrow) GrpcCallObj<typename T::Stub, Request, Response, StubFunc>(
            stub_.get(), cq, method, &request, response, stubFunc, doneCB, timeoutSeconds * 1000);
        if (obj == nullptr) {
            YRLOG_ERROR("ASync GRPC Call Failed: can not allocate GrpcCallObj memory.");
            return functionsystem::Status(GRPC_CALL_OBJ_ERROR,
                                          "ASync GRPC Call Failed: can not allocate GrpcCallObj memory.");
        }

        return donePromise->GetFuture();
    }

    /**
     * @brief Send RPC messages Asynchronously.
     * @param[in] methodName Method name.
     * @param[in] req Request proto msg.
     * @param[in] stubFunc Grpc func.
     * @param[in] done callback for notify call status.
     * @param[in] timeoutSeconds Timeout, in seconds. The default value is 0.
     * @return the future of response
     */
    template <typename Request, typename Response,
              typename StubFunc = std::unique_ptr<::grpc::ClientAsyncResponseReaderInterface<Response>> (*)(
                  ::grpc::ClientContext *, const Request &, ::grpc::CompletionQueue *)>
    litebus::Future<litebus::Try<Response>> CallAsync(const ::grpc::string &method, const Request &req, Response *,
                                                      StubFunc stubFunc, uint32_t timeoutSeconds = 0)
    {
        if (!isConnected_.load()) {
            return litebus::Try<Response>(static_cast<int32_t>(GRPC_UNAVAILABLE));
        }
        auto cq = GrpcCQThread::GetInstance().GetCQ();
        if (cq == nullptr) {
            YRLOG_ERROR("ASync GRPC Call Failed : CompletionQueue is shutdown.");
            return litebus::Try<Response>(static_cast<int32_t>(GRPC_UNAVAILABLE));
        }
        auto donePromise = std::make_shared<litebus::Promise<litebus::Try<Response>>>();
        auto rsp = std::make_shared<Response>();
        auto doneCB = [donePromise, rsp](const Status &s) {
            if (s.IsError()) {
                donePromise->SetValue(litebus::Try<Response>(static_cast<int32_t>(s.StatusCode())));
                return;
            }
            YRLOG_DEBUG("grpc call done");
            donePromise->SetValue(litebus::Try<Response>(*rsp));
        };

        auto obj = new (std::nothrow) GrpcCallObj<typename T::Stub, Request, Response, StubFunc>(
            stub_.get(), cq, method, &req, rsp.get(), stubFunc, doneCB, timeoutSeconds * 1000);
        if (obj == nullptr) {
            YRLOG_ERROR("ASync GRPC Call Failed : can not allocate GrpcCallObj memory.");
            return litebus::Future<litebus::Try<Response>>(litebus::Failure(static_cast<int32_t>(GRPC_CALL_OBJ_ERROR)));
        }
        return donePromise->GetFuture();
    }

    /**
     * @brief Get bidirectional stream.
     * @param[in] stubFunc Grpc func.
     * @param[in] context Grpc client context ptr, cannot be freed earlier than function return value
     * (ClientReaderWriter)
     * @return Grpc Client ReaderWriter object.
     */
    template <
        typename Request, typename Response,
        typename StubFunc = std::unique_ptr<::grpc::ClientReaderWriter<Request, Response>> (*)(::grpc::ClientContext *)>
    std::unique_ptr<::grpc::ClientReaderWriter<Request, Response>> CallReadWriteStream(StubFunc stubFunc,
                                                                                       ::grpc::ClientContext *context)
    {
        if (context == nullptr) {
            YRLOG_ERROR("Grpc CallReadWriteStream failed, context parameter is null.");
            return nullptr;
        }

        return (stub_.get()->*stubFunc)(context);
    }

    static void SetConnectTimeout(const gpr_timespec &timespec)
    {
        connectTimeoutSpec_ = timespec;
        stageChangeTimeoutSpec_ = timespec;
    }

private:
    std::string addr_;
    std::unique_ptr<typename T::Stub> stub_;
    std::shared_ptr<::grpc::Channel> channel_;
    std::shared_ptr<GrpcClientActor> clientActor_;
    std::atomic<bool> isConnected_;
    inline static gpr_timespec connectTimeoutSpec_ { WAIT_FOR_CONNECT_TIMEOUT_SEC, 0, GPR_TIMESPAN };
    inline static gpr_timespec stageChangeTimeoutSpec_ { WAIT_FOR_STAGE_CHANGE_TIMEOUT_SEC, 0, GPR_TIMESPAN };
};

}  // namespace functionsystem
#endif
