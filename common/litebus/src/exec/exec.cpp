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

#include <fcntl.h>
#include <cerrno>
#include <cstdlib>
#include <climits>
#include <unistd.h>

#include <string>
#include <set>
#include <map>
#include <functional>

#include "actor/buslog.hpp"
#include "exec/reap_process.hpp"
#include "securec.h"
#include "utils/os_utils.hpp"
#include "exec/exec.hpp"

namespace litebus {

static const int MAX_PARAMS_SIZE = 2048;

namespace execinternal {

// close a file descriptor
void CloseFD(const std::initializer_list<int> &fds)
{
    for (auto p = fds.begin(); p != fds.end(); p++) {
        if (*p >= 0) {
            (void)::close(*p);
        }
    }
}

// close all IO(input, output, Error)
void CloseAllIO(const InFileDescriptor &stdIn, const OutFileDescriptor &stdOut, const OutFileDescriptor &stdError)
{
    std::initializer_list<int> fds = {
        stdIn.read,   stdIn.write.IsNone() ? -1 : stdIn.write.Get(),     stdOut.read.IsNone() ? -1 : stdOut.read.Get(),
        stdOut.write, stdError.read.IsNone() ? -1 : stdError.read.Get(), stdError.write
    };
    execinternal::CloseFD(fds);
}

static void DoClean(const Future<Option<int>> &result, const std::shared_ptr<Promise<Option<int>>> &promise,
                    const std::shared_ptr<Exec>)
{
    // pending, no Discard
    if (result.IsInit()) {
        BUSLOG_INFO("Promise is initing");
    }
    if (!result.IsError()) {
        promise->SetValue(result.Get());
    } else {
        promise->SetFailed(result.GetErrorCode());
    }
    BUSLOG_INFO("Doclean after check");
}

// to clone a process
pid_t CloneProcess(const std::function<int()> &func)
{
    pid_t pid = (::fork());
    if (pid == -1) {
        return -1;
    } else if (pid == 0) {
        ::exit(func());
    } else {
        // Parent.
        BUSLOG_DEBUG("Clone child succ pid:{}", pid);
        return pid;
    }
}

// the main function of the exec command to be execute
int ExecMainFunc(const std::string &path, char **argVal, char **environmentParam, const InFileDescriptor &stdIn,
                 const OutFileDescriptor &stdOut, const OutFileDescriptor &stdError,
                 const std::vector<std::function<void()>> &childInitHooks)
{
    // to excuete the command
    HandleIOAndHook(stdIn, stdOut, stdError, childInitHooks);
    char **backupEnv = environ;
    environ = environmentParam;
    int r = (::execvp(path.c_str(), argVal));
    environ = backupEnv;
    // exit incase Error
    if (r < 0) {
        ::_exit(errno);
    }
    // to avoid complie warning;
    return r;
}

void HandleIOAndHook(const InFileDescriptor &stdIn, const OutFileDescriptor &stdOut, const OutFileDescriptor &stdError,
                     const std::vector<std::function<void()>> &childInitHooks)
{
    // Close parent's end of the pipes, only child process can acess
    // close in write of parent
    if (stdIn.write.IsSome()) {
        (void)::close(stdIn.write.Get());
    }
    // close out read of parent
    if (stdOut.read.IsSome()) {
        (void)::close(stdOut.read.Get());
    }
    // close Error read of parent
    if (stdError.read.IsSome()) {
        (void)::close(stdError.read.Get());
    }

    bool failed = false;
    // redirect input
    do {
        failed = (::dup2(stdIn.read, STDIN_FILENO) == -1 && errno == EINTR);
    } while (failed);
    // redirect output
    do {
        failed = (::dup2(stdOut.write, STDOUT_FILENO) == -1 && errno == EINTR);
    } while (failed);
    // redirect Error
    do {
        failed = (::dup2(stdError.write, STDERR_FILENO) == -1 && errno == EINTR);
    } while (failed);

    // close copy of the in/out/Error file descriptor
    // to ensure don't close a FD more than once
    if (stdIn.read != STDIN_FILENO && stdIn.read != STDOUT_FILENO && stdIn.read != STDERR_FILENO) {
        (void)::close(stdIn.read);
    }
    if (stdOut.write != STDIN_FILENO && stdOut.write != STDOUT_FILENO && stdOut.write != STDERR_FILENO
        && stdOut.write != stdIn.read) {
        (void)::close(stdOut.write);
    }
    if (stdError.write != STDIN_FILENO && stdError.write != STDOUT_FILENO && stdError.write != STDERR_FILENO
        && stdError.write != stdIn.read && stdError.write != stdOut.write) {
        (void)::close(stdError.write);
    }

    // exec child hook in loop
    auto it = childInitHooks.begin();
    for (; it != childInitHooks.end(); ++it) {
        (*it)();
    }
}

char **GenEnvFormMap(const Option<std::map<std::string, std::string>> &environment, unsigned int &envSize)
{
    char **environmentParam = environ;
    // use param enviroment instead of system default
    if (environment.IsSome()) {
        // NOTE: We add 1 to the size for a `nullptr` terminator.
        envSize = environment.Get().size();
        if (environment.Get().size() > MAX_PARAMS_SIZE) {
            // if enviroment oversize then set max size to defined size and log
            BUSLOG_WARN("Environment size overflow size:{}", environment.Get().size());
            envSize = MAX_PARAMS_SIZE;
        }
        environmentParam = new (std::nothrow) char *[envSize + 1];
        BUS_OOM_EXIT(environmentParam);

        size_t index = 0;
        // avoid  enviroment overflow size
        for (auto it = environment.Get().begin(); index < envSize; ++it, index++) {
            std::string entry = it->first + "=" + it->second;
            environmentParam[index] = new (std::nothrow) char[entry.size() + 1];
            BUS_OOM_EXIT(environmentParam[index]);
            // 1. dest is valid 2. destsz equals to count and both are valid.
            // memset_s will always executes successfully.
            (void)memset_s(environmentParam[index], entry.size() + 1, 0, entry.size() + 1);
            errno_t e = strncpy_s(environmentParam[index], entry.size() + 1, entry.c_str(), entry.size());
            if (e != 0) {
                BUSLOG_ERROR("strncpy_s env Error, entry:{},Error:{}", entry, e);
                BUS_EXIT("GenEnvFormMap strncpy_s env Error");
            }
        }
        // execvpe arg last param must be NULL
        environmentParam[index] = nullptr;
    }
    return environmentParam;
}

// to clone a exec command
Try<pid_t> CloneExec(const std::string &path, std::vector<std::string> argv,
                     const Option<std::map<std::string, std::string>> &environment, const InFileDescriptor &stdIn,
                     const OutFileDescriptor &stdOut, const OutFileDescriptor &stdError,
                     const std::vector<std::function<void()>> &childInitHooks,
                     const std::vector<std::function<void(pid_t)>> &)
{
    // shell command parameters char*
    char **argValues = new (std::nothrow) char *[argv.size() + 1];
    BUS_OOM_EXIT(argValues);
    for (size_t i = 0; i < argv.size(); i++) {
        argValues[i] = const_cast<char *>(argv[i].c_str());
    }
    // execvpe arg last param must be NULL
    argValues[argv.size()] = nullptr;

    // envirement parameters char*
    unsigned int envSize = 0;
    char **environmentParam = GenEnvFormMap(environment, envSize);

    // if parent hook will block the pipe whil a clone process executing, to
    // resolve
    pid_t pid = CloneProcess(
        std::bind(&ExecMainFunc, path, argValues, environmentParam, stdIn, stdOut, stdError, childInitHooks));

    // delete  char arguments  and char envirement parameters
    delete[] argValues;
    if (environment.IsSome()) {
        for (unsigned int i = 0; i < envSize; i++) {
            delete[] environmentParam[i];
        }
        delete[] environmentParam;
    }

    BUSLOG_DEBUG("Finish clone a exec command pid:{}", pid);
    // close child's FD
    execinternal::CloseFD({ stdIn.read, stdOut.write, stdError.write });
    return pid;
}

// set those file to open to sub process
int CloseOnExec(const InFileDescriptor &stdIn, const OutFileDescriptor &stdOut, const OutFileDescriptor &stdError)
{
    std::set<int> fdSet;
    (void)fdSet.insert(stdIn.read);
    (void)fdSet.insert(stdIn.write.IsNone() ? -1 : stdIn.write.Get());
    (void)fdSet.insert(stdOut.read.IsNone() ? -1 : stdOut.read.Get());
    (void)fdSet.insert(stdOut.write);
    (void)fdSet.insert(stdError.read.IsNone() ? -1 : stdError.read.Get());
    (void)fdSet.insert(stdError.write);

    auto it = fdSet.begin();
    for (; it != fdSet.end(); ++it) {
        if (*it >= 0) {
            int r = os::CloseOnExec(*it);
            // if set Error then retrun
            if (r == -1) {
                return r;
            }
        }
    }

    return 0;
}

}    // namespace execinternal

ExecIO ExecIO::CreatePipeIO()
{
    const int PIPE_PAIR_SIZE = 2;
    std::function<Try<InFileDescriptor>()> inFunc = []() {
        int pipeFd[PIPE_PAIR_SIZE];
        if (::pipe(pipeFd) == -1) {
            BUSLOG_ERROR("Create Pipe IO in failed");
            return Try<InFileDescriptor>(Failure(IO_CREATE_ERROR));
        }
        InFileDescriptor fileDescriptors;
        fileDescriptors.read = pipeFd[0];
        fileDescriptors.write = pipeFd[1];
        return Try<InFileDescriptor>(fileDescriptors);
    };

    std::function<Try<OutFileDescriptor>()> outFunc = []() {
        int pipeFd[PIPE_PAIR_SIZE];
        if (::pipe(pipeFd) == -1) {
            BUSLOG_ERROR("Create Pipe IO in failed");
            return Try<OutFileDescriptor>(Failure(IO_CREATE_ERROR));
        }
        OutFileDescriptor fileDescriptors;
        fileDescriptors.read = pipeFd[0];
        fileDescriptors.write = pipeFd[1];
        return Try<OutFileDescriptor>(fileDescriptors);
    };

    return ExecIO(inFunc, outFunc);
}

ExecIO ExecIO::CreateFileIO(const std::string &filePath)
{
    std::function<Try<InFileDescriptor>()> inFunc = [filePath]() {
        char path[PATH_MAX + 1] = { 0x00 };
        if (strlen(filePath.c_str()) > PATH_MAX || nullptr == realpath(filePath.c_str(), path)) {
            return Try<InFileDescriptor>(Failure(IO_CREATE_ERROR));
        }
        int fd = (::open(path, O_RDONLY | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP));
        if (fd < 0) {
            BUSLOG_ERROR("Create File IO in failed:{}", fd);
            return Try<InFileDescriptor>(Failure(IO_CREATE_ERROR));
        }

        InFileDescriptor fileDescriptors;
        fileDescriptors.read = fd;
        return Try<InFileDescriptor>(fileDescriptors);
    };

    std::function<Try<OutFileDescriptor>()> outFunc = [filePath]() {
        char path[PATH_MAX + 1] = { 0x00 };
        if (strlen(filePath.c_str()) > PATH_MAX || nullptr == realpath(filePath.c_str(), path)) {
            return Try<OutFileDescriptor>(Failure(IO_CREATE_ERROR));
        }
        int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP);
        if (fd < 0) {
            BUSLOG_ERROR("Create File IO in failed:{}", fd);
            return Try<OutFileDescriptor>(Failure(IO_CREATE_ERROR));
        }
        OutFileDescriptor fileDescriptors;
        fileDescriptors.write = fd;
        return Try<OutFileDescriptor>(fileDescriptors);
    };

