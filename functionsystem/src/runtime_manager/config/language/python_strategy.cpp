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

#include "python_strategy.h"

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/utils/path.h"
#include "runtime_manager/config/build.h"
#include "runtime_manager/utils/utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem::runtime_manager {

namespace {
const std::string PYTHON_JOB_ID_PREFIX = "job-";
const std::string CHDIR_PATH_CONFIG = "CHDIR_PATH";
}  // namespace

static bool IsEnableConda(const google::protobuf::Map<std::string, std::string> &deployOptions)
{
    return deployOptions.count(CONDA_PREFIX) && deployOptions.count(CONDA_DEFAULT_ENV);
}

std::pair<Status, std::string> PythonCommandStrategy::ResolveExecPath(
    const google::protobuf::Map<std::string, std::string> &deployOptions,
    const messages::RuntimeInstanceInfo &info,
    const RuntimeConfig &config) const
{
    if (IsEnableConda(deployOptions)) {
        const auto &condaPrefix = deployOptions.at(CONDA_PREFIX);
        const auto &condaEnv = deployOptions.at(CONDA_DEFAULT_ENV);
        std::string execPath =
            litebus::os::Join(litebus::os::Join(litebus::os::Join(litebus::os::Join(condaPrefix, "envs"), condaEnv),
                                                "bin"),
                              "python");
        YRLOG_INFO("{}|{}|conda python execPath: {}", info.traceid(), info.requestid(), execPath);
        return {Status::OK(), execPath};
    }

    const std::string &language = info.runtimeconfig().language();
    if (!execLookPath_) {
        return {Status::OK(), language};
    }
    auto path = LookPath(language);
    if (path.IsNone()) {
        YRLOG_ERROR("{}|{}|python LookPath failed for language: {}", info.traceid(), info.requestid(), language);
        return {Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "python exec path not found"), ""};
    }
    return {Status::OK(), path.Get()};
}

std::pair<Status, std::string> PythonCommandStrategy::ResolveWorkingDir(
    const messages::RuntimeInstanceInfo &info) const
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

std::pair<Status, CommandArgs> PythonCommandStrategy::BuildArgs(const messages::StartInstanceRequest &request,
                                                                  const std::string &port,
                                                                  const RuntimeConfig &config) const
{
    const auto &info = request.runtimeinstanceinfo();
    const auto &deployOptions = info.deploymentconfig().deployoptions();
    YRLOG_DEBUG("{}|{}|PythonCommandStrategy::BuildArgs", info.traceid(), info.requestid());

    auto [execStatus, execPath] = ResolveExecPath(deployOptions, info, config);
    if (execStatus.IsError()) {
        return {execStatus, {}};
    }

    auto [wdStatus, deployDir] = ResolveWorkingDir(info);
    if (wdStatus.IsError()) {
        return {wdStatus, {}};
    }
    if (deployDir.empty()) {
        YRLOG_ERROR("{}|{}|python deploy dir is empty", info.traceid(), info.requestid());
        return {Status(StatusCode::RUNTIME_MANAGER_DEPLOY_DIR_IS_EMPTY, "deploy dir is empty"), {}};
    }

    if (!litebus::os::ExistPath(deployDir)) {
        if (!litebus::os::Mkdir(deployDir).IsNone()) {
            YRLOG_WARN("{}|{}|failed to mkdir deployDir({}): {}", info.traceid(), info.requestid(), deployDir,
                       litebus::os::Strerror(errno));
            return {Status(StatusCode::RUNTIME_MANAGER_CONDA_PARAMS_INVALID, "failed to make dir deployDir"), {}};
        }
    }

    std::string address = GetPosixAddress(config, port);
    std::string jobID = PYTHON_JOB_ID_PREFIX + Utils::GetJobIDFromTraceID(info.traceid());

    CommandArgs result;
    result.execPath = execPath;
    result.args = {"--rt_server_address", address,  "--deploy_dir", deployDir,
                   "--runtime_id",        info.runtimeid(), "--job_id", jobID,
                   "--log_level",         config.runtimeLogLevel};
    result.workingDir = deployDir;
    result.deployOptionOverrides[CHDIR_PATH_CONFIG] = deployDir;
    return {Status::OK(), std::move(result)};
}

}  // namespace functionsystem::runtime_manager
