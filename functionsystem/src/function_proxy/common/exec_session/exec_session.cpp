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

#include "exec_session.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <random>
#include <sstream>

#include "common/logs/logging.h"

namespace functionsystem {

static const int READ_BUFFER_SIZE = 4096;
static const int SELECT_TIMEOUT_MS = 100;

std::string ExecSession::GenerateSessionId()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << dis(gen);
    return ss.str();
}

std::shared_ptr<ExecSession> ExecSession::Create(const CreateParams& params)
{
    return std::shared_ptr<ExecSession>(new ExecSession(params));
}

ExecSession::ExecSession(const CreateParams& params)
    : sessionId_(GenerateSessionId()),
      containerId_(params.containerId),
      command_(params.command),
      tty_(params.tty),
      rows_(params.rows),
      cols_(params.cols),
      env_(params.env)
{
    if (command_.empty()) {
        command_ = {"/bin/sh"};
    }
    YRLOG_INFO("ExecSession created, sessionId: {}, containerId: {}, tty: {}",
               sessionId_, containerId_, tty_);
}

ExecSession::~ExecSession()
{
    Close();
    YRLOG_INFO("ExecSession destroyed, sessionId: {}", sessionId_);
}

Status ExecSession::Start()
{
    if (running_.load()) {
        return Status(StatusCode::FAILED, "Session already started");
    }

    // Build docker exec command
    std::vector<std::string> argv = {"docker", "exec", "-i"};
    if (tty_) {
        argv.push_back("-t");
    }
    argv.push_back(containerId_);
    argv.insert(argv.end(), command_.begin(), command_.end());

    YRLOG_INFO("Starting docker exec, sessionId: {}, command: docker exec -i{} {} {}",
               sessionId_, tty_ ? "t" : "", containerId_,
               command_.empty() ? "" : command_[0]);

    if (tty_) {
        // Use PTY IO
        auto ptyResult = litebus::PtyExecIO::Create(rows_, cols_);
        if (ptyResult.IsError()) {
            YRLOG_ERROR("Failed to create PTY, sessionId: {}", sessionId_);
            return Status(StatusCode::FAILED, "Failed to create PTY");
        }

        // Move the PtyExecIO object to avoid copy
        auto pty = std::move(ptyResult).Get();
        std::cout << "[DEBUG] After ptyResult.Get(), pty.masterFd=" << pty.masterFd << ", pty.slaveFd=" << pty.slaveFd << std::endl;
        std::cout << "[DEBUG] pty.stdIn=" << pty.stdIn.get() << ", pty.stdOut=" << pty.stdOut.get() << ", pty.stdErr=" << pty.stdErr.get() << std::endl;

        ptyIO_ = std::make_unique<litebus::PtyExecIO>(std::move(pty));
        ptyMasterFd_ = ptyIO_->masterFd;

        std::cout << "[DEBUG] After make_unique, ptyIO_->masterFd=" << ptyIO_->masterFd << ", ptyIO_->stdIn=" << ptyIO_->stdIn.get() << std::endl;

        std::cout << "[DEBUG] Before CreateExec, *ptyIO_->stdIn=" << ptyIO_->stdIn.get() << ", *ptyIO_->stdOut=" << ptyIO_->stdOut.get() << std::endl;

        // Prepare child init hooks for TTY mode
        int slaveFd = ptyIO_->slaveFd;
        std::vector<std::function<void()>> childInitHooks = {
            litebus::ChildInitHook::EXITWITHPARENT(),
            [slaveFd]() {
                // Set controlling terminal for TTY mode
                // This is required for proper TTY functionality
                if (setsid() == -1) {
                    // Failed to create new session, but continue anyway
                    return;
                }
                // Open slave fd to set it as controlling terminal
                // TIOCSCTTY makes the slave the controlling terminal of the calling process
                ioctl(slaveFd, TIOCSCTTY, 0);
            }
        };

        exec_ = litebus::Exec::CreateExec(
            "docker", argv, litebus::None(),
            *ptyIO_->stdIn, *ptyIO_->stdOut, *ptyIO_->stdErr,
            childInitHooks,
            {}, true
        );

        YRLOG_INFO("ExecSession started with PTY, sessionId: {}, masterFd: {}",
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
            YRLOG_INFO("ExecSession started with Pipe, sessionId: {}", sessionId_);
    }

    if (!exec_) {
        YRLOG_ERROR("Failed to create exec process, sessionId: {}", sessionId_);
        return Status(StatusCode::FAILED, "Failed to create exec process");
    }

    YRLOG_INFO("Exec process created, sessionId: {}, pid: {}", sessionId_, exec_->GetPid());

    running_ = true;

    // Start output reader threads
    YRLOG_INFO("Starting output reader threads...");
    StartOutputReader();
    YRLOG_INFO("Output reader threads started");

    // Register async exit handler (event-driven, no polling thread needed)
    RegisterExitHandler();

    return Status::OK();
}

void ExecSession::StartOutputReader()
{
    YRLOG_INFO("StartOutputReader: sessionId: {}, tty: {}", sessionId_, tty_);
    if (tty_ && ptyIO_) {
        // PTY mode: read from master fd
        int readFd = ptyIO_->masterFd;
        YRLOG_INFO("PTY mode: reading from master fd: {}", readFd);

        // PTY mode only needs one reader thread (stdout and stderr merged)
        // Note: ptyIO_->stdErr points to same object as stdOut
        stdoutReader_ = std::thread([this, readFd, self = shared_from_this()]() {
            YRLOG_INFO("PTY reader thread started, sessionId: {}, fd: {}", sessionId_, readFd);
            ReadLoop(readFd, false);
            YRLOG_INFO("PTY reader thread exited, sessionId: {}", sessionId_);
        });
    } else {
        // Pipe mode: read stdout and stderr separately
        YRLOG_INFO("Pipe mode: creating reader threads");
        if (exec_->GetOut().IsSome()) {
            int stdoutFd = exec_->GetOut().Get();
            YRLOG_INFO("Starting stdout reader thread, fd: {}", stdoutFd);
            stdoutReader_ = std::thread([this, self = shared_from_this()]() {
                YRLOG_INFO("stdout reader thread started, sessionId: {}", sessionId_);
                ReadLoop(exec_->GetOut().Get(), false);
                YRLOG_INFO("stdout reader thread exited, sessionId: {}", sessionId_);
            });
        } else {
            YRLOG_WARN("stdout fd is not available!");
        }

        if (exec_->GetErr().IsSome()) {
            int stderrFd = exec_->GetErr().Get();
            YRLOG_INFO("Starting stderr reader thread, fd: {}", stderrFd);
            stderrReader_ = std::thread([this, self = shared_from_this()]() {
                YRLOG_INFO("stderr reader thread started, sessionId: {}", sessionId_);
                ReadLoop(exec_->GetErr().Get(), true);
                YRLOG_INFO("stderr reader thread exited, sessionId: {}", sessionId_);
            });
        } else {
            YRLOG_WARN("stderr fd is not available!");
        }
    }

    YRLOG_INFO("StartOutputReader completed, sessionId: {}", sessionId_);
}

void ExecSession::ReadLoop(int fd, bool isStderr)
{
    char buffer[READ_BUFFER_SIZE];

    while (running_.load()) {
        // Use select for non-blocking wait
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd, &readSet);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = SELECT_TIMEOUT_MS * 1000;

        int ret = select(fd + 1, &readSet, nullptr, nullptr, &timeout);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            YRLOG_ERROR("select error, sessionId: {}, fd: {}, errno: {}",
                       sessionId_, fd, errno);
            break;
        }

        if (ret == 0) {
            // Timeout, continue checking running_ status
            continue;
        }

        // Data available to read
        ssize_t bytesRead = read(fd, buffer, READ_BUFFER_SIZE);
        if (bytesRead < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            YRLOG_ERROR("read error, sessionId: {}, fd: {}, errno: {}",
                       sessionId_, fd, errno);
            break;
        }

        if (bytesRead == 0) {
            // EOF, pipe closed
            YRLOG_INFO("EOF on fd, sessionId: {}, fd: {}, isStderr: {}",
                      sessionId_, fd, isStderr);
            break;
        }

        YRLOG_DEBUG("Read {} bytes from fd {}, sessionId: {}", bytesRead, fd, sessionId_);

        // Call output callback
        std::string data(buffer, bytesRead);
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            if (outputCallback_) {
                YRLOG_DEBUG("Calling output callback, size: {}, isStderr: {}", data.size(), isStderr);
                outputCallback_(data, isStderr);
                YRLOG_DEBUG("Output callback returned");
            } else {
                YRLOG_WARN("Output callback is null!");
            }
        }
    }
    YRLOG_INFO("ReadLoop exiting, sessionId: {}, fd: {}", sessionId_, fd);
}

