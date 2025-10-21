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

#include "remote_deployer.h"

#include <sys/stat.h>

#include <utility>

#include "constants.h"
#include "logs/logging.h"
#include "metrics/metrics_adapter.h"
#include "common/utils/exec_utils.h"
#include "ssl_config.h"
#include "utils/os_utils.hpp"

namespace functionsystem::function_agent {

const int SHA256_VALUE_LEN = 32;
const int SHA512_VALUE_LEN = 64;
const int CHAR_TO_HEX_LEN = 2;
const size_t UNZIPINFO_HEADER_LEN = 2;
const size_t CMD_OUTPUT_MAX_LEN = 1024 * 1024 * 10;
const uint32_t ZIP_FILE_BUFFER = 200;

RemoteDeployer::RemoteDeployer(messages::CodePackageThresholds codePackageThresholds, bool enableSignatureValidation)
    : codePackageThresholds_(std::move(codePackageThresholds)), enableSignatureValidation_(enableSignatureValidation)
{
}

std::string RemoteDeployer::GetDestination(const std::string &deployDir, const std::string &bucketID,
                                           const std::string &objectID)
{
    std::string layerDir = litebus::os::Join(deployDir, "layer");
    std::string funcDir = litebus::os::Join(layerDir, "func");

    std::string bucketDir = litebus::os::Join(funcDir, bucketID);
    return litebus::os::Join(bucketDir, TransMultiLevelDirToSingle(objectID));
}

bool RemoteDeployer::IsDeployed(const std::string &destination, bool isMonopoly)
{
    if (!litebus::os::ExistPath(destination)) {
        return false;
    }
    // if single-pod for multi-function and the directory exists, function has been deployed.
    if (!isMonopoly) {
        return true;
    }
    auto option = litebus::os::Ls(destination);
    if (option.IsSome() && !option.Get().empty()) {
        return true;
    }
    return false;
}

DeployResult RemoteDeployer::Deploy(const std::shared_ptr<messages::DeployRequest> &request)
{
    const ::messages::DeploymentConfig &config = request->deploymentconfig();
    YRLOG_DEBUG("S3 deployer received Deploy request to directory {}, bucketID {} , objectID {}", config.deploydir(),
                config.bucketid(), config.objectid());

    DeployResult result;
    result.destination = config.deploydir();
    YRLOG_WARN("s3 deployer is not supported, skip it");
    result.status = Status::OK();
    return result;
}

bool RemoteDeployer::Clear(const std::string &filePath, const std::string &objectKey)
{
    return true;
}
}  // namespace functionsystem::function_agent
