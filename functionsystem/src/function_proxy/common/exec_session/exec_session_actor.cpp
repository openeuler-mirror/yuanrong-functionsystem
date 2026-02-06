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

#include "exec_session_actor.h"
#include "io_event_actor.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <random>
#include <sstream>

#include "async/async.hpp"
#include "common/logs/logging.h"
#include "exec/exec.hpp"

namespace functionsystem {

static const int WRITE_BUFFER_SIZE = 4096;

std::string ExecSessionActor::GenerateSessionId()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << dis(gen);
    return ss.str();
}

std::shared_ptr<ExecSessionActor> ExecSessionActor::Create(const CreateParams& params)
{
    return std::make_shared<ExecSessionActor>(params);
}

ExecSessionActor::ExecSessionActor(const CreateParams& params)
    : litebus::ActorBase("ExecSessionActor-" + GenerateSessionId()),
      streamWriter_(params.writer)
{
    sessionId_ = GenerateSessionId();
    YRLOG_INFO("ExecSessionActor created, sessionId: {}", sessionId_);
}

ExecSessionActor::~ExecSessionActor()
{
    Close();
    YRLOG_INFO("ExecSessionActor destroyed, sessionId: {}", sessionId_);
}

void ExecSessionActor::Init()
{
    YRLOG_INFO("ExecSessionActor::Init, sessionId: {}", sessionId_);

    // No message handlers needed - all communication uses Async

    YRLOG_INFO("ExecSessionActor initialized, sessionId: {}", sessionId_);
}

void ExecSessionActor::Finalize()
{
    YRLOG_INFO("ExecSessionActor::Finalize, sessionId: {}", sessionId_);
    Cleanup();
}

// DoOutput - handle output data from IOEventActor via Async
void ExecSessionActor::DoOutput(const std::string& data, int exitCode)
{
    if (exitCode >= 0) {
        // EOF/Exit
        YRLOG_INFO("Process EOF/Exit detected, sessionId: {}, exitCode: {}", sessionId_, exitCode);
        running_ = false;
        WriteToStream("", exitCode);
        Cleanup();
    } else {
        // Normal output data
        YRLOG_DEBUG("Received output data, sessionId: {}, size: {}", sessionId_, data.size());
        WriteToStream(data, -1);  // -1 means normal data
    }
}

