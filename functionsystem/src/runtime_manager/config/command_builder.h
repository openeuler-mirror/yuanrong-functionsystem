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

#ifndef RUNTIME_MANAGER_CONFIG_COMMAND_BUILDER_H
#define RUNTIME_MANAGER_CONFIG_COMMAND_BUILDER_H

#include "build.h"
#include "common/proto/pb/message_pb.h"
#include "runtime_manager/executor/executor.h"

namespace functionsystem::runtime_manager {
class CommandBuilder {
public:
    CommandBuilder(RuntimeConfig config, bool execLookPath) : config_(config), execLookPath_(execLookPath)
    {
    }
    CommandBuilder(bool execLookPath = true) : execLookPath_(execLookPath) {}
    ~CommandBuilder() = default;

    void SetRuntimeConfig(RuntimeConfig config)
    {
        config_ = config;
    }

    std::string GetExecPath(const std::string &language) const;

    std::string GetExecPathFromRuntimeConfig(const messages::RuntimeConfig &config) const;

    std::string GetLanguageArg(const std::string &language) const;

    std::map<std::string, std::string> CombineEnvs(const Envs &envs) const;

    Status GetBuildArgs(const std::string &language, const std::string &port,
                        const std::shared_ptr<messages::StartInstanceRequest> &request, std::vector<std::string> &args);

private:
    void InheritEnv(std::map<std::string, std::string> &combineEnvs) const;

    std::pair<Status, std::vector<std::string>> GetCppBuildArgs(
        const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::pair<Status, std::vector<std::string>> GetGoBuildArgs(
        const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::pair<Status, std::string> GetPythonExecPath(
        const google::protobuf::Map<std::string, std::string> &deployOptions,
        const messages::RuntimeInstanceInfo &info) const;
    std::pair<Status, std::string> HandleWorkingDirectory(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const messages::RuntimeInstanceInfo &info) const;

    std::pair<Status, std::vector<std::string>> GetPythonBuildArgs(
        const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::pair<Status, std::vector<std::string>> PythonBuildFinalArgs(const std::string &port,
                                                                     const std::string &execPath,
                                                                     const std::string &deployDir,
                                                                     const messages::RuntimeInstanceInfo &info) const;

    std::pair<Status, std::vector<std::string>> GetJavaBuildArgs(
        const std::string &port, const std::vector<std::string> &jvmArgs,
        const std::shared_ptr<messages::StartInstanceRequest> &request) const;
        
    std::pair<Status, std::vector<std::string>> GetJavaBuildArgsDefault(
        const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::pair<Status, std::vector<std::string>> GetJavaBuildArgsForJava11(
        const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::pair<Status, std::vector<std::string>> GetJavaBuildArgsForJava17(
        const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::pair<Status, std::vector<std::string>> GetJavaBuildArgsForJava21(
        const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::pair<Status, std::vector<std::string>> GetNodejsBuildArgs(
        const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::pair<Status, std::vector<std::string>> GetPosixCustomBuildArgs(
        const std::string &, const std::shared_ptr<messages::StartInstanceRequest> &request) const;

    std::map<const std::string, std::pair<Status, std::vector<std::string>> (CommandBuilder::*)(
                                    const std::string &, const std::shared_ptr<messages::StartInstanceRequest> &) const>
        buildArgsFunc_ = { { CPP_LANGUAGE, &CommandBuilder::GetCppBuildArgs },
                           { GO_LANGUAGE, &CommandBuilder::GetGoBuildArgs },
                           { JAVA_LANGUAGE, &CommandBuilder::GetJavaBuildArgsDefault },
                           { JAVA11_LANGUAGE, &CommandBuilder::GetJavaBuildArgsForJava11 },
                           { JAVA17_LANGUAGE, &CommandBuilder::GetJavaBuildArgsForJava17 },
                           { JAVA21_LANGUAGE, &CommandBuilder::GetJavaBuildArgsForJava21 },
                           { POSIX_CUSTOM_RUNTIME, &CommandBuilder::GetPosixCustomBuildArgs },
                           { NODE_JS, &CommandBuilder::GetNodejsBuildArgs },
                           { PYTHON_LANGUAGE, &CommandBuilder::GetPythonBuildArgs },
                           { PYTHON3_LANGUAGE, &CommandBuilder::GetPythonBuildArgs },
                           { PYTHON36_LANGUAGE, &CommandBuilder::GetPythonBuildArgs },
                           { PYTHON37_LANGUAGE, &CommandBuilder::GetPythonBuildArgs },
                           { PYTHON38_LANGUAGE, &CommandBuilder::GetPythonBuildArgs },
                           { PYTHON39_LANGUAGE, &CommandBuilder::GetPythonBuildArgs },
                           { PYTHON310_LANGUAGE, &CommandBuilder::GetPythonBuildArgs },
                           { PYTHON311_LANGUAGE, &CommandBuilder::GetPythonBuildArgs } };
    RuntimeConfig config_;
    bool execLookPath_ = true;
};
}

#endif
