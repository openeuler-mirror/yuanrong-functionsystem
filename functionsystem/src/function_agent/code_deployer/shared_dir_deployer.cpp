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


#include "shared_dir_deployer.h"

#include <algorithm>
#include <cctype>  // 用于 std::isalnum

#include "async/uuid_generator.hpp"
#include "common/logs/logging.h"
#include "common/metadata/metadata.h"
#include "common/utils/exec_utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem::function_agent {

const std::string DEFAULT_SHARED_DIR = "/tmp/shared";
const std::string SHARED_DIR_PREFIX = "shared";
const int MAX_NAME_LEN = 64;

std::string isValid(const std::string &str)
{
    if (str.empty()) {
        return "the shared directory name is empty";
    }
    if (str.length() > MAX_NAME_LEN) {
        return "the length of the shared directory exceeds 64 characters";
    }
    if (!std::all_of(str.begin(), str.end(),
                     [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; })) {
        return "the shared directory name contains invalid characters";
    }
    return "";
}

SharedDirDeployer::SharedDirDeployer()
{
}

std::string SharedDirDeployer::GetDestination(const std::string &deployDir, const std::string &,
                                              const std::string &sharedDir)
{
    if (deployDir.empty()) {
        return litebus::os::Join(DEFAULT_SHARED_DIR, sharedDir);
    } else {
        return litebus::os::Join(litebus::os::Join(deployDir, SHARED_DIR_PREFIX), sharedDir);
    }
}

DeployResult SharedDirDeployer::Deploy(const std::shared_ptr<messages::DeployRequest> &request)
{
    DeployResult deployRes;
    auto &deployDir = request->deploymentconfig().deploydir();
    auto &srcDir = request->deploymentconfig().objectid();
    if (auto msg = isValid(srcDir); !msg.empty()) {
        deployRes.status = Status(StatusCode::FUNC_AGENT_INVALID_DEPLOY_DIRECTORY, msg);
        return deployRes;
    }
    deployRes.destination = GetDestination(deployDir, "", srcDir);
    if (!litebus::os::ExistPath(deployRes.destination) && !litebus::os::Mkdir(deployRes.destination).IsNone()) {
        deployRes.status = Status(StatusCode::FUNC_AGENT_INVALID_DEPLOY_DIRECTORY,
                                  "failed to create shared directory, msg: " + litebus::os::Strerror(errno));
        return deployRes;
    }
    int result = chmod(deployRes.destination.c_str(), 0770);
    if (result != 0) {
        YRLOG_ERROR("failed to execute chmod error msg: {}", litebus::os::Strerror(errno));
        (void)litebus::os::Rmdir(deployRes.destination);
        deployRes.status = Status(StatusCode::FUNC_AGENT_INVALID_DEPLOY_DIRECTORY,
                                  "failed to execute chmod error msg: " + litebus::os::Strerror(errno));
        return deployRes;
    }

    deployRes.status = Status::OK();
    return deployRes;
}

bool SharedDirDeployer::Clear(const std::string &destination, const std::string &sharedDir)
{
    YRLOG_INFO("clear shared directory: {}", destination);
    ttlMap_.erase(destination);
    return ClearFile(destination, sharedDir);
}

bool SharedDirDeployer::IsDeployed(const std::string &destination, bool)
{
    return litebus::os::ExistPath(destination);
}

void SharedDirDeployer::SetTTL(const std::string &destination, int seconds)
{
    ttlMap_[destination] = seconds;
}

int SharedDirDeployer::GetTTL(const std::string &destination)
{
    if (ttlMap_.find(destination) != ttlMap_.end()) {
        return ttlMap_[destination];
    }
    return -1;
}

}  // namespace functionsystem::function_agent