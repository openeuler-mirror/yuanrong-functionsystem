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
#include "common_grpc_server.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>

#include "common/logs/logging.h"

namespace functionsystem::grpc {
const uint32_t WAIT_FOR_SERVER_EXIT_SEC = 3;
CommonGrpcServer::~CommonGrpcServer()
{
    if (!serverThread_) {
        return;
    }
    try {
        if (server_) {
            auto tmout =
                gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), { WAIT_FOR_SERVER_EXIT_SEC, 0, GPR_TIMESPAN });
            server_->Shutdown(tmout);
        }
        if (serverThread_->joinable()) {
            serverThread_->join();
        }
        serverThread_ = nullptr;
    } catch (const std::exception &e) {
        std::cerr << "failed in CommonGrpcServer destructor, error: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "failed in CommonGrpcServer CommonGrpcServer destructor." << std::endl;
    }
}

void CommonGrpcServer::Start()
{
    serverThread_ = std::make_unique<std::thread>(std::bind(&CommonGrpcServer::Run, this));
    serverThread_->detach();
}

void CommonGrpcServer::Run()
{
    auto address = config_.ip + ":" + config_.listenPort;
    ::grpc::ServerBuilder builder;
    (void)builder.SetMaxReceiveMessageSize(config_.grpcMessageMaxSize);
    (void)builder.SetMaxSendMessageSize(config_.grpcMessageMaxSize);
    for (auto &service : services_) {
        (void)builder.RegisterService(service.get());
    }
    (void)builder.AddListeningPort(address, config_.creds);
    (void)builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    server_ = std::move(builder.BuildAndStart());
    if (server_ == nullptr) {
        YRLOG_ERROR("Grpc Server({}) start failed.", address);
        serverReady_.SetValue(false);
        return;
    }
    YRLOG_INFO("Grpc Server({}) listening.", address);
    serverReady_.SetValue(true);
    server_->Wait();
    std::cerr << "Grpc Server exit. address: " << address << std::endl;
}

bool CommonGrpcServer::WaitServerReady() const
{
    return serverReady_.GetFuture().Get();
}

void CommonGrpcServer::RegisterService(const std::shared_ptr<::grpc::Service> &service)
{
    if (service != nullptr) {
        services_.emplace_back(service);
    }
}
}  // namespace functionsystem::grpc