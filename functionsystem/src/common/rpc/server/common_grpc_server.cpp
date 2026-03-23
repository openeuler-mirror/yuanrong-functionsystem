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
#include <unistd.h>

#include "common/logs/logging.h"
#include "utils/os_utils.hpp"

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
    ::grpc::ServerBuilder builder;
    (void)builder.SetMaxReceiveMessageSize(config_.grpcMessageMaxSize);
    (void)builder.SetMaxSendMessageSize(config_.grpcMessageMaxSize);
    for (auto &service : services_) {
        (void)builder.RegisterService(service.get());
    }

    // Collect addresses to bind. Support both UDS and TCP simultaneously.
    if (!config_.udsPath.empty()) {
        if (!litebus::os::ExistPath(config_.udsPath)) {
            if (litebus::os::Mkdir(config_.udsPath).IsSome()) {
                YRLOG_ERROR("Failed to create UDS directory path: {}, err:{}", config_.udsPath, strerror(errno));
                serverReady_.SetValue(false);
                return;
            }
        }
        std::string udsAddress = litebus::os::Join(config_.udsPath, "fs.sock");
        // If the UDS file already exists, try to remove it before binding.
        (void)unlink(udsAddress.c_str());
        udsAddress = "unix://" + udsAddress;
        YRLOG_INFO("Grpc Server listening on UDS address: {}", udsAddress);
        builder.AddListeningPort(udsAddress, config_.creds);
    }

    if (!config_.ip.empty() && !config_.listenPort.empty()) {
        std::string tcpAddress = config_.ip + ":" + config_.listenPort;
        YRLOG_INFO("Grpc Server listening on TCP address: {}", tcpAddress);
        builder.AddListeningPort(tcpAddress, config_.creds);
    }
    (void)builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    // Enable keepalive to detect dead connections
    (void)builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, 30000);  // Send ping every 30s
    (void)builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);  // Wait 10s for ping ack
    (void)builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);  // Allow pings without active calls
    server_ = std::move(builder.BuildAndStart());
    if (server_ == nullptr) {
        YRLOG_ERROR("Grpc Server start failed (BuildAndStart returned null).");
        serverReady_.SetValue(false);
        return;
    }
    serverReady_.SetValue(true);
    server_->Wait();
    std::cerr << "Grpc Server exit." << std::endl;
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