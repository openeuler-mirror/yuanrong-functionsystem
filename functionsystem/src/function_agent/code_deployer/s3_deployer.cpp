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

#include "s3_deployer.h"

#include "constants.h"

namespace functionsystem::function_agent {
const uint32_t RECOVER_RETRY_COUNT = 3;

S3Deployer::~S3Deployer()
{
}

Status S3Deployer::DownloadCode(const std::string &destFile, const ::messages::DeploymentConfig &config)
{
    return Status::OK();
}

}  // namespace functionsystem::function_agent