    return ExecIO(inFunc, outFunc);
}

ExecIO ExecIO::CreateFDIO(int fd)
{
    std::function<Try<InFileDescriptor>()> inFunc = [fd]() {
        int dup = (::dup(fd));
        if (dup < 0) {
            BUSLOG_ERROR("Create FD IO in failed");
            return Try<InFileDescriptor>(Failure(IO_CREATE_ERROR));
        }
        InFileDescriptor fileDescriptors;
        fileDescriptors.read = dup;
        return Try<InFileDescriptor>(fileDescriptors);
    };

    std::function<Try<OutFileDescriptor>()> outFunc = [fd]() {
        int dup = (::dup(fd));
        if (dup < 0) {
            BUSLOG_ERROR("Create FD IO in failed");
            return Try<OutFileDescriptor>(Failure(IO_CREATE_ERROR));
        }

        OutFileDescriptor fileDescriptors;
        fileDescriptors.write = dup;
        return Try<OutFileDescriptor>(fileDescriptors);
    };

    return ExecIO(inFunc, outFunc);
}

namespace Shell {
constexpr const char *CMD = "sh";
constexpr const char *ARG0 = "sh";
constexpr const char *ARG1 = "-c";
}    // namespace Shell

std::shared_ptr<Exec> Exec::CreateExec(const std::string &command,
                                       const Option<std::map<std::string, std::string>> &environment,
                                       const ExecIO &stdIn, const ExecIO &stdOut, const ExecIO &stdError,
                                       const std::vector<std::function<void()>> &childInitHooks,
                                       const std::vector<std::function<void(pid_t)>> &parentInitHooks,
                                       const bool &enableReap)
{
    std::vector<std::string> argv = { Shell::ARG0, Shell::ARG1, command };
    return CreateExec(Shell::CMD, argv, environment, stdIn, stdOut, stdError, childInitHooks, parentInitHooks,
                      enableReap);
}

