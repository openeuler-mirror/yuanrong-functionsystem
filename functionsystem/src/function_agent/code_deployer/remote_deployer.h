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

#ifndef FUNCTION_AGENT_REMOTE_DEPLOYER_H
#define FUNCTION_AGENT_REMOTE_DEPLOYER_H

#include "deployer.h"

namespace functionsystem::function_agent {

const uint64_t SIZE_MEGA_BYTES = 1024 * 1024;

class RemoteDeployer : public Deployer {
public:
    explicit RemoteDeployer(messages::CodePackageThresholds codePackageThresholds,
                            bool enableSignatureValidation = false);

    ~RemoteDeployer() override = default;

    std::string GetDestination(const std::string &deployDir, const std::string &bucketID,
                               const std::string &objectID) override;

    bool IsDeployed(const std::string &destination, bool isMonopoly) override;

    DeployResult Deploy(const std::shared_ptr<messages::DeployRequest> &request) override;

    bool Clear(const std::string &filePath, const std::string &objectKey) override;

    virtual Status UnzipFile(const std::string &destDir, const std::string &destFile);

    virtual Status DownloadCode(const std::string &destFile, const ::messages::DeploymentConfig &config) = 0;

    virtual Status PackageValidation(const std::string &destFile, const std::string &sha512Str,
                                     const std::string &sha256Str);

protected:
    virtual void CollectFunctionInfo(const std::string &destFile,
                                     const std::shared_ptr<messages::DeployRequest> &request);

    Status CheckSha256(const std::string &destFile, const std::string &sha256Str);
    Status CheckSha512(const std::string &destFile, const std::string &sha512Str);
    Status CheckFileContent(const std::string &destFile);
    std::string GetFileNameFromObjectID(const std::string &objectID);
protected:
    messages::CodePackageThresholds codePackageThresholds_;

    uint64_t unzipFileSizeMaxBytes_{ 0 };

    bool enableSignatureValidation_{ false };

private:
    Status CheckZipFile(const std::string &path) const;
};

}  // namespace functionsystem::function_agent

#endif  // FUNCTION_AGENT_REMOTE_DEPLOYER_H