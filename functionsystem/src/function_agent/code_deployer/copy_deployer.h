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

#ifndef FUNCTION_AGENT_COPY_DEPLOYER_H
#define FUNCTION_AGENT_COPY_DEPLOYER_H

#include "deployer.h"

namespace functionsystem::function_agent {

class CopyDeployer : public Deployer {
public:
    explicit CopyDeployer();

    ~CopyDeployer() override = default;

    std::string GetDestination(const std::string &deployDir, const std::string &bucketID,
                               const std::string &objectID) override;

    bool IsDeployed(const std::string &destination, bool isMonopoly) override;

    DeployResult Deploy(const std::shared_ptr<messages::DeployRequest> &request) override;

    bool Clear(const std::string &filePath, const std::string &objectKey) override;

    Status CopyFile(const std::string &srcDir, const std::string &destDir);

    // for test
    [[maybe_unused]] void SetBaseDeployDir(const std::string &dir)
    {
        baseDeployDir_ = dir;
    }

private:
    std::unordered_map<std::string, std::string> codeDirMap_;
    std::unordered_map<std::string, std::string> codeDestDirMap_;
    std::string baseDeployDir_;
};

}  // namespace functionsystem::function_agent

#endif  // FUNCTION_AGENT_COPY_DEPLOYER_H
