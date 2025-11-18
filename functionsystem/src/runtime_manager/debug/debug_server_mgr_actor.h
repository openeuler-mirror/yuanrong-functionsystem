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

#ifndef FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_DEBUG_DEBUG_SERVER_ACTOR_H
#define FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_DEBUG_DEBUG_SERVER_ACTOR_H

#include <unordered_map>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/status/status.h"
#include "runtime_manager/config/flags.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::runtime_manager {
class DebugServerMgrActor : public litebus::ActorBase {
public:
    explicit DebugServerMgrActor(const std::string &name);
    ~DebugServerMgrActor() override = default;
    void SetConfig(const Flags &flags);
    virtual litebus::Future<Status> CreateServer(const std::string &runtimeID, const std::string &port,
                                                 const std::string &instanceID, const std::string &language);
    void AddRecord(const std::string &runtimeID, const pid_t &pid);
    virtual litebus::Future<Status> DestroyServer(const std::string &runtimeID);
    virtual litebus::Future<Status> DestroyAllServers();
    virtual litebus::Future<messages::QueryDebugInstanceInfosResponse> QueryDebugInstanceInfos(
        const std::string &requestID);

protected:
    void Init() override;
    void Finalize() override;

private:
    struct DebugServerConfig {
        std::string programNameOrInterpreter;  // e.g., "gdbserver", "python"
        std::string serverScriptPath;          // e.g., "/python/fnruntime/debug_server.py" (suffix for python)
        std::string resolvedExecutablePath;    // Full path to gdbserver or python interpreter
        std::string resolvedScriptPath;        // Full path to the python debug script
        bool isFound = false;
        // Generates command: (port, executable_path, script_path_if_any) -> command_string
        std::function<std::string(const std::string &, const std::string &, const std::string &)> commandGenerator;
        // Discovers paths for launguage debug server
        std::function<bool(const Flags *flagsOpt)> discoverer;
    };

    std::string logDir_;
    std::string hostIP_;
    // key: runtimeID
    std::unordered_map<std::string, std::string> runtime2DebugServerPort_;
    std::unordered_map<std::string, pid_t> runtime2PID_;
    std::unordered_map<std::string, pid_t> runtime2DebugServerPID_;
    std::unordered_map<std::string, std::string> runtime2instanceID_;
    std::unordered_map<std::string, std::string> runtime2Language_;
    // key: pid
    std::unordered_map<pid_t, std::string> pid2runtimeID_;
    std::unordered_map<pid_t, std::string> lastStatusMap_;
    std::vector<std::function<void()>> initHook_;
    // key: language ("cpp", "python")
    std::unordered_map<std::string, DebugServerConfig> languageDebugConfigs_;

    void MakeResponse(messages::QueryDebugInstanceInfosResponse &response, const pid_t &pid, const std::string &status);
};
}  // namespace functionsystem::runtime_manager
#endif  // FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_DEBUG_DEBUG_SERVER_ACTOR_H
