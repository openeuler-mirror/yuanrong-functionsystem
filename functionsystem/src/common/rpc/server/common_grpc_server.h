/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#ifndef COMMON_RPC_SERVER_COMMON_GRPC_SERVER_H
#define COMMON_RPC_SERVER_COMMON_GRPC_SERVER_H

#include <grpcpp/server.h>
#include "async/future.hpp"

namespace functionsystem::grpc {
const int DEFAULT_GRPC_MESSAGE_MAX_SIZE = 500 * 1024 * 1024;
struct CommonGrpcServerConfig {
    int grpcMessageMaxSize{ DEFAULT_GRPC_MESSAGE_MAX_SIZE };
    std::string ip;
    std::string listenPort;
    std::shared_ptr<::grpc::ServerCredentials> creds;
};

class CommonGrpcServer {
public:
    explicit CommonGrpcServer(const CommonGrpcServerConfig &serverConfig) : config_(std::move(serverConfig)) {};
    ~CommonGrpcServer();

    void Start();
    bool WaitServerReady() const;
    void RegisterService(const std::shared_ptr<::grpc::Service> &service);

private:
    void Run();

    std::unique_ptr<std::thread> serverThread_{ nullptr };
    std::unique_ptr<::grpc::Server> server_{ nullptr };
    std::vector<std::shared_ptr<::grpc::Service>> services_{};
    litebus::Promise<bool> serverReady_;
    CommonGrpcServerConfig config_;
};
} // namespace functionsystem::grpc

#endif  // COMMON_RPC_SERVER_GRPC_SERVER_H
