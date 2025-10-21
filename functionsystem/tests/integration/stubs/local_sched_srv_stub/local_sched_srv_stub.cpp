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

#include "local_sched_srv_stub.h"

namespace functionsystem::test {
void LocalSchedulerServiceStub::InitControlClient(const std::string &instanceID, const std::string &runtimeID,
                                                  const std::string &target,
                                                  const std::shared_ptr< ::grpc::ChannelCredentials> &creds)
{
    grpc::ControlClientConfig config{ .target = target,
                                      .creds = ::grpc::InsecureChannelCredentials(),
                                      .timeoutSec = 30,
                                      .maxGrpcSize = 4 };
    controlClient_ = std::make_shared<grpc::ControlClient>(instanceID, runtimeID, config);
    YRLOG_INFO("start control client");
    controlClient_->Start();
}

void LocalSchedulerServiceStub::StopControlClient() const
{
    YRLOG_INFO("stop control client");
    controlClient_->Stop();
}

void LocalSchedulerServiceStub::RegisterHandler(runtime_rpc::StreamingMessage::BodyCase type,
                                                const functionsystem::grpc::PosixFunctionSysControlHandler &func)
{
    grpc::ControlClient::RegisterPosixHandler(type, func);
}

litebus::Future<runtime_rpc::StreamingMessage> LocalSchedulerServiceStub::SendMessage(
    const std::shared_ptr<runtime_rpc::StreamingMessage> &request)
{
    return controlClient_->Send(request);
}
}  // namespace functionsystem::test
