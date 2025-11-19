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

#include "local_deployer.h"

#include "common/logs/logging.h"

namespace functionsystem::function_agent {

std::string LocalDeployer::GetDestination(const std::string &deployDir, const std::string &bucketID,
                                          const std::string &objectID)
{
    return deployDir;
}

DeployResult LocalDeployer::Deploy(const std::shared_ptr<messages::DeployRequest> &request)
{
    DeployResult deployRes;
    std::string deployPath = request->deploymentconfig().deploydir();
    YRLOG_DEBUG("local deployer received Deploy request to directory {}", deployPath);

    deployRes.destination = deployPath;
    return deployRes;
}

bool LocalDeployer::Clear(const std::string &filePath, const std::string &objectKey)
{
    YRLOG_DEBUG("local deployer received Clear request of object {} from path {}", objectKey, filePath);
    return true;
}

bool LocalDeployer::IsDeployed(const std::string &destination, bool isMonopoly)
{
    return true;
}

}  // namespace functionsystem::function_agent
