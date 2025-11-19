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

#include "copy_deployer.h"

#include "async/uuid_generator.hpp"
#include "common/logs/logging.h"
#include "common/metadata/metadata.h"
#include "common/utils/exec_utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem::function_agent {

CopyDeployer::CopyDeployer()
{
    auto baseDir = GetDeployDir();
    std::string layerDir = litebus::os::Join(baseDir, "layer");
    std::string funcDir = litebus::os::Join(layerDir, "func");
    baseDeployDir_ = funcDir;
}

std::string CopyDeployer::GetDestination(const std::string &deployDir, const std::string &bucketID,
                                         const std::string &objectID)
{
    if (codeDirMap_.find(objectID) != codeDirMap_.end()) {
        return codeDirMap_[objectID];
    }
    auto dstDir = litebus::os::Join(baseDeployDir_, litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    codeDirMap_[objectID] = dstDir;
    codeDestDirMap_[dstDir] = objectID;
    return dstDir;
}

DeployResult CopyDeployer::Deploy(const std::shared_ptr<messages::DeployRequest> &request)
{
    DeployResult deployRes;
    auto srcDir = request->deploymentconfig().objectid();
    deployRes.destination = GetDestination("", "", srcDir);
    YRLOG_DEBUG("copy deployer received Deploy request from {} to directory {}", srcDir, deployRes.destination);
    if (!litebus::os::ExistPath(baseDeployDir_) && !litebus::os::Mkdir(baseDeployDir_).IsNone()) {
        deployRes.status = Status(StatusCode::FUNC_AGENT_INVALID_DEPLOY_DIRECTORY,
                                  "failed to create parent dir, msg: " + litebus::os::Strerror(errno));
        return deployRes;
    }

    // baseDeployDir_ is from user environment input
    auto realBaseDeployDirOpt = litebus::os::RealPath(baseDeployDir_);
    if (realBaseDeployDirOpt.IsNone()) {
        (void)litebus::os::Rmdir(baseDeployDir_);
        YRLOG_WARN("failed to get real path of baseDeployDir_({})", baseDeployDir_);
        deployRes.status = Status(StatusCode::FUNC_AGENT_INVALID_DEPLOY_DIRECTORY, "failed to get real parent dir");
        return deployRes;
    }
    baseDeployDir_ = realBaseDeployDirOpt.Get();

    if (!litebus::os::ExistPath(srcDir)) {
        YRLOG_ERROR("source code({}) is not exist", srcDir);
        deployRes.status =
            Status(StatusCode::ERR_USER_CODE_LOAD, "source code dir(" + srcDir + ")is not exist.");
        return deployRes;
    }
    Status copyStatus = CopyFile(srcDir, deployRes.destination);
    if (copyStatus.IsError()) {
        YRLOG_ERROR("failed to copy source code({})", srcDir);
        deployRes.status = copyStatus;
        return deployRes;
    }
    deployRes.status = Status::OK();
    return deployRes;
}

bool CopyDeployer::Clear(const std::string &filePath, const std::string &objectKey)
{
    if (codeDestDirMap_.find(filePath) != codeDestDirMap_.end()) {
        (void)codeDirMap_.erase(codeDestDirMap_[filePath]);
    }
    bool isClear = ClearFile(filePath, objectKey);
    if (isClear) {
        (void)codeDestDirMap_.erase(filePath);
    }
    return isClear;
}

bool CopyDeployer::IsDeployed(const std::string &destination, bool isMonopoly)
{
    return litebus::os::ExistPath(destination);
}

Status CopyDeployer::CopyFile(const std::string &srcDir, const std::string &destDir)
{
    std::string cmd = "/usr/bin/cp -ar " + srcDir + " " + destDir;
    if (!CheckIllegalChars(cmd)) {
        return Status(StatusCode::PARAMETER_ERROR, "command has invalid characters");
    }
    if (auto code(std::system(cmd.c_str())); code) {
        YRLOG_ERROR("failed to execute cp cmd({}). code: {}", cmd, code);
        (void)litebus::os::Rmdir(destDir);
        return Status(StatusCode::ERR_USER_CODE_LOAD, "failed to copy file");
    }
    cmd = "chmod -R 750 " + destDir;
    if (auto code(std::system(cmd.c_str())); code) {
        YRLOG_WARN("failed to execute chmod cmd({}). code: {}", cmd, code);
    }
    return Status::OK();
}
}  // namespace functionsystem::function_agent