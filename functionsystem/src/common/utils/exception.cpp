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

#include "exception.h"

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <execinfo.h>
#include <syscall.h>

namespace functionsystem {

const int MAX_TRACE_DEPTH = 64;

pid_t GetTid()
{
    return static_cast<pid_t>(syscall(SYS_gettid));
}

void RecordBackTrace(int sig)
{
    (void)signal(sig, SIG_DFL);
    void *backTraceArray[MAX_TRACE_DEPTH] = { nullptr };
    auto depth = backtrace(backTraceArray, MAX_TRACE_DEPTH);
    if (depth <= 0) {
        std::cerr << "get backtrace failed!" << std::endl;
        return;
    }

    std::cerr << "process pid " << GetTid() << " receive sig " << sig << std::endl;

    // backtrace_symbols_fd() does not call malloc,
    // and so can be employed in situations where the latter function might fail
    backtrace_symbols_fd(backTraceArray, depth, STDERR_FILENO);
    (void)raise(sig);
}

void RegisterSigHandler()
{
    std::vector<int> signals = { SIGBUS, SIGILL, SIGALRM, SIGABRT, SIGSEGV, SIGFPE };
    for (auto &iter : signals) {
        if (signal(iter, RecordBackTrace) == SIG_ERR) {
            std::cerr << "register signal handler failed. " << errno << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}

void RegisterGracefulExit(sighandler_t handler)
{
    std::vector<int> signals = { SIGINT, SIGTERM };
    for (auto &iter : signals) {
        if (signal(iter, handler) == SIG_ERR) {
            std::cerr << "register signal gracefulExit handler failed. " << errno << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}

}  // namespace functionsystem
