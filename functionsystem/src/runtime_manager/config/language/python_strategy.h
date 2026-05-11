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

#ifndef RUNTIME_MANAGER_CONFIG_LANGUAGE_PYTHON_STRATEGY_H
#define RUNTIME_MANAGER_CONFIG_LANGUAGE_PYTHON_STRATEGY_H

#include "language_strategy.h"

namespace functionsystem::runtime_manager {

class PythonCommandStrategy : public LanguageCommandStrategy {
public:
    explicit PythonCommandStrategy(bool execLookPath = true) : execLookPath_(execLookPath) {}

    std::pair<Status, CommandArgs> BuildArgs(const messages::StartInstanceRequest &request,
                                             const std::string &port,
                                             const RuntimeConfig &config) const override;

private:
    std::pair<Status, std::string> ResolveExecPath(
        const google::protobuf::Map<std::string, std::string> &deployOptions,
        const messages::RuntimeInstanceInfo &info,
        const RuntimeConfig &config) const;

    std::pair<Status, std::string> ResolveWorkingDir(
        const messages::RuntimeInstanceInfo &info) const;

    bool execLookPath_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_CONFIG_LANGUAGE_PYTHON_STRATEGY_H
