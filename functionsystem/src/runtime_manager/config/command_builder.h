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

#include <memory>
#include <unordered_map>

#include "build.h"
#include "common/proto/pb/message_pb.h"
#include "language/language_strategy.h"
#include "runtime_manager/executor/executor.h"

namespace functionsystem::runtime_manager {

/**
 * CommandBuilder: thin dispatcher from language tag → LanguageCommandStrategy.
 *
 * Responsibilities:
 *   - Route BuildArgs() calls to the correct per-language strategy.
 *   - Merge environment variables with documented precedence rules.
 *   - Resolve exec path from RuntimeConfig (for POSIX custom runtime).
 *
 * CommandBuilder itself contains NO language-specific logic.
 * All language knowledge lives in the LanguageCommandStrategy implementations.
 */
class CommandBuilder {
public:
    CommandBuilder() = default;
    explicit CommandBuilder(bool execLookPath);

    void SetRuntimeConfig(RuntimeConfig config)
    {
        config_ = std::move(config);
    }

    const RuntimeConfig &GetConfig() const
    {
        return config_;
    }

    /**
     * Register a strategy for a language tag.
     * Multiple tags may map to the same strategy type (e.g. python3.6..python3.11).
     * Called during Init; not thread-safe after construction.
     */
    void RegisterStrategy(const std::string &languageTag, std::unique_ptr<LanguageCommandStrategy> strategy);

    /**
     * Build startup arguments for the given language.
     * Pure: does not modify request, does not call chdir().
     *
     * @param language  Language tag (e.g. "python3.9", "java17").
     * @param port      Allocated port string.
     * @param request   StartInstanceRequest (read-only).
     * @return          CommandArgs on success; error Status on failure.
     */
    std::pair<Status, CommandArgs> BuildArgs(const std::string &language, const std::string &port,
                                             const messages::StartInstanceRequest &request) const;

    /**
     * Merge environment variable sources with the following precedence
     * (highest wins, except LD_LIBRARY_PATH which is appended):
     *
     *   1. Framework envs (log level, log path, ds timeout) — always override
     *   2. User envs — override posix/custom envs
     *   3. Custom resource envs — override posix envs
     *   4. Posix envs — base layer
     *   5. Inherited host envs (YR_* prefix, or all if inheritEnv=true) — fill gaps only
     */
    std::map<std::string, std::string> CombineEnvs(const Envs &envs) const;

    /**
     * Resolve the executable path from a RuntimeConfig proto.
     * Handles POSIX custom runtime's multiple exec-path cases.
     */
    std::string GetExecPathFromRuntimeConfig(const messages::RuntimeConfig &config) const;

private:
    std::string GetExecPath(const std::string &language) const;
    std::string GetLanguageTag(const std::string &language) const;
    void InheritEnv(std::map<std::string, std::string> &envs) const;

    std::unordered_map<std::string, std::shared_ptr<LanguageCommandStrategy>> strategies_;
    RuntimeConfig config_;
    bool execLookPath_ = true;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_CONFIG_COMMAND_BUILDER_H