// Do* methods - actual implementation with normal function signatures
void ExecSessionActor::DoStart(const std::string& containerId,
                               const std::vector<std::string>& command,
                               const std::map<std::string, std::string>& env,
                               bool tty, int rows, int cols)
{
    YRLOG_INFO("ExecSessionActor::DoStart, sessionId: {}, containerId: {}, command: [{}], env: {}, tty: {}, rows: {}, cols: {}",
               sessionId_, containerId,
               command.empty() ? std::string("/bin/sh") : command[0],
               env.size(),
               tty, rows, cols);

    if (running_.load()) {
        YRLOG_WARN("Session already started, sessionId: {}", sessionId_);
        return;
    }

    // Parse parameters
    containerId_ = containerId;
    command_ = command.empty() ? std::vector<std::string>{"/bin/sh"} : command;
    tty_ = tty;
    rows_ = rows;
    cols_ = cols;

    // Build docker exec command
    std::vector<std::string> argv = {"docker", "exec", "-i"};

    // Add environment variables
    for (const auto& [key, value] : env) {
        argv.push_back("-e");
        argv.push_back(key + "=" + value);
    }

    if (tty_) {
        argv.push_back("-t");
    }
    argv.push_back(containerId_);
    argv.insert(argv.end(), command_.begin(), command_.end());

    YRLOG_INFO("Starting docker exec, sessionId: {}, command: docker exec -i{} {} {}",
               sessionId_, tty_ ? "t" : "", containerId_, command_[0]);

    if (tty_) {
        // Use PTY IO
        auto ptyResult = litebus::PtyExecIO::Create(rows_, cols_);
        if (ptyResult.IsError()) {
            YRLOG_ERROR("Failed to create PTY, sessionId: {}", sessionId_);
            WriteToStream("", 1);  // Send error
            return;
        }

        auto pty = std::move(ptyResult).Get();
        ptyIO_ = std::make_unique<litebus::PtyExecIO>(std::move(pty));
        ptyMasterFd_ = ptyIO_->masterFd;

        // Prepare child init hooks for TTY mode
        int slaveFd = ptyIO_->slaveFd;
        std::vector<std::function<void()>> childInitHooks = {
            litebus::ChildInitHook::EXITWITHPARENT(),
            [slaveFd]() {
                if (setsid() == -1) {
                    return;
                }
                ioctl(slaveFd, TIOCSCTTY, 0);
            }
        };

        exec_ = litebus::Exec::CreateExec(
            "docker", argv, litebus::None(),
            *ptyIO_->stdIn, *ptyIO_->stdOut, *ptyIO_->stdErr,
            childInitHooks,
            {}, true
        );

        YRLOG_INFO("ExecSessionActor started with PTY, sessionId: {}, masterFd: {}",
                   sessionId_, ptyMasterFd_);

    } else {
        // Use normal Pipe IO
        exec_ = litebus::Exec::CreateExec(
            "docker", argv, litebus::None(),
            litebus::ExecIO::CreatePipeIO(),
            litebus::ExecIO::CreatePipeIO(),
            litebus::ExecIO::CreatePipeIO(),
            {litebus::ChildInitHook::EXITWITHPARENT()},
            {}, true
        );

        YRLOG_INFO("ExecSessionActor started with Pipe, sessionId: {}", sessionId_);
    }

    if (!exec_) {
        YRLOG_ERROR("Failed to create exec process, sessionId: {}", sessionId_);
        WriteToStream("", 1);  // Send error
        return;
    }

    YRLOG_INFO("Exec process created, sessionId: {}, pid: {}", sessionId_, exec_->GetPid());

    running_ = true;

    // Get stdin/stdout fd
    if (tty_ && ptyIO_) {
        stdinFd_ = ptyIO_->masterFd;
        stdoutFd_ = ptyIO_->masterFd;  // PTY uses same fd for input/output
    } else {
        if (exec_->GetIn().IsSome()) {
            stdinFd_ = exec_->GetIn().Get();
        }
        if (exec_->GetOut().IsSome()) {
            stdoutFd_ = exec_->GetOut().Get();
        }
    }

    // Register stdout fd with IOEventActor using Async
    if (stdoutFd_ >= 0) {
        YRLOG_INFO("Registering stdout fd {} with IOEventActor, sessionId: {}", stdoutFd_, sessionId_);

        // Capture weak_ptr to avoid circular reference
        std::weak_ptr<ExecSessionActor> weakSelf = std::static_pointer_cast<ExecSessionActor>(shared_from_this());

        // Register with callback - callback will be invoked in IOEventActor context
        // and will use Async to call DoOutput in ExecSessionActor context
        litebus::Async(IOEventActor::GetInstance(), &IOEventActor::DoRegister,
                       stdoutFd_,
                       [weakSelf](const std::string& data, int exitCode) {
                           auto self = weakSelf.lock();
                           if (!self) {
                               YRLOG_WARN("ExecSessionActor already destroyed, ignoring IO callback");
                               return;
                           }
                           // Call DoOutput via Async to execute in ExecSessionActor context
                           litebus::Async(self->GetAID(), &ExecSessionActor::DoOutput, data, exitCode);
                       });

    } else {
        YRLOG_WARN("stdout fd not available, sessionId: {}", sessionId_);
    }

    // Register exit handler
    RegisterExitHandler();

    // Send success response
    WriteToStream("", -1);  // -1 means success (no data)
}

void ExecSessionActor::DoInput(const std::string& data)
{
    if (closed_.load() || !running_.load()) {
        YRLOG_WARN("Session is closed, ignoring input, sessionId: {}", sessionId_);
        return;
    }

    if (stdinFd_ < 0) {
        YRLOG_ERROR("stdin fd not available, sessionId: {}", sessionId_);
        return;
    }

    YRLOG_DEBUG("Writing input data, size: {}, sessionId: {}", data.size(), sessionId_);

    ssize_t written = write(stdinFd_, data.c_str(), data.size());
    if (written < 0) {
        YRLOG_ERROR("Write failed, sessionId: {}, errno: {}", sessionId_, errno);
        return;
    }

    if (static_cast<size_t>(written) != data.size()) {
        YRLOG_WARN("Partial write, sessionId: {}, written: {}, total: {}",
                   sessionId_, written, data.size());
    }
}

