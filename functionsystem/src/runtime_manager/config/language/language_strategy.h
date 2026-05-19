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

#ifndef RUNTIME_MANAGER_CONFIG_LANGUAGE_STRATEGY_H
#define RUNTIME_MANAGER_CONFIG_LANGUAGE_STRATEGY_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "runtime_manager/executor/executor.h"

namespace functionsystem::runtime_manager {

/**
 * Output of BuildArgs: all information needed to launch a runtime process.
 * Pure data struct — no references to the input request.
 */
struct CommandArgs {
    std::string execPath;
    std::vector<std::string> args;
    // Working directory for the runtime process (empty = use default).
    // Caller is responsible for applying this (e.g. via chdir or container config).
    std::string workingDir;
    // Extra deploy-option key-value pairs to inject into the StartRequest proto.
    // Strategies populate this instead of mutating the input request.
    std::map<std::string, std::string> deployOptionOverrides;
};

/**
 * Per-language strategy for building runtime startup arguments.
 *
 * Contract:
 *   - BuildArgs MUST be a pure function: it must not modify `request`,
 *     must not call chdir(), and must not hold mutable state.
 *   - All output (including working directory and exec path) is returned
 *     in CommandArgs; the caller decides how to apply it.
 */
class LanguageCommandStrategy {
public:
    virtual ~LanguageCommandStrategy() = default;

    virtual std::pair<Status, CommandArgs> BuildArgs(const messages::StartInstanceRequest &request,
                                                     const std::string &port,
                                                     const RuntimeConfig &config) const = 0;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_CONFIG_LANGUAGE_STRATEGY_H
