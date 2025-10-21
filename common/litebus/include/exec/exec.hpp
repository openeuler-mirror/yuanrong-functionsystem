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

#ifndef __LITEBUS_EXEC_HPP__
#define __LITEBUS_EXEC_HPP__

#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <vector>

#include <async/future.hpp>
#include <async/try.hpp>
#include "actor/buslog.hpp"

namespace litebus {

const int IO_CREATE_ERROR = -1;

/**
 * the IO for input/output/Error that used by child process
 * will have the 3 types
 * Pipe: redirect to pipe
 * File: redirect to a file
 * Filediscriptor
 **/
class ExecIO {
public:
    struct InFileDescriptor {
        int read = -1;
        Option<int> write = None();
    };

    struct OutFileDescriptor {
        Option<int> read = None();
        int write = -1;
    };

    std::function<Try<InFileDescriptor>()> inputSetup;

    std::function<Try<OutFileDescriptor>()> outputSetup;

    ExecIO(const std::function<Try<InFileDescriptor>()> &tInputSetup,
           const std::function<Try<OutFileDescriptor>()> &tOutputSetup)
        : inputSetup(tInputSetup), outputSetup(tOutputSetup){};

    ~ExecIO(){};
    // create a pipe io
    static ExecIO CreatePipeIO();

    // create a File Io
    static ExecIO CreateFileIO(const std::string &filePath);

    // create a File discriptor
    static ExecIO CreateFDIO(int fd);

    // read from pipe
    static std::string ReadPipe(int pipeRead);
};

/**
 * default child Hooks (functions);
 */
class ChildInitHook {
public:
    static std::function<void()> EXITWITHPARENT()
    {
        return []() {
            int r = prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (r == -1) {
                ::exit(errno);
            }
        };
    }
};

/**
 * default Parent Init Hooks (functions)
 */
class ParentInitHook {
};

// exec command class
class Exec {
public:
    pid_t GetPid() const
    {
        return pid;
    }

    Future<Option<int>> GetStatus() const
    {
        return future;
    };

    Option<int> GetIn() const
    {
        return inStream;
    }

    Option<int> GetOut() const
    {
        return outStream;
    }

    Option<int> GetErr() const
    {
        return errorStream;
    }

    Exec() : pid(0)
    {
    }

    // use bit to discript wether enable or disable
    ~Exec()
    {
        try {
            BUSLOG_DEBUG("IO Closed, pid:{}", pid);
            if (inStream.IsSome()) {
                (void)::close(inStream.Get());
            }
            if (outStream.IsSome()) {
                (void)::close(outStream.Get());
            }
            if (errorStream.IsSome()) {
                (void)::close(errorStream.Get());
            }
        } catch (...) {
            // Ignore
        }
    }
    /*
    to excute a command
    path          : the command or path of binary executable file
    argv                  :argument for command
    enviroment        : the enviroment of the command to execute
    in                      : the in stream of the command
    out                    : the out stream of the command
    error                    : the Error stream of the command
    init_child_funcs   : the functions of the child process to be excute
    init_parent_funcs: the fucntions of the parent process to be excute after
    create child process involker should handler exception in hook
    stopReaphook    : funcitons to be involk when the process terminate
     */
    static std::shared_ptr<Exec> CreateExec(const std::string &path, const std::vector<std::string> &argv,
                                            const Option<std::map<std::string, std::string>> &environment = None(),
                                            const ExecIO &stdIn = ExecIO::CreateFileIO("/dev/null"),
                                            const ExecIO &stdOut = ExecIO::CreateFileIO("/dev/null"),
                                            const ExecIO &stdError = ExecIO::CreateFileIO("/dev/null"),
                                            const std::vector<std::function<void()>> &childInitHooks = {},
                                            const std::vector<std::function<void(pid_t)>> &parentInitHooks = {},
                                            const bool &enableReap = true);

    /*
    command: the command fo shell
    */
    static std::shared_ptr<Exec> CreateExec(const std::string &command,
                                            const Option<std::map<std::string, std::string>> &environment = None(),
                                            const ExecIO &stdIn = ExecIO::CreateFileIO("/dev/null"),
                                            const ExecIO &stdOut = ExecIO::CreateFileIO("/dev/null"),
                                            const ExecIO &stdError = ExecIO::CreateFileIO("/dev/null"),
                                            const std::vector<std::function<void()>> &childInitHooks = {},
                                            const std::vector<std::function<void(pid_t)>> &parentInitHooks = {},
                                            const bool &enableReap = true);

private:
    // process Id of a process
    pid_t pid;

    // the future that will the process will return
    Future<Option<int>> future;

    // the input stream of the child process if any
    Option<int> inStream;

    // the output stream of the child process if any
    Option<int> outStream;

    // the error stream of the child process if any
    Option<int> errorStream;
};

using InFileDescriptor = ExecIO::InFileDescriptor;
using OutFileDescriptor = ExecIO::OutFileDescriptor;

namespace execinternal {
void HandleIOAndHook(const InFileDescriptor &stdIn, const OutFileDescriptor &stdOut, const OutFileDescriptor &stdError,
                     const std::vector<std::function<void()>> &childInitHooks);
}
}    // namespace litebus

#endif
