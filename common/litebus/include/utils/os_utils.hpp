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

#ifndef LITEBUS_OS_UTILS_H
#define LITEBUS_OS_UTILS_H

#include <cassert>
#include <cerrno>
#include <fts.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include "litebus.hpp"
#include "async/option.hpp"
#include "async/future.hpp"
#include "actor/buslog.hpp"
#include "string_utils.hpp"

namespace litebus {
namespace os {

constexpr char PATH_SEPARATOR = '/';
constexpr int ERROR_LENTH = 1024;
constexpr int BUFFER_SIZE_KB = 1024;
constexpr int BUFFER_SIZE_COUNT = 8;
constexpr int BUFFER_CONTENT_COUNT = 64;
constexpr int READ_FAIL = -1;
constexpr int POLL_OPEN_MAX = 1024;
constexpr int MAX_POLL_SIZE = 1;
constexpr size_t BUFFER_READ_SIZE = (BUFFER_SIZE_COUNT * BUFFER_SIZE_KB);
constexpr size_t BUFFER_CONTENT_SIZE = (BUFFER_SIZE_KB * BUFFER_CONTENT_COUNT);
constexpr size_t ENV_VAR_MAX_LENGTH = 8196;

const std::string LITEBUS_AKSK_ENABLED = "LITEBUS_AKSK_ENABLED";
const std::string LITEBUS_ACCESS_KEY = "LITEBUS_ACCESS_KEY";
const std::string LITEBUS_SECRET_KEY = "LITEBUS_SECRET_KEY";

enum DIR_AUTH {
    DIR_AUTH_600 = 600,
    DIR_AUTH_700 = 700,
    DIR_AUTH_750 = 0750,
};

inline std::string Join(const std::string &path1, const std::string &path2, const char separator = PATH_SEPARATOR)
{
    const std::string separatorStr = litebus::strings::ToString(separator).Get();
    return litebus::strings::Remove(path1, separatorStr, litebus::strings::SUFFIX) + separatorStr +
           litebus::strings::Remove(path2, separatorStr, litebus::strings::PREFIX);
}

std::string Strerror(int errnum);
litebus::Option<int> Mkdir(const std::string &directory, const bool recursive = true,
                           const DIR_AUTH dirAuth = DIR_AUTH_750);

inline litebus::Option<int> Rm(const std::string &path)
{
    if (::remove(path.c_str()) != 0) {
        return -1;
    }

    return litebus::None();
}

litebus::Option<int> Rmdir(const std::string &directory, bool recursive = true);

inline void SetEnv(const std::string &key, const std::string &value, bool overwrite = true)
{
    (void)::setenv(key.c_str(), value.c_str(), overwrite ? 1 : 0);
}

inline void UnSetEnv(const std::string &key)
{
    (void)::unsetenv(key.c_str());
}

litebus::Option<std::string> GetEnv(const std::string &key, size_t maxLength = ENV_VAR_MAX_LENGTH);

bool ExistPath(const std::string &path);

litebus::Option<std::vector<std::string>> Ls(const std::string &directory);

litebus::Option<int> Chown(const std::string &user, const std::string &path, bool recursive = true);

std::map<std::string, std::string> Environment();

litebus::Option<std::string> Read(const std::string &inputPath);

litebus::Option<std::string> RealPath(const std::string &inputPath, const int reserveLen = 0);

std::string GetFileName(const std::string &path);

int CloseOnExec(int fd);

inline int Nonblock(int fd);

std::string ReadPipe(int fd, int readMaxSize = BUFFER_READ_SIZE);

void ReadPipeRealTime(int fd, const std::function<void(const std::string &)> &readPipeCallback);

class PipeReadActor : public ActorBase {
public:
    PipeReadActor(const std::string &name) : ActorBase(name){};
    ~PipeReadActor() override{};
    std::string ReadPipeByPoll(int fd);
    // void ReadFromPipe, read by actor;readAsync:read once return atonce, long readMaxSize
    void ReadFromPipe(int fd, std::shared_ptr<Promise<std::string>> promise, AID aid,
                      std::shared_ptr<std::string> pipeContent, bool readASync = true,
                      int readMaxSize = BUFFER_CONTENT_SIZE);
    void ReadFromPipeRealTime(int fd, std::shared_ptr<Promise<std::string>> promise, AID aid,
                              const std::function<void(const std::string &)> &readPipeCallback);
    void ImmediatePipeReadOperation(int fd, const std::function<void(const std::string&)>& dataCallback);
};

litebus::Future<std::string> ReadPipeAsync(int fd, bool readASync = true);
litebus::Future<std::string> ReadPipeAsyncRealTime(int pipeFd,
                                                   const std::function<void(const std::string &)> &readPipeCallback);

}    // namespace os
}    // namespace litebus

#endif
