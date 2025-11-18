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

#include "debug_server_mgr_actor.h"

#include <async/asyncafter.hpp>

#include "common/utils/path.h"
#include "common/utils/files.h"
#include "common/utils/exec_utils.h"
#include "common/utils/proc_utils.h"
#include "exec/exec.hpp"

namespace functionsystem::runtime_manager {
const std::string LANG_CPP = "cpp";
const std::string LANG_PYTHON = "python";

const std::string GDB_SERVER_PROGRAM_NAME = "gdbserver";
const std::string PYTHON_DEBUG_SERVER_PATH_SUFFIX = "/python/fnruntime/debug_server.py";  // Relative to runtime path

enum class PROC_STAUS { RUNNING, SLEEPING, TRACE_STOPPED, SIGNAL_STOPPED };

const std::unordered_map<PROC_STAUS, std::string> PROC_STATUS_STR = { { PROC_STAUS::RUNNING, "R" },
                                                                      { PROC_STAUS::SLEEPING, "S" },
                                                                      { PROC_STAUS::TRACE_STOPPED, "t" },
                                                                      { PROC_STAUS::SIGNAL_STOPPED, "T" } };

DebugServerMgrActor::DebugServerMgrActor(const std::string &name) : ActorBase(name)
{
}

std::function<void()> SetDebugServerProcessPgid()
{
    return []() {
        pid_t pid = getpid();
        int pgidRet = setpgid(pid, 0);
        if (pgidRet < 0) {
            YRLOG_ERROR("failed to set pgid: {}, get errno: {}", pid, errno);
        }
    };
}

void DebugServerMgrActor::Init()
{
    YRLOG_INFO("Init DebugServerMgrActor.");

    // --- C++ Debug Server Configuration ---
    languageDebugConfigs_[LANG_CPP] = { GDB_SERVER_PROGRAM_NAME,  // programNameOrInterpreter
                                        "",                       // serverScriptPath (not applicable for C++)
                                        "",                       // resolvedExecutablePath (discovered by discoverer)
                                        "",                       // resolvedScriptPath (not applicable for C++)
                                        false,                    // isFound
                                        // commandGenerator for C++
                                        [](const std::string &port, const std::string &execPath, const std::string &) {
                                            return execPath + " --multi --debug :" + port;
                                        },
                                        // discoverer for C++
                                        [this](const Flags *) {  // Captures this, flagsOpt not used for C++
                                            auto &config = this->languageDebugConfigs_[LANG_CPP];
                                            auto path = LookPath(config.programNameOrInterpreter);
                                            if (path.IsNone()) {
                                                YRLOG_WARN("{} not found in path", config.programNameOrInterpreter);
                                                config.isFound = false;
                                                return false;
                                            }
                                            config.resolvedExecutablePath = path.Get();
                                            config.isFound = true;
                                            YRLOG_DEBUG("Found cpp debug server program ({}) in path: {}",
                                                        config.programNameOrInterpreter, config.resolvedExecutablePath);
                                            return true;
                                        } };
    // Attempt to discover C++ debugger at init
    languageDebugConfigs_[LANG_CPP].discoverer(nullptr);

    // --- Python Debug Server Configuration (discovery needs Flags from SetConfig) ---
    languageDebugConfigs_[LANG_PYTHON] = {
        "python",                         // programNameOrInterpreter (can be python3 or python)
        PYTHON_DEBUG_SERVER_PATH_SUFFIX,  // serverScriptPath (suffix)
        "",                               // resolvedExecutablePath (discovered by discoverer)
        "",                               // resolvedScriptPath (discovered by discoverer)
        false,                            // isFound
        // commandGenerator for Python
        [](const std::string &port, const std::string &interpreterPath, const std::string &scriptPath) {
            // Example: "python /path/to/debug_server.py --port 5555"
            return interpreterPath + " " + scriptPath + " --port " + port;
        },
        // discoverer for Python
        [this](const Flags *flagsOpt) {  // Captures this
            if (!flagsOpt) {
                YRLOG_WARN("Flags not available for Python debug server discovery.");
                this->languageDebugConfigs_[LANG_PYTHON].isFound = false;
                return false;
            }
            auto &config = this->languageDebugConfigs_[LANG_PYTHON];

            // 1. Find python interpreter (python3 then python)
            auto pythonInterpreter = LookPath("python3");
            if (pythonInterpreter.IsNone()) {
                pythonInterpreter = LookPath("python");
            }
            if (pythonInterpreter.IsNone()) {
                YRLOG_WARN("Python interpreter (python3 or python) not found in PATH for language {}", LANG_PYTHON);
                config.isFound = false;
                return false;
            }
            config.resolvedExecutablePath = pythonInterpreter.Get();
            // Store the actual interpreter found (python or python3) back into programNameOrInterpreter if needed,
            // or just use resolvedExecutablePath. For now, programNameOrInterpreter remains "python" as a general key.

            // 2. Construct and check python debug script path
            config.resolvedScriptPath = flagsOpt->GetRuntimePath() + config.serverScriptPath;
            if (!FileExists(config.resolvedScriptPath)) {
                YRLOG_WARN("Python debug server script {} not found for language {}", config.resolvedScriptPath,
                           LANG_PYTHON);
                config.isFound = false;  // Mark as not found if script is missing
                return false;
            }

            config.isFound = true;  // Both interpreter and script are found
            YRLOG_DEBUG("Found python interpreter: {} and debug server script: {} for language {}",
                        config.resolvedExecutablePath, config.resolvedScriptPath, LANG_PYTHON);
            return true;
        }
    };

    (void)initHook_.emplace_back(litebus::ChildInitHook::EXITWITHPARENT());
    (void)initHook_.emplace_back(SetDebugServerProcessPgid());
}

void DebugServerMgrActor::Finalize()
{
    DestroyAllServers();
}

void DebugServerMgrActor::SetConfig(const Flags &flags)
{
    YRLOG_DEBUG("Init DebugServerMgrActor config");
    auto path = litebus::os::Join(flags.GetRuntimeLogPath(), flags.GetRuntimeStdLogDir());
    if (!litebus::os::ExistPath(path)) {
        if (!litebus::os::Mkdir(path).IsNone()) {
            YRLOG_WARN("failed to make dir {}, msg: {}", path, litebus::os::Strerror(errno));
            return;
        }
    }
    char realPath[PATH_MAX] = { 0 };
    if (realpath(path.c_str(), realPath) == nullptr) {
        YRLOG_WARN("real path std log file {} failed, errno: {}, {}", path, errno, litebus::os::Strerror(errno));
        return;
    }
    logDir_ = std::string(realPath);
    hostIP_ = flags.GetHostIP();

    // Trigger Python debug server discovery now that flags are available
    if (languageDebugConfigs_.count(LANG_PYTHON)) {
        languageDebugConfigs_[LANG_PYTHON].discoverer(&flags);
    } else {
        YRLOG_ERROR("Python debug configuration not initialized in languageDebugConfigs_ map.");
    }
}

litebus::Future<Status> DebugServerMgrActor::CreateServer(const std::string &runtimeID, const std::string &port,
                                                          const std::string &instanceID, const std::string &language)
{
    const static std::unordered_map<std::string, std::string> GET_DEBUG_LANG = {
        { "cpp11", "cpp" },         { "cpp", "cpp" },          { "python", "python" },    { "python3", "python" },
        { "python3.6", "python" },  { "python3.7", "python" }, { "python3.8", "python" }, { "python3.9", "python" },
        { "python3.10", "python" }, { "python3.11", "python" }
    };
    auto langIt = languageDebugConfigs_.find(GET_DEBUG_LANG.at(language));
    if (langIt == languageDebugConfigs_.end()) {
        YRLOG_WARN("Unsupported language for debug server: {}", language);
        return Status(StatusCode::RUNTIME_MANAGER_PARAMS_INVALID, "Unsupported language: " + language);
    }

    DebugServerConfig &langConfig = langIt->second;

    // If not found during Init/SetConfig, try to discover again (e.g., gdbserver might be installed later)
    // For Python, discovery primarily happens in SetConfig with Flags.
    // This on-demand discovery is more relevant for C++ gdbserver.
    if (!langConfig.isFound) {
        YRLOG_DEBUG("Debug server for language {} not found initially, attempting on-demand discovery.", language);
        // For C++, discoverer doesn't need flags. For Python, it would need flags,
        // but python discovery should ideally succeed in SetConfig.
        // We pass nullptr for flags; C++ discoverer handles it. Python might fail if it strictly needs flags here.
        // However, python's isFound should be true if SetConfig ran successfully.
        langConfig.discoverer(nullptr);  // Attempt discovery
        if (!langConfig.isFound) {
            YRLOG_WARN("On-demand discovery failed. Debug server for language {} still not found.", language);
            return Status(StatusCode::RUNTIME_MANAGER_DEBUG_SERVER_NOTFOUND,
                          "Debug server components for language " + language + " not found.");
        }
    }

    // Debug server log
    auto outFile = litebus::os::Join(std::string(logDir_), runtimeID + "-" + language + "-debugserver.txt");
    if (!litebus::os::ExistPath(outFile) && TouchFile(outFile) != 0) {
        YRLOG_WARN("create debug server out log file {} failed", outFile);
        return Status(StatusCode::RUNTIME_MANAGER_PARAMS_INVALID, "create debug server out log file");
    }
    auto stdOut = litebus::ExecIO::CreateFileIO(outFile);

    std::string cmd =
        langConfig.commandGenerator(port, langConfig.resolvedExecutablePath, langConfig.resolvedScriptPath);
    if (cmd.empty()) {
        YRLOG_ERROR("Failed to generate command for language {} with path {} and script {}", language,
                    langConfig.resolvedExecutablePath, langConfig.resolvedScriptPath);
        return Status(StatusCode::RUNTIME_MANAGER_PARAMS_INVALID, "Failed to generate debug command for " + language);
    }

    std::shared_ptr<litebus::Exec> execPtr =
        litebus::Exec::CreateExec(cmd, {}, litebus::ExecIO::CreatePipeIO(), stdOut, stdOut, initHook_, {}, false);

    if (execPtr == nullptr || execPtr->GetPid() == -1) {
        YRLOG_ERROR("failed to start debug server for language {}, runtimeID({}), errno({}), errorMsg({})", language,
                    runtimeID, errno, litebus::os::Strerror(errno));
        return Status(StatusCode::RUNTIME_MANAGER_PARAMS_INVALID, "failed to start debug server for " + language);
    }

    runtime2DebugServerPort_[runtimeID] = port;
    runtime2DebugServerPID_[runtimeID] = execPtr->GetPid();
    runtime2instanceID_[runtimeID] = instanceID;
    runtime2Language_[runtimeID] = language;

    YRLOG_INFO("Started {} debug server for runtime({}) (pid {}), execute final cmd: \"{}\", debug server log: {}",
               language, runtimeID, execPtr->GetPid(), cmd, outFile);
    return Status::OK();
}

void DebugServerMgrActor::AddRecord(const std::string &runtimeID, const pid_t &pid)
{
    runtime2PID_[runtimeID] = pid;
    pid2runtimeID_[pid] = runtimeID;
}

litebus::Future<Status> DebugServerMgrActor::DestroyServer(const std::string &runtimeID)
{
    auto pidIt = runtime2DebugServerPID_.find(runtimeID);
    if (pidIt == runtime2DebugServerPID_.end()) {
        YRLOG_WARN("Debug server of runtime({}) not found in active map", runtimeID);
        return Status(StatusCode::RUNTIME_MANAGER_DEBUG_SERVER_NOTFOUND,
                      "Debug server of runtime(" + runtimeID + ") not found");
    }

    pid_t serverPID = pidIt->second;

    YRLOG_INFO("Stopping debug server for runtime({}) (pid {})...", runtimeID, serverPID);
    auto ret = kill(serverPID, SIGINT);  // Send SIGINT for graceful shutdown
    if (ret != 0) {
        // ESRCH means No such process, it might have already exited.
        if (errno != ESRCH) {
            YRLOG_WARN("Failed to send SIGINT to debug server pid {}: {}, errno: {}", serverPID,
                       litebus::os::Strerror(errno), errno);
        } else {
            YRLOG_DEBUG("Debug server pid {} already exited.", serverPID);
        }
    } else {
        YRLOG_DEBUG("SIGINT sent to debug server pid {} for runtime {}", serverPID, runtimeID);
    }

    // Clean up records
    runtime2DebugServerPID_.erase(runtimeID);
    if (runtime2PID_.count(runtimeID)) {
        pid_t appPid = runtime2PID_[runtimeID];
        pid2runtimeID_.erase(appPid);
        runtime2PID_.erase(runtimeID);
    }
    runtime2DebugServerPort_.erase(runtimeID);
    runtime2instanceID_.erase(runtimeID);
    runtime2Language_.erase(runtimeID);
    return Status::OK();
}

litebus::Future<Status> DebugServerMgrActor::DestroyAllServers()
{
    YRLOG_DEBUG("Destroying all active debug servers...");
    // Create a copy of keys to iterate over, as DestroyServer modifies the map
    std::vector<std::string> runtimeIDsToDestroy;
    for (const auto &pair : runtime2DebugServerPID_) {
        runtimeIDsToDestroy.push_back(pair.first);
    }

    for (const auto &runtimeID : runtimeIDsToDestroy) {
        DestroyServer(runtimeID);  // This will log and handle individual server destruction
    }

    // Ensure all maps are clear, though DestroyServer should handle its entries.
    // This is a safeguard.
    pid2runtimeID_.clear();
    runtime2DebugServerPID_.clear();
    runtime2PID_.clear();
    runtime2DebugServerPort_.clear();
    runtime2instanceID_.clear();
    runtime2Language_.clear();

    YRLOG_DEBUG("All debug servers scheduled for destruction.");
    return Status::OK();
}

void DebugServerMgrActor::MakeResponse(messages::QueryDebugInstanceInfosResponse &response, const pid_t &pid,
                                       const std::string &status)
{
    auto *info = response.add_debuginstanceinfos();
    info->set_pid(pid);
    info->set_status(status);

    if (auto rit = pid2runtimeID_.find(pid); rit != pid2runtimeID_.end()) {
        const auto &runtime = rit->second;
        if (runtime2instanceID_.count(runtime)) {  // Check if instanceID exists
            info->set_instanceid(runtime2instanceID_[runtime]);
        }
        if (runtime2DebugServerPort_.count(runtime)) {  // Check if port exists
            info->set_debugserver(hostIP_ + ":" + runtime2DebugServerPort_[runtime]);
        }
        if (runtime2Language_.count(runtime)) {
            info->set_language(runtime2Language_[runtime]);
        }
    }
}

litebus::Future<messages::QueryDebugInstanceInfosResponse> DebugServerMgrActor::QueryDebugInstanceInfos(
    const std::string &requestID)
{
    messages::QueryDebugInstanceInfosResponse response;
    response.set_requestid(requestID);

    // Collect current active PIDs
    std::unordered_set<pid_t> currentPIDs;
    for (const auto &[runtime, pid] : runtime2PID_) {
        std::ignore = runtime;
        currentPIDs.insert(pid);
    }

    // Batch query process statuses
    auto currentStatus = QueryProcStatus(currentPIDs);

    // Calculate delta changes
    std::unordered_set<pid_t> lastPIDs;
    for (const auto &[pid, lastStatus] : lastStatusMap_) {
        std::ignore = lastStatus;
        lastPIDs.insert(pid);
    }
    std::vector<pid_t> changedPIDs;
    for (const auto &[pid, status] : currentStatus) {
        if (auto it = lastStatusMap_.find(pid); it != lastStatusMap_.end() && it->second != status) {
            changedPIDs.push_back(pid);
        }
    }

    // Process added/changed PIDs
    for (const auto &[pid, status] : currentStatus) {
        if (lastPIDs.count(pid) == 0 || std::find(changedPIDs.begin(), changedPIDs.end(), pid) != changedPIDs.end()) {
            MakeResponse(response, pid, status);
        }
    }

    // not mark removed PIDs
    // Update status records
    lastStatusMap_ = std::move(currentStatus);
    for (auto it = lastStatusMap_.begin(); it != lastStatusMap_.end();) {
        if (currentPIDs.count(it->first) == 0) {
            it = lastStatusMap_.erase(it);  // Erase PIDs that are no longer active
        } else {
            ++it;
        }
    }

    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    return response;
}

}  // namespace functionsystem::runtime_manager
