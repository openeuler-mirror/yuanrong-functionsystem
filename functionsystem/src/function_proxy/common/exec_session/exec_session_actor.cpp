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

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <sstream>

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "exec/exec.hpp"
#include "io_event_actor.h"

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

std::shared_ptr<ExecSessionActor> ExecSessionActor::Create(const CreateParams &params)
{
    CreateParams resolved = params;
    if (resolved.sessionId.empty()) {
        resolved.sessionId = GenerateSessionId();
    }
    return std::make_shared<ExecSessionActor>(resolved);
}

ExecSessionActor::ExecSessionActor(const CreateParams &params)
    : litebus::ActorBase("ExecSessionActor-" + params.sessionId), streamWriter_(params.writer), sessionId_(params.sessionId)
{
    YRLOG_INFO("ExecSessionActor created, sessionId: {}", sessionId_);
}

ExecSessionActor::~ExecSessionActor()
{
    Close();
    YRLOG_INFO("ExecSessionActor destroyed, sessionId: {}", sessionId_);
}

void ExecSessionActor::Init()
{
    // No message handlers needed - all communication uses Async
}

void ExecSessionActor::Finalize()
{
    YRLOG_INFO("ExecSessionActor::Finalize, sessionId: {}", sessionId_);
    Cleanup();
}

void ExecSessionActor::DoOutput(const std::string &data, int exitCode)
{
    if (exitCode >= 0) {
        YRLOG_INFO("Process exit, sessionId: {}, exitCode: {}", sessionId_, exitCode);
        running_ = false;
        stdoutFd_ = -1;  // Mark unregistered so Cleanup skips (IOEventActor handles EOF path)
        stderrFd_ = -1;
        WriteToStream("", exitCode);
        Close();
    } else {
        WriteToStream(data, -1);
    }
}

// Do* methods - actual implementation with normal function signatures
void ExecSessionActor::DoStart(const std::string &containerId, const std::vector<std::string> &command,
                               const std::map<std::string, std::string> &env, bool tty, int rows, int cols)
{
    if (running_.load()) {
        YRLOG_WARN("Session already started, sessionId: {}", sessionId_);
        return;
    }

    containerId_ = containerId;
    command_ = command.empty() ? std::vector<std::string>{ "/bin/sh" } : command;
    tty_ = tty;
    rows_ = rows;
    cols_ = cols;

    std::vector<std::string> argv = { "docker", "exec", "-i" };

    for (const auto &[key, value] : env) {
        argv.push_back("-e");
        argv.push_back(key + "=" + value);
    }

    if (tty_) {
        argv.push_back("-t");
    }
    argv.push_back(containerId_);
    argv.insert(argv.end(), command_.begin(), command_.end());

    YRLOG_INFO("Starting docker exec, sessionId: {}, container: {}", sessionId_, containerId);

    if (tty_) {
        auto ptyResult = litebus::PtyExecIO::Create(rows_, cols_);
        if (ptyResult.IsError()) {
            YRLOG_ERROR("Failed to create PTY, sessionId: {}", sessionId_);
            WriteToStream("", 1);
            return;
        }

        auto pty = std::move(ptyResult).Get();
        ptyIO_ = std::make_unique<litebus::PtyExecIO>(std::move(pty));
        ptyMasterFd_ = ptyIO_->masterFd;

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
            childInitHooks, {}, true);

    } else {
        exec_ = litebus::Exec::CreateExec(
            "docker", argv, litebus::None(),
            litebus::ExecIO::CreatePipeIO(),
            litebus::ExecIO::CreatePipeIO(),
            litebus::ExecIO::CreatePipeIO(),
            { litebus::ChildInitHook::EXITWITHPARENT() }, {}, true);
    }

    if (!exec_) {
        YRLOG_ERROR("Failed to create exec process, sessionId: {}", sessionId_);
        WriteToStream("", 1);
        return;
    }

    YRLOG_INFO("Exec process started, sessionId: {}, pid: {}", sessionId_, exec_->GetPid());

    running_ = true;

    if (tty_) {
        stdinFd_ = ptyMasterFd_;
        stdoutFd_ = ptyMasterFd_;
    } else {
        if (exec_->GetIn().IsSome()) {
            stdinFd_ = exec_->GetIn().Get();
        }
        if (exec_->GetOut().IsSome()) {
            stdoutFd_ = exec_->GetOut().Get();
        }
    }

    auto onUnregister = [aid = GetAID()]() {
        litebus::Async(aid, &ExecSessionActor::DoCleanupAfterUnregister);
    };
    if (stdoutFd_ >= 0) {
        litebus::Async(IOEventActor::GetInstance(), &IOEventActor::DoRegister, stdoutFd_,
                       [aid = GetAID()](const std::string &data, int exitCode) {
                           litebus::Async(aid, &ExecSessionActor::DoOutput, data, exitCode);
                       },
                       onUnregister);
    }

    if (!tty_ && exec_->GetErr().IsSome()) {
        stderrFd_ = exec_->GetErr().Get();
        if (stderrFd_ >= 0) {
            litebus::Async(IOEventActor::GetInstance(), &IOEventActor::DoRegister, stderrFd_,
                           [aid = GetAID()](const std::string &data, int exitCode) {
                               litebus::Async(aid, &ExecSessionActor::DoOutput, data, exitCode);
                           },
                           nullptr);  // stdout's onUnregister triggers DoCleanupAfterUnregister
        }
    }

    RegisterExitHandler();
    WriteToStream("", -1);
}