std::shared_ptr<Exec> Exec::CreateExec(const std::string &path, const std::vector<std::string> &argv,
                                       const Option<std::map<std::string, std::string>> &environment,
                                       const ExecIO &stdIn, const ExecIO &stdOut, const ExecIO &stdError,
                                       const std::vector<std::function<void()>> &childInitHooks,
                                       const std::vector<std::function<void(pid_t)>> &parentInitHooks,
                                       const bool &enableReap)
{
    InFileDescriptor tStdIn;
    OutFileDescriptor tStdOut;
    OutFileDescriptor tStdError;
    // get input stream
    Try<InFileDescriptor> input = stdIn.inputSetup();
    if (input.IsError()) {
        BUSLOG_ERROR("input setup failed!");
        return nullptr;
    }
    tStdIn = input.Get();

    // get output stream
    Try<OutFileDescriptor> output = stdOut.outputSetup();
    if (output.IsError()) {
        BUSLOG_ERROR("output setup failed!");
        litebus::execinternal::CloseAllIO(tStdIn, tStdOut, tStdError);
        return nullptr;
    }
    tStdOut = output.Get();

    // get Error stream
    output = stdError.outputSetup();
    if (output.IsError()) {
        BUSLOG_ERROR("output setup failed!");
        litebus::execinternal::CloseAllIO(tStdIn, tStdOut, tStdError);
        return nullptr;
    }
    tStdError = output.Get();

    // automatic close fd when parent process exit
    int r = execinternal::CloseOnExec(tStdIn, tStdOut, tStdError);
    if (r == -1) {
        BUSLOG_ERROR("CloseOnExec setup failed!");
        execinternal::CloseAllIO(tStdIn, tStdOut, tStdError);
        return nullptr;
    }

    std::shared_ptr<Exec> tExec = std::make_shared<Exec>();
    // clone a process and run
    Try<pid_t> pid =
        execinternal::CloneExec(path, argv, environment, tStdIn, tStdOut, tStdError, childInitHooks, parentInitHooks);

    if (pid.IsError()) {
        BUSLOG_ERROR("Clone a exec command failed!");
        litebus::execinternal::CloseAllIO(tStdIn, tStdOut, tStdError);
        return nullptr;
    }

    tExec->pid = pid.Get();
    tExec->inStream = tStdIn.write;
    tExec->outStream = tStdOut.read;
    tExec->errorStream = tStdError.read;

    std::shared_ptr<Promise<Option<int>>> promise = std::make_shared<Promise<Option<int>>>();
    tExec->future = promise->GetFuture();
    if (enableReap) {
        // reap in a thread
        // in case of the FD will close once exec has finalized, so need to keep the
        // exec until reap the child process
        (void)litebus::ReapInActor(tExec->pid)
            .OnComplete(std::bind(execinternal::DoClean, std::placeholders::_1, promise, tExec));
    } else {
        promise->SetValue(litebus::Option<int>(0));
    }

    return tExec;
}

}    // namespace litebus
