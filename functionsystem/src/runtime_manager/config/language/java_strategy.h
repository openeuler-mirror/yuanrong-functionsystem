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

#ifndef RUNTIME_MANAGER_CONFIG_LANGUAGE_JAVA_STRATEGY_H
#define RUNTIME_MANAGER_CONFIG_LANGUAGE_JAVA_STRATEGY_H

#include "language_strategy.h"

namespace functionsystem::runtime_manager {

/**
 * Java command strategy.
 *
 * Handles all Java versions (1.8 / 11 / 17 / 21) in a single class.
 * JVM args are selected based on the language tag in RuntimeConfig
 * via SelectJvmArgs(), replacing the 4 nearly-identical legacy functions.
 */
class JavaCommandStrategy : public LanguageCommandStrategy {
public:
    explicit JavaCommandStrategy(bool execLookPath = true) : execLookPath_(execLookPath) {}

    std::pair<Status, CommandArgs> BuildArgs(const messages::StartInstanceRequest &request,
                                             const std::string &port,
                                             const RuntimeConfig &config) const override;

private:
    // Select jvmArgs from config based on the language version tag.
    const std::vector<std::string> &SelectJvmArgs(const std::string &language,
                                                   const RuntimeConfig &config) const;

    bool execLookPath_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_CONFIG_LANGUAGE_JAVA_STRATEGY_H
