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

#ifndef FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_EXEC_SESSION_ACTOR_H
#define FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_EXEC_SESSION_ACTOR_H

#include <actor/actor.hpp>
#include <atomic>
#include <exec/exec.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace functionsystem {

// ExecSessionActor: Actor-based session for managing docker exec processes
// Replaces the multi-threaded ExecSession with message-driven architecture
class ExecSessionActor : public litebus::ActorBase, public std::enable_shared_from_this<ExecSessionActor> {
public:
    // Stream writer callback (for gRPC stream output)
    using StreamWriter = std::function<void(const std::string &data, int exitCode)>;

    // Creation parameters
    struct CreateParams {
        StreamWriter writer = nullptr;  // Callback for writing to gRPC stream (can be set later)
        std::string sessionId;          // Optional: use external session_id; empty = generate internally
    };

    // Factory method to create a session actor
    static std::shared_ptr<ExecSessionActor> Create(const CreateParams &params);

    ~ExecSessionActor() override;

    explicit ExecSessionActor(const CreateParams &params);

    // Disable copy
    ExecSessionActor(const ExecSessionActor &) = delete;
    ExecSessionActor &operator=(const ExecSessionActor &) = delete;

    // Public methods for Async calls
    void DoStart(const std::string &containerId, const std::vector<std::string> &command,
                 const std::map<std::string, std::string> &env, bool tty, int rows, int cols);
    void DoInput(const std::string &data);
    void DoOutput(const std::string &data, int exitCode);  // exitCode: -1=normal, >=0=exit
    void DoResize(int rows, int cols);
    void DoClose();

    // Get session ID
    const std::string& GetSessionId() const { return sessionId_; }

    // Set writer callback
    void SetWriter(const StreamWriter& writer) { streamWriter_ = writer; }

    // Generate unique session ID (for callers that need to pre-generate before Create)
    static std::string GenerateSessionId();

protected:
    void Init() override;
    void Finalize() override;

private:
    // Helper methods
    void WriteToStream(const std::string &data, int exitCode);
    void RegisterExitHandler();
    void OnProcessExit(const litebus::Future<litebus::Option<int>> &future);
    void Cleanup();
    void DoCleanupAfterUnregister();  // Called after fd unregistered; closes pty/fds
    void Close();

    // Session parameters
    StreamWriter streamWriter_;

    // Process related
    std::shared_ptr<litebus::Exec> exec_;
    std::unique_ptr<litebus::PtyExecIO> ptyIO_;
    int stdinFd_{ -1 };
    int stdoutFd_{ -1 };
    int stderrFd_{ -1 };   // non-TTY only; TTY merges stderr to stdout
    int ptyMasterFd_{ -1 };

    // State
    std::string sessionId_;
    std::string containerId_;
    std::vector<std::string> command_;
    bool tty_{ false };
    int rows_{ 24 };
    int cols_{ 80 };
    std::map<std::string, std::string> env_;

    std::atomic<bool> running_{ false };
    std::atomic<bool> closed_{ false };
};

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_FUNCTION_PROXY_EXEC_SESSION_EXEC_SESSION_ACTOR_H
