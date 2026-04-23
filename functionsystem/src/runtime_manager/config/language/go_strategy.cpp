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

#include "go_strategy.h"

#include "common/logs/logging.h"
#include "runtime_manager/config/build.h"
#include "runtime_manager/utils/utils.h"

namespace functionsystem::runtime_manager {

namespace {
const std::string GO_NEW_EXEC_PATH = "/go/bin/goruntime";
const std::string GO_PROGRAM_NAME = "goruntime";
const std::string RUNTIME_ID_ARG_PREFIX = "-runtimeId=";
const std::string INSTANCE_ID_ARG_PREFIX = "-instanceId=";
const std::string LOG_LEVEL_PREFIX = "-logLevel=";
const std::string GRPC_ADDRESS_PREFIX = "-grpcAddress=";
}  // namespace

std::pair<Status, CommandArgs> GoCommandStrategy::BuildArgs(const messages::StartInstanceRequest &request,
                                                             const std::string &port,
                                                             const RuntimeConfig &config) const
{
    const auto &info = request.runtimeinstanceinfo();
    YRLOG_DEBUG("{}|{}|GoCommandStrategy::BuildArgs instance({}) runtime({})", info.traceid(), info.requestid(),
                info.instanceid(), info.runtimeid());

    std::string address = GetPosixAddress(config, port);

    CommandArgs result;
    result.execPath = config.runtimePath + GO_NEW_EXEC_PATH;
    result.args = {GO_PROGRAM_NAME,
                   RUNTIME_ID_ARG_PREFIX + info.runtimeid(),
                   INSTANCE_ID_ARG_PREFIX + info.instanceid(),
                   LOG_LEVEL_PREFIX + config.runtimeLogLevel,
                   GRPC_ADDRESS_PREFIX + address};
    return {Status::OK(), std::move(result)};
}

}  // namespace functionsystem::runtime_manager
