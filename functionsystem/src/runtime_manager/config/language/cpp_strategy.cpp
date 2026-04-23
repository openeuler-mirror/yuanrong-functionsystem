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

#include "cpp_strategy.h"

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "runtime_manager/config/build.h"
#include "runtime_manager/utils/utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem::runtime_manager {

namespace {
const std::string CPP_NEW_EXEC_PATH = "/cpp/bin/runtime";
const std::string CPP_PROGRAM_NAME = "cppruntime";
const std::string RUNTIME_ID_ARG_PREFIX = "-runtimeId=";
const std::string LOG_LEVEL_PREFIX = "-logLevel=";
const std::string GRPC_ADDRESS_PREFIX = "-grpcAddress=";
const std::string CONFIG_PATH_PREFIX = "-runtimeConfigPath=";
const std::string JOB_ID_PREFIX = "-jobId=job-";
const std::string CHDIR_PATH_CONFIG = "CHDIR_PATH";
}  // namespace

static std::pair<Status, std::string> ResolveCppWorkingDir(const messages::RuntimeInstanceInfo &info)
{
    const auto &posixEnvs = info.runtimeconfig().posixenvs();
    auto workingDirIter = posixEnvs.find(UNZIPPED_WORKING_DIR);
    auto fileIter = posixEnvs.find(YR_WORKING_DIR);

    if (workingDirIter == posixEnvs.end() || fileIter == posixEnvs.end()) {
        return {Status::OK(), info.deploymentconfig().deploydir()};
    }
    if (workingDirIter->second.empty() || fileIter->second.empty()) {
        YRLOG_ERROR("{}|{}|working dir({}) or unzipped dir({}) is empty", info.traceid(), info.requestid(),
                    fileIter->second, workingDirIter->second);
        return {Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND,
                       "params working dir or unzipped dir is empty"),
                ""};
    }
    if (workingDirIter->second.size() > 4 &&
        workingDirIter->second.substr(workingDirIter->second.size() - 4) == ".img") {
        return {Status::OK(), info.container().mountpoint()};
    }

    char canonicalPath[PATH_MAX];
    if (realpath(workingDirIter->second.c_str(), canonicalPath) == nullptr) {
        return {Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND, "cannot resolve path"), ""};
    }
    if (access(canonicalPath, R_OK | W_OK | X_OK) != 0) {
        return {Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND, "insufficient directory permissions"),
                ""};
    }
    return {Status::OK(), workingDirIter->second};
}

std::pair<Status, CommandArgs> CppCommandStrategy::BuildArgs(const messages::StartInstanceRequest &request,
                                                              const std::string &port,
                                                              const RuntimeConfig &config) const
{
    const auto &info = request.runtimeinstanceinfo();
    YRLOG_DEBUG("{}|{}|CppCommandStrategy::BuildArgs", info.traceid(), info.requestid());

    auto [wdStatus, workingDir] = ResolveCppWorkingDir(info);
    if (wdStatus.IsError()) {
        return {wdStatus, {}};
    }

    std::string address = GetPosixAddress(config, port);
    std::string confPath = litebus::os::Join(config.runtimeConfigPath, "runtime.json");

    CommandArgs result;
    result.execPath = config.runtimePath + CPP_NEW_EXEC_PATH;
    result.args = {CPP_PROGRAM_NAME,
                   RUNTIME_ID_ARG_PREFIX + info.runtimeid(),
                   LOG_LEVEL_PREFIX + config.runtimeLogLevel,
                   JOB_ID_PREFIX + Utils::GetJobIDFromTraceID(info.traceid()),
                   GRPC_ADDRESS_PREFIX + address,
                   CONFIG_PATH_PREFIX + confPath};
    result.workingDir = workingDir;
    if (!workingDir.empty()) {
        result.deployOptionOverrides[CHDIR_PATH_CONFIG] = workingDir;
    }
    return {Status::OK(), std::move(result)};
}

}  // namespace functionsystem::runtime_manager
