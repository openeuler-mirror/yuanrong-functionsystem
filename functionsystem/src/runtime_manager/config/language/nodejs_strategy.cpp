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

#include "nodejs_strategy.h"

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/path.h"
#include "runtime_manager/config/build.h"
#include "runtime_manager/utils/utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem::runtime_manager {

namespace {
const std::string CHDIR_PATH_CONFIG = "CHDIR_PATH";
const std::string BASH_PATH = "/bin/bash";
}  // namespace

// ── NodejsCommandStrategy ────────────────────────────────────────────────────

std::pair<Status, CommandArgs> NodejsCommandStrategy::BuildArgs(const messages::StartInstanceRequest &request,
                                                                  const std::string &port,
                                                                  const RuntimeConfig &config) const
{
    const auto &info = request.runtimeinstanceinfo();
    YRLOG_DEBUG("{}|{}|NodejsCommandStrategy::BuildArgs", info.traceid(), info.requestid());

    std::string execPath;
    if (!execLookPath_) {
        execPath = NODE_JS_CMD;
    } else {
        auto path = LookPath(NODE_JS_CMD);
        if (path.IsNone()) {
            YRLOG_ERROR("{}|{}|nodejs LookPath failed", info.traceid(), info.requestid());
            return {Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "nodejs exec path not found"), {}};
        }
        execPath = path.Get();
    }

    std::string address = GetPosixAddress(config, port);
    std::string jobID = Utils::GetJobIDFromTraceID(info.traceid());

    // Optionally cap V8 heap from memory resource
    std::string memoryFlag;
    for (const auto &resource : info.runtimeconfig().resources().resources()) {
        if (resource.first == resource_view::MEMORY_RESOURCE_NAME) {
            auto memVal = resource.second.scalar().value();
            if (memVal > 0 && memVal < static_cast<double>(std::numeric_limits<int>::max())) {
                memoryFlag = "--max-old-space-size=" + std::to_string(static_cast<int>(memVal));
            }
            break;
        }
    }

    std::vector<std::string> args;
    if (!memoryFlag.empty()) {
        args.emplace_back(memoryFlag);
    }
    args.emplace_back("/home/snuser/runtime/nodejs/wrapper.js");
    args.emplace_back("--rt_server_address=" + address);
    args.emplace_back("--runtime_id=" + info.runtimeid());
    args.emplace_back("--job_id=" + jobID);
    args.emplace_back("--log_level=" + config.runtimeLogLevel);

    CommandArgs result;
    result.execPath = execPath;
    result.args = std::move(args);
    return {Status::OK(), std::move(result)};
}

// ── PosixCustomCommandStrategy ───────────────────────────────────────────────

std::pair<Status, CommandArgs> PosixCustomCommandStrategy::BuildArgs(const messages::StartInstanceRequest &request,
                                                                       const std::string & /*port*/,
                                                                       const RuntimeConfig & /*config*/) const
{
    const auto &info = request.runtimeinstanceinfo();
    const auto &posixEnvs = info.runtimeconfig().posixenvs();
    YRLOG_DEBUG("{}|{}|PosixCustomCommandStrategy::BuildArgs", info.traceid(), info.requestid());

    // Case 1: delegate-bootstrap script (C++ FaaS style)
    auto bootstrapIt = posixEnvs.find(ENV_DELEGATE_BOOTSTRAP);
    auto downloadIt = posixEnvs.find(ENV_DELEGATE_DOWNLOAD);
    if (bootstrapIt != posixEnvs.end() && downloadIt != posixEnvs.end()) {
        YRLOG_DEBUG("{}|{}|posix-custom: using delegate entry", info.traceid(), info.requestid());
        // exec_path will be overridden by GetExecPathFromRuntimeConfig; args are empty
        CommandArgs result;
        result.execPath = BASH_PATH;
        return {Status::OK(), std::move(result)};
    }

    // Case 2: job working-dir entrypoint
    auto workingDirIt = posixEnvs.find(UNZIPPED_WORKING_DIR);
    auto fileIt = posixEnvs.find(YR_WORKING_DIR);
    if (workingDirIt != posixEnvs.end() && fileIt != posixEnvs.end()) {
        YRLOG_DEBUG("{}|{}|posix-custom: using job entrypoint", info.traceid(), info.requestid());
        if (workingDirIt->second.empty() || fileIt->second.empty()) {
            YRLOG_ERROR("{}|{}|working dir or unzipped dir is empty", info.traceid(), info.requestid());
            return {Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND,
                           "params working dir or unzipped dir is empty"),
                    {}};
        }
        if (!litebus::os::ExistPath(workingDirIt->second)) {
            YRLOG_ERROR("{}|{}|job working dir not found: {}", info.traceid(), info.requestid(),
                        workingDirIt->second);
            return {Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND, "job working dir is invalid"),
                    {}};
        }
        CommandArgs result;
        result.execPath = BASH_PATH;
        result.workingDir = workingDirIt->second;
        result.deployOptionOverrides[CHDIR_PATH_CONFIG] = workingDirIt->second;
        return {Status::OK(), std::move(result)};
    }

    // Case 3: explicit entryFile + /bootstrap
    std::string entryFile = info.runtimeconfig().entryfile();
    if (entryFile.empty()) {
        YRLOG_ERROR("{}|{}|posix-custom entryFile is empty", info.traceid(), info.requestid());
        return {Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "entryFile is empty"), {}};
    }
    if (!litebus::os::ExistPath(entryFile)) {
        YRLOG_ERROR("{}|{}|posix-custom entryFile not found: {}", info.traceid(), info.requestid(), entryFile);
        return {Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "entryFile path not found"), {}};
    }
    const std::string bootstrapPath = entryFile + "/bootstrap";
    if (!litebus::os::ExistPath(bootstrapPath)) {
        YRLOG_ERROR("{}|{}|posix-custom bootstrap not found: {}", info.traceid(), info.requestid(), bootstrapPath);
        return {Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "bootstrap script not found"), {}};
    }

    CommandArgs result;
    result.execPath = BASH_PATH;
    result.args = {bootstrapPath};
    result.workingDir = entryFile;
    result.deployOptionOverrides[CHDIR_PATH_CONFIG] = entryFile;
    return {Status::OK(), std::move(result)};
}
}  // namespace functionsystem::runtime_manager
