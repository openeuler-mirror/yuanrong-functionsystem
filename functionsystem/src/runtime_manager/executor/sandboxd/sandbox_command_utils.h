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

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_COMMAND_UTILS_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_COMMAND_UTILS_H

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "common/proto/pb/message_pb.h"

namespace functionsystem::runtime_manager {


inline bool HasSandboxCustomRootfs(const messages::RuntimeInstanceInfo &info)
{
    const auto &opts = info.deploymentconfig().deployoptions();
    if (opts.find("rootfs") != opts.end()) {
        return true;
    }
    return info.has_container() && info.container().has_rootfsconfig();
}

inline bool HasSelfContainedSandboxBootstrap(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &bootstrapConfig = info.bootstrapconfig();
    return HasSandboxCustomRootfs(info) && (!bootstrapConfig.entrypoint().empty() || !bootstrapConfig.cmd().empty());
}

inline std::vector<std::string> BuildBootstrapCommands(
    const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    std::vector<std::string> commands;
    const auto &bootstrapConfig = request->runtimeinstanceinfo().bootstrapconfig();
    for (const std::string *source : {&bootstrapConfig.entrypoint(), &bootstrapConfig.cmd()}) {
        if (source->empty()) {
            continue;
        }
        std::istringstream stream(*source);
        std::string token;
        while (stream >> token) {
            commands.push_back(token);
        }
    }
    return commands;
}

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_COMMAND_UTILS_H