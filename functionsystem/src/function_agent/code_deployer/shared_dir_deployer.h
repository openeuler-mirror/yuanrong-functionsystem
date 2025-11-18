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


#pragma once

#include "deployer.h"

namespace functionsystem::function_agent {

class SharedDirDeployer : public Deployer {
public:
    explicit SharedDirDeployer();

    ~SharedDirDeployer() override = default;

    std::string GetDestination(const std::string &deployDir, [[maybe_unused]] const std::string &,
                               const std::string &sharedDir) override;

    bool IsDeployed(const std::string &destination, [[maybe_unused]] bool) override;

    DeployResult Deploy(const std::shared_ptr<messages::DeployRequest> &request) override;

    bool Clear(const std::string &destination, const std::string &sharedDir) override;

    void SetTTL(const std::string &destination, int seconds) override;

    int GetTTL(const std::string &destination) override;

private:
    std::unordered_map<std::string, int> ttlMap_;
};

}  // namespace functionsystem::function_agent