void ExecSessionActor::DoResize(int rows, int cols)
{
    if (!tty_) {
        YRLOG_WARN("Resize only available in TTY mode, sessionId: {}", sessionId_);
        return;
    }

    if (!ptyIO_ || ptyMasterFd_ < 0) {
        YRLOG_ERROR("PTY not available, sessionId: {}", sessionId_);
        return;
    }

    if (ptyIO_->Resize(rows, cols) != 0) {
        YRLOG_ERROR("Failed to resize PTY, sessionId: {}", sessionId_);
        return;
    }

    rows_ = rows;
    cols_ = cols;
    YRLOG_INFO("Window resized, sessionId: {}, rows: {}, cols: {}", sessionId_, rows_, cols_);
}

void ExecSessionActor::DoClose()
{
    YRLOG_INFO("Closing ExecSessionActor, sessionId: {}", sessionId_);
    Close();
}

void ExecSessionActor::WriteToStream(const std::string& data, int exitCode)
{
    // In Actor, this is called serially, no lock needed
    if (streamWriter_) {
        streamWriter_(data, exitCode);
    } else {
        YRLOG_WARN("Stream writer is null, sessionId: {}", sessionId_);
    }
}

void ExecSessionActor::RegisterExitHandler()
{
    YRLOG_INFO("Registering exit handler, sessionId: {}", sessionId_);

    auto future = exec_->GetStatus();

    // Use weak_ptr to prevent circular reference
    std::weak_ptr<ExecSessionActor> weakSelf = std::static_pointer_cast<ExecSessionActor>(shared_from_this());

    future.OnComplete([this, weakSelf](const litebus::Future<litebus::Option<int>>& f) {
        auto self = weakSelf.lock();
        if (!self) {
            YRLOG_WARN("ExecSessionActor already destroyed, ignoring exit callback");
            return;
        }

        if (!running_.load()) {
            YRLOG_INFO("Session already stopped, ignoring exit callback, sessionId: {}", sessionId_);
            return;
        }

        YRLOG_INFO("Process exit detected (async callback), sessionId: {}", sessionId_);

        running_ = false;

        // Get exit code from Future
        int exitCode = 0;
        if (!f.IsError() && f.Get().IsSome()) {
            exitCode = f.Get().Get();
        }

        YRLOG_INFO("Process exited, sessionId: {}, exitCode: {}", sessionId_, exitCode);

        // Send exit notification
        WriteToStream("", exitCode);
    });

    YRLOG_INFO("Exit handler registered successfully, sessionId: {}", sessionId_);
}

void ExecSessionActor::Cleanup()
{
    YRLOG_INFO("Cleaning up ExecSessionActor, sessionId: {}", sessionId_);

    running_ = false;

    // Unregister fd from IOEventActor using Async
    if (stdoutFd_ >= 0) {
        YRLOG_INFO("Unregistering stdout fd {} from IOEventActor, sessionId: {}", stdoutFd_, sessionId_);
        litebus::Async(IOEventActor::GetInstance(), &IOEventActor::DoUnregister, stdoutFd_);
        stdoutFd_ = -1;
    }

    // Close PTY
    if (ptyIO_) {
        ptyIO_->Close();
        ptyIO_.reset();
    }

    // Give process some time to exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // If process still running, send SIGTERM
    if (exec_ && exec_->GetPid() > 0) {
        YRLOG_INFO("Sending SIGTERM to process, pid: {}, sessionId: {}", exec_->GetPid(), sessionId_);
        kill(exec_->GetPid(), SIGTERM);
    }

    YRLOG_INFO("ExecSessionActor cleanup completed, sessionId: {}", sessionId_);
}

void ExecSessionActor::Close()
{
    if (closed_.exchange(true)) {
        return;  // Already closed
    }

    YRLOG_INFO("Closing ExecSessionActor, sessionId: {}", sessionId_);

    Cleanup();
}

}  // namespace functionsystem