void ExecSession::RegisterExitHandler()
{
    YRLOG_INFO("RegisterExitHandler: registering async exit handler, sessionId: {}", sessionId_);

    auto future = exec_->GetStatus();

    // Use weak_ptr to prevent circular reference
    // Future holds callback, callback captures ExecSession
    std::weak_ptr<ExecSession> weakSelf = shared_from_this();

    future.OnComplete([this, weakSelf](const litebus::Future<litebus::Option<int>>& f) {
        // Try to lock weak_ptr to prevent accessing destroyed ExecSession
        auto self = weakSelf.lock();
        if (!self) {
            YRLOG_WARN("ExecSession already destroyed, ignoring exit callback");
            return;
        }

        // Check if session was already stopped
        if (!running_.load()) {
            YRLOG_INFO("Session was already stopped, ignoring exit callback, sessionId: {}", sessionId_);
            return;
        }

        YRLOG_INFO("Process exit detected (async callback), sessionId: {}", sessionId_);

        // Mark session as stopped
        running_ = false;

        // Get exit code from Future
        int exitCode = 0;
        if (!f.IsError() && f.Get().IsSome()) {
            exitCode = f.Get().Get();
        }

        YRLOG_INFO("Process exited, sessionId: {}, exitCode: {}", sessionId_, exitCode);

        // Call exit callback (thread-safe with mutex)
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            if (exitCallback_) {
                YRLOG_INFO("Calling exit callback, sessionId: {}, exitCode: {}", sessionId_, exitCode);
                exitCallback_(exitCode);
                YRLOG_INFO("Exit callback returned, sessionId: {}", sessionId_);
            } else {
                YRLOG_WARN("Exit callback is null, sessionId: {}", sessionId_);
            }
        }
    });

    YRLOG_INFO("Exit handler registered successfully, sessionId: {}", sessionId_);
}

