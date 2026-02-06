/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_H
#define FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <map>

#include "exec/exec.hpp"
#include "common/status/status.h"

namespace functionsystem {

class ExecSession : public std::enable_shared_from_this<ExecSession> {
public:
    // Output callback type: data is the output content, isStderr indicates if it's from stderr
    using OutputCallback = std::function<void(const std::string& data, bool isStderr)>;

    // Exit callback type: exitCode is the process exit code
    using ExitCallback = std::function<void(int exitCode)>;

    // Creation parameters
    struct CreateParams {
        std::string containerId;
        std::vector<std::string> command;  // default {"/bin/sh"}
        bool tty = false;
        int rows = 24;
        int cols = 80;
        std::map<std::string, std::string> env;
    };

    // Factory method to create a session
    static std::shared_ptr<ExecSession> Create(const CreateParams& params);

    // Disable copy
    ExecSession(const ExecSession&) = delete;
    ExecSession& operator=(const ExecSession&) = delete;

    ~ExecSession();

    // Start the session (create docker exec process)
    Status Start();

    // Write input data (thread safe)
    Status WriteInput(const std::string& data);

    // Resize window (only effective in TTY mode, thread safe)
    Status Resize(int rows, int cols);

    // Close the session (thread safe)
    void Close();

    // Register output callback
    void OnOutput(OutputCallback cb);

    // Register exit callback
    void OnExit(ExitCallback cb);

    // Get session ID
    const std::string& GetSessionId() const { return sessionId_; }

    // Check if running
    bool IsRunning() const { return running_.load(); }

private:
    explicit ExecSession(const CreateParams& params);

    // Start async output reader threads
    void StartOutputReader();

    // Register async exit handler (uses Future.OnComplete)
    void RegisterExitHandler();

    // Read loop (runs in a separate thread)
    void ReadLoop(int fd, bool isStderr);

    // Generate session ID
    static std::string GenerateSessionId();

private:
    // Session parameters
    std::string sessionId_;
    std::string containerId_;
    std::vector<std::string> command_;
    bool tty_;
    int rows_;
    int cols_;
    std::map<std::string, std::string> env_;

    // Process related
    std::shared_ptr<litebus::Exec> exec_;
    std::unique_ptr<litebus::PtyExecIO> ptyIO_;  // PTY IO (TTY mode only)
    int ptyMasterFd_ = -1;  // PTY master fd (for resize, TTY mode only)

    // Threads
    std::thread stdoutReader_;
    std::thread stderrReader_;

    // State flags
    std::atomic<bool> running_{false};
    std::atomic<bool> closed_{false};

    // Synchronization primitives
    std::mutex ioMutex_;        // Protect WriteInput/Close
    std::mutex callbackMutex_;  // Protect callbacks

    // Callback functions
    OutputCallback outputCallback_;
    ExitCallback exitCallback_;
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_H
