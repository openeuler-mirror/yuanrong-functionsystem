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

#ifndef FUNCTION_AGENT_DEPLOYER_H
#define FUNCTION_AGENT_DEPLOYER_H

#include <string>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem::function_agent {

struct DeployResult {
    Status status;

    std::string destination;
};

class Deployer {
public:
    Deployer() = default;

    virtual ~Deployer() = default;

    virtual std::string GetDestination(const std::string &deployDir, const std::string &bucketID,
                                       const std::string &objectID) = 0;

    virtual bool IsDeployed(const std::string &destination, bool isMonopoly) = 0;

    virtual DeployResult Deploy(const std::shared_ptr<messages::DeployRequest> &request) = 0;

    virtual bool Clear(const std::string &filePath, const std::string &objectKey) = 0;

    virtual void SetTTL(const std::string &destination, int seconds)
    {
    }

    virtual int GetTTL(const std::string &destination)
    {
        return -1;
    }
};
}  // end of namespace functionsystem::function_agent

#endif  // FUNCTION_AGENT_DEPLOYER_H