Status ExecSession::WriteInput(const std::string& data)
{
    std::lock_guard<std::mutex> lock(ioMutex_);

    if (closed_.load() || !running_.load()) {
        return Status(StatusCode::FAILED, "Session is closed");
    }

    int writeFd;
    if (tty_ && ptyIO_) {
        // PTY mode: write to master fd
        writeFd = ptyIO_->masterFd;
    } else if (exec_ && exec_->GetIn().IsSome()) {
        // Pipe mode: write to stdin pipe
        writeFd = exec_->GetIn().Get();
    } else {
        return Status(StatusCode::FAILED, "No input stream available");
    }

    ssize_t written = write(writeFd, data.c_str(), data.size());
    if (written < 0) {
        YRLOG_ERROR("Write failed, sessionId: {}, errno: {}", sessionId_, errno);
        return Status(StatusCode::FAILED, "Write failed");
    }

    if (static_cast<size_t>(written) != data.size()) {
        YRLOG_WARN("Partial write, sessionId: {}, written: {}, total: {}",
                   sessionId_, written, data.size());
    }

    return Status::OK();
}

Status ExecSession::Resize(int rows, int cols)
{
    std::lock_guard<std::mutex> lock(ioMutex_);

    if (!tty_) {
        return Status(StatusCode::PARAMETER_ERROR, "Resize only available in TTY mode");
    }

    if (!ptyIO_ || ptyMasterFd_ < 0) {
        return Status(StatusCode::FAILED, "PTY not available");
    }

    if (ptyIO_->Resize(rows, cols) != 0) {
        return Status(StatusCode::FAILED, "Failed to resize PTY");
    }

    rows_ = rows;
    cols_ = cols;
    YRLOG_INFO("Window resized, sessionId: {}, rows: {}, cols: {}", sessionId_, rows, cols);

    return Status::OK();
}

void ExecSession::Close()
{
    // Use ioMutex_ to prevent race with WriteInput
    {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (closed_.exchange(true)) {
            return;  // Already closed
        }
    }

    YRLOG_INFO("Closing ExecSession, sessionId: {}", sessionId_);

    // Set running_ = false to let reader threads exit
    running_ = false;

    // Close PTY
    if (ptyIO_) {
        ptyIO_->Close();
        ptyIO_.reset();
    }

    // Give process some time to exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // If process still running, send SIGTERM
    if (exec_ && exec_->GetPid() > 0) {
        kill(exec_->GetPid(), SIGTERM);
    }

    // Wait for reader threads to finish
    if (stdoutReader_.joinable()) {
        stdoutReader_.join();
    }
    if (stderrReader_.joinable()) {
        stderrReader_.join();
    }

    YRLOG_INFO("ExecSession closed, sessionId: {}", sessionId_);
}

void ExecSession::OnOutput(OutputCallback cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    outputCallback_ = std::move(cb);
}

void ExecSession::OnExit(ExitCallback cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    exitCallback_ = std::move(cb);
}

}  // namespace functionsystem
