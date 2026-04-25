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

#include "command_builder.h"

#include <unordered_set>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/utils/path.h"
#include "language/cpp_strategy.h"
#include "language/go_strategy.h"
#include "language/java_strategy.h"
#include "language/nodejs_strategy.h"
#include "language/python_strategy.h"

namespace functionsystem::runtime_manager {

namespace {
const std::string CPP_NEW_EXEC_PATH = "/cpp/bin/runtime";
const std::string GO_NEW_EXEC_PATH = "/go/bin/goruntime";
const std::string BASH_PATH = "/bin/bash";
const std::string GLOG_LOG_DIR = "GLOG_log_dir";
const std::string YR_LOG_LEVEL = "YR_LOG_LEVEL";
const std::string PATH_ENV = "PATH";
const std::string MAX_LOG_SIZE_MB_ENV = "YR_MAX_LOG_SIZE_MB";
const std::string MAX_LOG_FILE_NUM_ENV = "YR_MAX_LOG_FILE_NUM";
const std::string RUNTIME_DS_CONNECT_TIMEOUT_ENV = "DS_CONNECT_TIMEOUT_SEC";
const std::string PYTHON_PATH = "PYTHONPATH";
const std::string WHITE_LIST_ENV_PREFIX = "YR_";
const std::string ASCEND_RT_VISIBLE_DEVICES = "ASCEND_RT_VISIBLE_DEVICES";
const std::string YR_NOSET_ASCEND_RT_VISIBLE_DEVICES = "YR_NOSET_ASCEND_RT_VISIBLE_DEVICES";

// Environment keys to exclude from being passed to runtime processes
const std::vector<std::string> EXCLUDE_ENV_KEYS = {UNZIPPED_WORKING_DIR};

// System-level environment variables that should be stripped when starting runtime in container.
// These are host OS env vars that are irrelevant or potentially harmful inside containers.
const std::unordered_set<std::string> SYSTEM_ENV_BLACKLIST = {
    // OS / shell fundamentals
    "PATH", "HOME", "SHELL", "USER", "LOGNAME", "HOSTNAME", "LANG", "LANGUAGE",
    "LC_ALL", "LC_CTYPE", "LC_MESSAGES", "LC_COLLATE", "LC_NUMERIC", "LC_TIME",
    "TERM", "DISPLAY", "COLORTERM", "TERM_PROGRAM", "TERM_PROGRAM_VERSION",
    "SHLVL", "PWD", "OLDPWD", "TMPDIR", "EDITOR", "VISUAL", "PAGER", "MAIL",
    "XDG_RUNTIME_DIR", "XDG_SESSION_ID", "XDG_DATA_DIRS", "XDG_CONFIG_DIRS",
    "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME",
    "DBUS_SESSION_BUS_ADDRESS", "LS_COLORS",
    // SSH related
    "SSH_CLIENT", "SSH_CONNECTION", "SSH_TTY", "SSH_AUTH_SOCK",
    // Python related
    "PYTHONPATH",
    // Linker related
    "LD_LIBRARY_PATH", "LD_PRELOAD",
    // Go related
    "GOPATH", "GOROOT", "GOBIN", "GOCACHE", "GOMODCACHE", "GOENV",
    "GOFLAGS", "GONOPROXY", "GONOSUMDB", "GOPRIVATE", "GOPROXY", "GOSUMDB",
    "GOTMPDIR", "GOTOOLDIR", "GOTELEMETRY", "GOTELEMETRYDIR",
};

}  // namespace

// ── Construction ──────────────────────────────────────────────────────────────

CommandBuilder::CommandBuilder(bool execLookPath) : execLookPath_(execLookPath)
{
    // Register language strategies.
    // Multiple language tags may share a strategy instance (via shared_ptr).
    auto cpp     = std::make_shared<CppCommandStrategy>();
    auto go      = std::make_shared<GoCommandStrategy>();
    auto python  = std::make_shared<PythonCommandStrategy>(execLookPath);
    auto java    = std::make_shared<JavaCommandStrategy>(execLookPath);
    auto nodejs  = std::make_shared<NodejsCommandStrategy>(execLookPath);
    auto posix   = std::make_shared<PosixCustomCommandStrategy>();

    strategies_[CPP_LANGUAGE]          = cpp;
    strategies_[GO_LANGUAGE]           = go;
    strategies_[JAVA_LANGUAGE]         = java;
    strategies_[JAVA11_LANGUAGE]       = java;
    strategies_[JAVA17_LANGUAGE]       = java;
    strategies_[JAVA21_LANGUAGE]       = java;
    strategies_[NODE_JS]               = nodejs;
    strategies_[POSIX_CUSTOM_RUNTIME]  = posix;
    strategies_[PYTHON_LANGUAGE]       = python;
    strategies_[PYTHON3_LANGUAGE]      = python;
    strategies_[PYTHON36_LANGUAGE]     = python;
    strategies_[PYTHON37_LANGUAGE]     = python;
    strategies_[PYTHON38_LANGUAGE]     = python;
    strategies_[PYTHON39_LANGUAGE]     = python;
    strategies_[PYTHON310_LANGUAGE]    = python;
    strategies_[PYTHON311_LANGUAGE]    = python;
}

void CommandBuilder::RegisterStrategy(const std::string &languageTag,
                                       std::unique_ptr<LanguageCommandStrategy> strategy)
{
    strategies_[languageTag] = std::move(strategy);
}

// ── Core dispatch ─────────────────────────────────────────────────────────────

std::pair<Status, CommandArgs> CommandBuilder::BuildArgs(const std::string &language, const std::string &port,
                                                          const messages::StartInstanceRequest &request) const
{
    const auto &info = request.runtimeinstanceinfo();
    std::string tag = GetLanguageTag(language);

    auto it = strategies_.find(tag);
    if (it == strategies_.end()) {
        YRLOG_ERROR("{}|{}|CommandBuilder: unsupported language: {}", info.traceid(), info.requestid(), language);
        return {Status(StatusCode::PARAMETER_ERROR, "unsupported language: " + language), {}};
    }

    YRLOG_DEBUG("{}|{}|CommandBuilder::BuildArgs dispatching to strategy for lang: {}", info.traceid(),
                info.requestid(), tag);
    return it->second->BuildArgs(request, port, config_);
}

// ── Environment merging ───────────────────────────────────────────────────────

std::map<std::string, std::string> CommandBuilder::CombineEnvs(const Envs &envs) const
{
    // Layer 1 + 2: posix envs, then custom resource envs
    std::map<std::string, std::string> combined = envs.posixEnvs;
    combined.insert(envs.customResourceEnvs.begin(), envs.customResourceEnvs.end());

    // Layer 3: user envs override posix/custom; LD_LIBRARY_PATH is appended
    for (const auto &[key, val] : envs.userEnvs) {
        auto it = combined.find(key);
        if (it == combined.end()) {
            combined[key] = val;
            continue;
        }
        if (key == LD_LIBRARY_PATH) {
            combined[key] = it->second + ":" + val;
            continue;
        }
        combined[key] = val;
    }

    // Layer 4: framework envs — always override regardless of user settings
    combined[YR_LOG_LEVEL]                  = config_.runtimeLogLevel;
    combined[GLOG_LOG_DIR]                  = config_.runtimeLogPath;
    combined[MAX_LOG_SIZE_MB_ENV]           = std::to_string(config_.runtimeMaxLogSize);
    combined[MAX_LOG_FILE_NUM_ENV]          = std::to_string(config_.runtimeMaxLogFileNum);
    combined[RUNTIME_DS_CONNECT_TIMEOUT_ENV] = std::to_string(config_.runtimeDsConnectTimeout);

    // Append python working dir to PYTHONPATH if present
    std::string pythonPath;
    if (auto it = combined.find(PYTHON_PATH); it != combined.end()) {
        pythonPath = it->second;
    }
    if (auto it = combined.find(UNZIPPED_WORKING_DIR); it != combined.end() && !it->second.empty()) {
        if (!pythonPath.empty()) {
            pythonPath += ":";
        }
        pythonPath += it->second;
    }
    combined[PYTHON_PATH] = pythonPath;

    // Remove keys that must not be forwarded to the runtime process
    for (const auto &key : EXCLUDE_ENV_KEYS) {
        combined.erase(key);
    }

    // Layer 5: inherit host envs (YR_* whitelist, or all if inheritEnv=true)
    InheritEnv(combined);

    // Strip system-level environment variables that should not be passed to container runtime.
    for (const auto &key : SYSTEM_ENV_BLACKLIST) {
        combined.erase(key);
    }

    return combined;
}

// ── Exec path resolution ──────────────────────────────────────────────────────

std::string CommandBuilder::GetExecPathFromRuntimeConfig(const messages::RuntimeConfig &config) const
{
    const std::string &language = config.language();
    if (language == POSIX_CUSTOM_RUNTIME) {
        // Case 1: job entrypoint (UNZIPPED_WORKING_DIR present)
        auto workingDirIt = config.posixenvs().find(UNZIPPED_WORKING_DIR);
        if (workingDirIt != config.posixenvs().end() && !workingDirIt->second.empty()) {
            std::string entrypoint = config.entryfile();
            if (entrypoint.empty()) {
                YRLOG_ERROR("posix-custom: empty entrypoint");
                return "";
            }
            return entrypoint;
        }
        // Case 2: delegate bootstrap
        auto bootstrapIt = config.posixenvs().find(ENV_DELEGATE_BOOTSTRAP);
        auto downloadIt  = config.posixenvs().find(ENV_DELEGATE_DOWNLOAD);
        if (bootstrapIt != config.posixenvs().end() && downloadIt != config.posixenvs().end()) {
            return downloadIt->second + "/" + bootstrapIt->second;
        }
        return BASH_PATH;
    }
    return GetExecPath(language);
}

// ── Private helpers ───────────────────────────────────────────────────────────

std::string CommandBuilder::GetExecPath(const std::string &language) const
{
    std::string tag = GetLanguageTag(language);
    if (tag == CPP_LANGUAGE) {
        return config_.runtimePath + CPP_NEW_EXEC_PATH;
    }
    if (tag == GO_LANGUAGE) {
        return config_.runtimePath + GO_NEW_EXEC_PATH;
    }
    if (tag == POSIX_CUSTOM_RUNTIME) {
        return BASH_PATH;
    }

    std::string cmd = (tag == NODE_JS) ? NODE_JS_CMD : language;
    if (!execLookPath_) {
        return cmd;
    }
    auto path = LookPath(cmd);
    if (path.IsNone()) {
        YRLOG_ERROR("GetExecPath: LookPath failed for: {}", cmd);
        return "";
    }
    return path.Get();
}

std::string CommandBuilder::GetLanguageTag(const std::string &language) const
{
    // Match against registered strategies in insertion order
    for (const auto &[tag, _] : strategies_) {
        if (language.find(tag) != std::string::npos) {
            return tag;
        }
    }
    YRLOG_DEBUG("CommandBuilder::GetLanguageTag: no match for: {}", language);
    return language;
}

void CommandBuilder::InheritEnv(std::map<std::string, std::string> &envs) const
{
    char **env = environ;
    for (; *env; ++env) {
        std::string envStr = *env;
        auto eqPos = envStr.find('=');
        if (eqPos == std::string::npos) {
            continue;
        }
        std::string key = envStr.substr(0, eqPos);
        std::string val = envStr.substr(eqPos + 1);

        // Always forward YR_* variables that aren't already set
        if (litebus::strings::StartsWithPrefix(key, WHITE_LIST_ENV_PREFIX)) {
            if (envs.find(key) == envs.end()) {
                envs[key] = val;
            }
            continue;
        }

        if (config_.inheritEnv) {
            if (key == PATH_ENV) {
                // Append host PATH rather than override
                envs[key] = (envs[key].empty() ? "" : envs[key] + ":") + val;
                continue;
            }
            if (envs.find(key) == envs.end()) {
                envs[key] = val;
            }
        }
    }

    // If YR_NOSET_ASCEND_RT_VISIBLE_DEVICES is set, suppress GPU visibility env
    if (envs.count(YR_NOSET_ASCEND_RT_VISIBLE_DEVICES)) {
        envs.erase(ASCEND_RT_VISIBLE_DEVICES);
    }
}

}  // namespace functionsystem::runtime_manager