void ExecSessionActor::DoInput(const std::string &data)
{
    if (closed_.load() || !running_.load() || stdinFd_ < 0) {
        return;
    }

    ssize_t written = write(stdinFd_, data.c_str(), data.size());
    if (written < 0) {
        YRLOG_ERROR("Write failed, sessionId: {}, errno: {}", sessionId_, errno);
    }
}

void ExecSessionActor::DoResize(int rows, int cols)
{
    if (!ptyIO_ || ptyMasterFd_ < 0) {
        return;
    }

    if (ptyIO_->Resize(rows, cols) != 0) {
        YRLOG_ERROR("Failed to resize PTY, sessionId: {}", sessionId_);
        return;
    }

    rows_ = rows;
    cols_ = cols;
}

void ExecSessionActor::DoClose()
{
    Close();
}

void ExecSessionActor::WriteToStream(const std::string &data, int exitCode)
{
    if (streamWriter_) {
        streamWriter_(data, exitCode);
    }
}

void ExecSessionActor::RegisterExitHandler()
{
    exec_->GetStatus().OnComplete(litebus::Defer(GetAID(), &ExecSessionActor::OnProcessExit, std::placeholders::_1));
}

void ExecSessionActor::OnProcessExit(const litebus::Future<litebus::Option<int>> &future)
{
    if (!running_.load()) {
        return;
    }

    running_ = false;

    int exitCode = 0;
    if (!future.IsError() && future.Get().IsSome()) {
        exitCode = future.Get().Get();
    }

    YRLOG_INFO("Process exited, sessionId: {}, exitCode: {}", sessionId_, exitCode);
    WriteToStream("", exitCode);
    Close();
}

void ExecSessionActor::Cleanup()
{
    running_ = false;

    int outFd = stdoutFd_;
    int errFd = stderrFd_;
    stdoutFd_ = -1;
    stderrFd_ = -1;

    YRLOG_INFO("Cleanup session, sessionId: {}, outFd: {}, errFd: {}", sessionId_, outFd, errFd);

    auto onAllUnregistered = [aid = GetAID(), this]() {
        YRLOG_INFO("Calling DoCleanupAfterUnregister, sessionId: {}", sessionId_);
        // Directly call instead of async dispatch to ensure it runs before actor destruction
        DoCleanupAfterUnregister();
    };
    if (outFd >= 0) {
        auto doNext = [aid = GetAID(), errFd, onAllUnregistered]() {
            if (errFd >= 0) {
                litebus::Async(IOEventActor::GetInstance(), &IOEventActor::DoUnregister, errFd, onAllUnregistered);
            } else {
                onAllUnregistered();
            }
        };
        litebus::Async(IOEventActor::GetInstance(), &IOEventActor::DoUnregister, outFd, doNext);
    } else if (errFd >= 0) {
        litebus::Async(IOEventActor::GetInstance(), &IOEventActor::DoUnregister, errFd, onAllUnregistered);
    } else {
        YRLOG_INFO("No valid file descriptors, calling onAllUnregistered directly, sessionId: {}", sessionId_);
        onAllUnregistered();
    }
}

void ExecSessionActor::DoCleanupAfterUnregister()
{
    stdoutFd_ = -1;  // Mark done so any queued Cleanup (from DoClose) skips unregister
    stderrFd_ = -1;
    if (ptyIO_) {
        ptyIO_->Close();
        ptyIO_.reset();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    YRLOG_INFO("DoCleanupAfterUnregister: exec_={}, sessionId: {}",
                (exec_ ? "valid" : "null"), sessionId_);

    if (exec_) {
        int pid = exec_->GetPid();
        YRLOG_INFO("DoCleanupAfterUnregister: pid={}, sessionId: {}", pid, sessionId_);
        if (pid > 0) {
            YRLOG_INFO("Killing exec process, pid: {}, sessionId: {}", pid, sessionId_);
            kill(pid, SIGTERM);
        }
    }
}

void ExecSessionActor::Close()
{
    if (closed_.exchange(true)) {
        return;
    }

    YRLOG_INFO("Closing session, sessionId: {}", sessionId_);
    Cleanup();
}

}  // namespace functionsystem
