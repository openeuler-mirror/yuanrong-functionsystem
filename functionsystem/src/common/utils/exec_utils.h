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

#ifndef COMMON_UTILS_EXEC_UTILS_H
#define COMMON_UTILS_EXEC_UTILS_H

#include <csignal>
#include <fstream>
#include <regex>

#include <stdio.h>
#include <string>

#include "exec/exec.hpp"
#include "utils/os_utils.hpp"
#include "common/logs/logging.h"

namespace functionsystem {
struct CommandExecResult {
    std::string output;
    std::string error;
};

const int GETS_LINE_MAX_LEN = 256;
const size_t CMD_OUTPUT_MAX_LEN = 1024 * 1024 * 10;

[[maybe_unused]] static std::string AnonymizeCommand(const std::string &input)
{
    static const std::string ANONYMOUS = "****";
    static const int32_t PRE = 5;
    if (input.length() <= PRE) {
        return input;
    }
    return input.substr(0, PRE) + ANONYMOUS;
}

[[maybe_unused]] static CommandExecResult ExecuteCommand(const std::string &command)
{
    auto process =
        litebus::Exec::CreateExec(command, litebus::None(), litebus::ExecIO::CreateFDIO(STDIN_FILENO),
                                  litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO(), {}, {}, false);
    if (process == nullptr) {
        return CommandExecResult{ .output = "", .error = "failed to execute command, process is nullptr" };
    }
    litebus::Future<std::string> output = litebus::os::ReadPipeAsync(process->GetOut().Get());
    litebus::Future<std::string> error = litebus::os::ReadPipeAsync(process->GetErr().Get());

    CommandExecResult result{};
    result.output = output.Get();
    result.error = error.Get();
    return result;
}

[[maybe_unused]] static litebus::Future<CommandExecResult> AsyncExecuteCommand(const std::string &command)
{
    auto process =
        litebus::Exec::CreateExec(command, litebus::None(), litebus::ExecIO::CreateFDIO(STDIN_FILENO),
                                  litebus::ExecIO::CreatePipeIO(), litebus::ExecIO::CreatePipeIO(), {}, {}, false);
    if (process == nullptr) {
        return CommandExecResult{ .output = "", .error = "failed to execute command, process is nullptr" };
    }
    litebus::Future<std::string> output = litebus::os::ReadPipeAsync(process->GetOut().Get());
    auto promise = std::make_shared<litebus::Promise<CommandExecResult>>();
    (void)output.OnComplete([promise](const litebus::Future<std::string> &output) {
        CommandExecResult result{};
        if (output.IsOK()) {
            result.output = output.Get();
        } else {
            result.error = "failed to execute command";
        }
        promise->SetValue(result);
    });
    return promise->GetFuture();
}

[[maybe_unused]] static std::string EscapeShellCommand(const std::string& command)
{
    std::stringstream escapedCommand;

    for (char c : command) {
        switch (c) {
            case '"':  // Double quote needs to be escaped in shell
                escapedCommand << "\\\"";
                break;
            case '\\':  // Backslash needs to be escaped
                escapedCommand << "\\\\";
                break;
            case '$':  // Dollar sign could be interpreted as a variable reference in some shells
                escapedCommand << "\\$";
                break;
            case '`':  // Backtick needs to be escaped
                escapedCommand << "\\`";
                break;
            default:
                escapedCommand << c;
                break;
        }
    }

    return escapedCommand.str();
}

[[maybe_unused]] static std::string ExecuteCommandByPopen(const std::string &command, const size_t resultSize,
                                                          bool withStdErr = false, const std::string &traceInfo = "")
{
    auto startTime =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    FILE *stream;
    std::string fullCommand;
    if (withStdErr) {
        // Escape command string to handle any special characters
        // Wrap command with sh -c to ensure the shell interprets the 2>&1 redirection
        fullCommand = "sh -c \"" + EscapeShellCommand(command) + "\" 2>&1";
        YRLOG_DEBUG("fullCommand: {}", fullCommand);
    } else {
        fullCommand = command;
    }
    if ((stream = popen(fullCommand.c_str(), "r")); stream != nullptr) {
        std::string result;
        char line[GETS_LINE_MAX_LEN];
        while (fgets(line, GETS_LINE_MAX_LEN, stream) != nullptr) {
            result.append(line);
            if (result.length() > resultSize) {
                break;
            }
        }
        pclose(stream);
        stream = nullptr;
        auto endTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        YRLOG_INFO("{}|success to execute command({}) cost {} ms", traceInfo, AnonymizeCommand(command),
                   endTime - startTime);
        return result;
    }
    YRLOG_ERROR("popen error: {}", fullCommand);
    return "";
}

[[maybe_unused]] static bool CheckIllegalChars(const std::string &command)
{
    if (std::regex_search(command, std::regex("[$&!?*;<>{}|`\n\\[\\]\\\\]"))) {
        YRLOG_ERROR("command {} has invalid invalid characters.", command);
        return false;
    }
    return true;
}

[[maybe_unused]] static bool ClearFile(const std::string &filePath, const std::string &objectKey)
{
    if (!litebus::os::ExistPath(filePath)) {
        return true;
    }
    YRLOG_DEBUG("clear object {} from path {}.", objectKey, filePath);
    if (rename(filePath.c_str(), (filePath + "_tmp").c_str()) == 0) {
        auto status = litebus::os::Rmdir(filePath + "_tmp");
        if (!status.IsNone()) {
            YRLOG_WARN("failed to rmdir for object({}) after rename, status = {}.", objectKey, status.Get());
            return false;
        }
    } else {
        auto status = litebus::os::Rmdir(filePath);
        if (!status.IsNone()) {
            YRLOG_WARN("failed to rmdir for object({}), status = {}.", objectKey, status.Get());
            return false;
        }
    }
    return true;
}

[[maybe_unused]] static bool IsCentos()
{
    std::string line;
    std::ifstream file("/etc/os-release");
    bool result = false;
    if (file.is_open()) {
        while (getline(file, line)) {
            if (line.find("CentOS") != std::string::npos) {
                YRLOG_INFO("the operating system is CentOS");
                result = true;
                break;
            }
        }
        file.close();
    }
    return result;
}

[[maybe_unused]] static std::string TransMultiLevelDirToSingle(const std::string &dir)
{
    // process multi-level directory a/b/c/object  --> a-b-c-object
    auto items = litebus::strings::Split(dir, "/");
    if (items.size() == 0) {
        return "";
    }
    if (items.size() == 1) {
        return items[0];
    }
    std::string objectDir = "";
    for (auto item : items) {
        if (item.empty()) {
            continue;
        }
        objectDir = litebus::os::Join(objectDir, item, '-');
    }
    return litebus::strings::Trim(objectDir, litebus::strings::Mode::PREFIX, "-");
}

class RaiseWrapper {
public:
    RaiseWrapper() = default;
    virtual ~RaiseWrapper() = default;

    virtual void Raise(int sig)
    {
        (void)raise(sig);
    }
};

// wrapper for test inject function
class CommandRunner {
public:
    virtual ~CommandRunner() = default;
    virtual bool CheckAndRunCommandWrapper(const std::string& command)
    {
        if (!CheckIllegalChars(command)) {
            YRLOG_ERROR("failed to check illegal chars of command");
            return false;
        }
        if (system(command.c_str()) != 0) {
            YRLOG_ERROR("command error: {}", command);
            return false;
        }
        return true;
    }
    virtual CommandExecResult ExecuteCommandWrapper(const std::string& command)
    {
        CommandExecResult result;
        result.output = ExecuteCommandByPopen(command, CMD_OUTPUT_MAX_LEN);
        return result;
    };
}; // class CommandRunner
} // namespace functionsystem

#endif  // COMMON_UTILS_EXEC_UTILS_H
