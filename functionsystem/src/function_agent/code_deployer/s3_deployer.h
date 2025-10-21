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

#ifndef FUNCTION_AGENT_S3_DEPLOYER_H
#define FUNCTION_AGENT_S3_DEPLOYER_H

#include <utility>

#include "common/utils/s3_config.h"
#include "remote_deployer.h"

namespace functionsystem::function_agent {
class S3Deployer : public RemoteDeployer {
    uint32_t gReconnectObsRetryCount = 3;
    uint32_t gDownloadInitObsRetryTime = 1;
    uint32_t gDownloadCodeRetryCount = 3;

public:
    explicit S3Deployer(std::shared_ptr<S3Config> config, messages::CodePackageThresholds codePackageThresholds,
                        bool enableSignatureValidation = false)
        : RemoteDeployer(std::move(codePackageThresholds), enableSignatureValidation), s3Config_(std::move(config))
    {
    }

    ~S3Deployer() override;

    Status DownloadCode(const std::string &destFile, const ::messages::DeploymentConfig &config) override;
private:
    std::shared_ptr<S3Config> s3Config_;
};

}  // namespace functionsystem::function_agent

#endif  // FUNCTION_AGENT_S3_DEPLOYER_H