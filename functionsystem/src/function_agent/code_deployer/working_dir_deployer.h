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

#ifndef FUNCTION_AGENT_WORKING_DIR_DEPLOYER_H
#define FUNCTION_AGENT_WORKING_DIR_DEPLOYER_H

#include "deployer.h"

namespace functionsystem::function_agent {

class WorkingDirDeployer : public Deployer {
public:
    explicit WorkingDirDeployer();

    ~WorkingDirDeployer() override = default;

    std::string GetDestination(const std::string &deployDir, const std::string &uriFile,
                               const std::string &appID) override;

    bool IsDeployed(const std::string &destination, bool isMonopoly) override;

    DeployResult Deploy(const std::shared_ptr<messages::DeployRequest> &request) override;

    bool Clear(const std::string &filePath, const std::string &objectKey) override;

    Status UnzipFile(const std::string &destDir, const std::string &workingDirZipFile);

private:
    std::string baseDeployDir_;
};

}  // namespace functionsystem::function_agent

#endif  // FUNCTION_AGENT_WORKING_DIR_DEPLOYER_H
