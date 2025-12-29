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

#include <climits>
#include <dirent.h>
#include <fts.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <chrono>
#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/uuid_generator.hpp"
#include "unistd.h"
#include "utils/os_utils.hpp"

using litebus::None;
using litebus::Option;
using litebus::uuid_generator::UUID;
using std::string;

namespace litebus {
namespace os {

constexpr int PIPE_POLL_INTERV = 500;
constexpr int UPPRE_DIR_LEN = 2;
constexpr int STR_CALC_NUM = 2;
constexpr int CURRENT_DIR_LEN = 1;
const std::string STR_PIPE_SYNC_READER = "PIPE_READER_ACTOR";
AID g_pipeSyncReader;

std::atomic_bool g_pipeReaderLoaded(false);

std::string Strerror(int errnum)
{
    size_t size = ERROR_LENTH;
    char *buf = new (std::nothrow) char[size];
    BUS_ASSERT(buf != nullptr);
    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(buf, size, 0, size);
#if defined(__GLIBC__) && ((_POSIX_C_SOURCE < 200112L && _XOPEN_SOURCE < 600) || defined(_GNU_SOURCE))
    char *errorStr = ::strerror_r(errnum, buf, size);
    if (errorStr == nullptr) {
        delete[] buf;
        return "";
    }
    const std::string errmsg = errorStr;
    delete[] buf;
    return errmsg;
#else
    while (true) {
        delete[] buf;
        if (size <= 0) {
            return "";
        }
        buf = new (std::nothrow) char[size];
        if (buf == nullptr) {
            return "";
        }
        // 1. dest is valid 2. destsz equals to count and both are valid.
        // memset_s will always executes successfully.
        (void)memset_s(buf, size, 0, size);
        bool b = (::strerror_r(errnum, buf, size) != ERANGE);
        const std::string errmsg = buf;
        if (b) {
            delete[] buf;
            buf = nullptr;
            return errmsg;
        } else {
            size = size * STR_CALC_NUM;
        }
    }
#endif
}

Option<int> Mkdir(const std::string &directory, const bool recursive, const DIR_AUTH dirAuth)
{
    if (recursive) {
        std::string pathSeparator(1, PATH_SEPARATOR);
        std::vector<std::string> tokens = strings::Tokenize(directory, pathSeparator);
        std::string path;
        // if can not find "/", then it's the root path
        if (directory.find_first_of(pathSeparator) == 0) {
            path = PATH_SEPARATOR;
        }
        // create up level dir
        for (auto it = tokens.begin(); it != tokens.end(); ++it) {
            path += (*it + PATH_SEPARATOR);
            // create failed(not exist path), then return
            if (::mkdir(path.c_str(), static_cast<unsigned int>(dirAuth)) < 0 && errno != EEXIST) {
                return -1;
            }
        }
    } else if (::mkdir(directory.c_str(), static_cast<unsigned int>(dirAuth)) < 0 && errno != EEXIST) {
        return -1;
    }

    return None();
}

Option<std::string> GetEnv(const std::string &key, size_t maxLength)
{
    char *value = (::getenv(key.c_str()));
    if (value == nullptr) {
        return None();
    }
    if (strlen(value) > maxLength) {
        return None();
    }
    return std::string(value);
}

bool ExistPath(const std::string &path)
{
    struct stat s;
    return (::lstat(path.c_str(), &s) >= 0);
}

Option<std::vector<std::string>> Ls(const std::string &directory)
{
    DIR *dir = opendir(directory.c_str());
    if (dir == nullptr) {
        return litebus::None();
    }

    std::vector<std::string> result;
    struct dirent *entry = nullptr;
    errno = 0;

    while ((entry = readdir(dir)) != nullptr) {
        // not . &&..
        if (strncmp(entry->d_name, ".", CURRENT_DIR_LEN) != 0 && strncmp(entry->d_name, "..", UPPRE_DIR_LEN) != 0) {
            result.emplace_back(entry->d_name);
        }
    }

    // close dir
    if (closedir(dir) == -1 || errno != 0) {
        return litebus::None();
    }
    return result;
}

Option<int> Chown(uid_t uid, gid_t gid, const std::string &path, bool recursive)
{
    char *cPath[] = { const_cast<char *>(path.c_str()), nullptr };

    FTS *dirTree = (::fts_open(cPath, FTS_NOCHDIR | FTS_PHYSICAL, nullptr));
    if (dirTree == nullptr) {
        return litebus::None();
    }

    FTSENT *node;
    while ((node = (::fts_read(dirTree))) != nullptr) {
        if (node->fts_info == FTS_D || node->fts_info == FTS_F ||
            node->fts_info == FTS_SL || node->fts_info == FTS_SLNONE) {
            if (::lchown(node->fts_path, uid, gid) < 0) {
                (void)::fts_close(dirTree);
                return litebus::None();
            }
        } else if (node->fts_info == FTS_DNR || node->fts_info == FTS_ERR ||
                   node->fts_info == FTS_DC || node->fts_info == FTS_NS) {
            (void)::fts_close(dirTree);
            return litebus::None();
        }

        if (node->fts_level == FTS_ROOTLEVEL && !recursive) {
            break;
        }
    }
    (void)::fts_close(dirTree);
    return 0;
}    // namespace os

Option<int> Chown(const std::string &user, const std::string &path, bool recursive)
{
    passwd *passwd;
    if ((passwd = (::getpwnam(user.c_str()))) == nullptr) {
        return litebus::None();
    }
    Option<int> ret = Chown(passwd->pw_uid, passwd->pw_gid, path, recursive);
    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(passwd, sizeof(*passwd), 0, sizeof(*passwd));
    return ret;
}

std::map<std::string, std::string> Environment()
{
    char **env = environ;
    std::map<std::string, std::string> tmpEnv;
    size_t i = 0;
    while (env[i] != nullptr) {
        std::string evnStr(env[i++]);
        std::vector<std::string> strList = strings::Split(evnStr, "=", STR_CALC_NUM);
        if (strList.size() != STR_CALC_NUM) {
            continue;
        }
        tmpEnv[strList[0]] = strList[1];
    }
    return tmpEnv;
}

litebus::Option<std::string> Read(const std::string &inputPath)
{
    char path[PATH_MAX + 1] = { 0x00 };
    if (inputPath.length() > PATH_MAX || realpath(inputPath.c_str(), path) == nullptr) {
        return litebus::None();
    }

    FILE *file = (::fopen(path, "r"));
    if (file == nullptr) {
        return litebus::None();
    }

    char *buffer = new (std::nothrow) char[BUFSIZ];
    BUS_OOM_EXIT(buffer);
    std::string str;
    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(buffer, BUFSIZ, 0, BUFSIZ);
    for (;;) {
        size_t read = (::fread(buffer, 1, BUFSIZ, file));
        if (read == 0) {
            break;
        }
        if (::ferror(file)) {
            delete[] buffer;
            (void)::fclose(file);
            return litebus::None();
        }

        (void)str.append(buffer, read);

        if (read != BUFSIZ) {
            break;
        }
    };

    (void)::fclose(file);
    delete[] buffer;
    return str;
}

litebus::Option<std::string> RealPath(const std::string &inputPath, const int reserveLen)
{
    char path[PATH_MAX] = { 0x00 };
    if (reserveLen < 0) {
        return litebus::None();
    }
    if (inputPath.length() + static_cast<size_t>(reserveLen) >= static_cast<size_t>(PATH_MAX)) {
        return litebus::None();
    }
    if (nullptr == realpath(inputPath.c_str(), path)) {
        BUSLOG_WARN("realpath fail, errno: {}, {}", errno, Strerror(errno));
        return litebus::None();
    }
    return std::string(path);
}

litebus::Option<int> Rmdir(const std::string &directory, bool recursive)
{
    if (recursive) {
        if (!ExistPath(directory)) {
            return ENOENT;
        }

        char *paths[] = { const_cast<char *>(directory.c_str()), nullptr };
        FTS *dirTree = fts_open(paths, (FTS_NOCHDIR | FTS_PHYSICAL), nullptr);
        if (dirTree == nullptr) {
            return errno;
        }

        FTSENT *node;
        while ((node = fts_read(dirTree)) != nullptr) {
            if (node->fts_info == FTS_DP) {
                if (::rmdir(node->fts_path) < 0 && errno != ENOENT) {
                    (void)fts_close(dirTree);
                    return errno;
                }
            } else if (node->fts_info == FTS_SL || node->fts_info == FTS_F ||
                       node->fts_info == FTS_SLNONE || node->fts_info == FTS_DEFAULT) {
                if (::unlink(node->fts_path) < 0 && errno != ENOENT) {
                    (void)fts_close(dirTree);
                    return errno;
                }
            }
        }
        if (errno != 0) {
            (void)fts_close(dirTree);
            return errno;
        }

        if (fts_close(dirTree) < 0) {
            return errno;
        }
    } else if (::rmdir(directory.c_str())) {
        return errno;
    }

    return litebus::None();
}

std::string GetFileName(const std::string &path)
{
    char delim = '/';

    size_t i = path.rfind(delim, path.length());
    if (i != std::string::npos) {
        return (path.substr(i + 1, path.length() - i));
    }

    return "";
}

int CloseOnExec(int fd)
{
    int f = (::fcntl(fd, F_GETFD));
    if (f <= -1) {
        return -1;
    } else {
        auto u = static_cast<unsigned int>(f);
        f = ::fcntl(fd, F_SETFD, u | FD_CLOEXEC);
    }
    return (f <= -1) ? -1 : 0;
}

inline int Nonblock(int fd)
{
    int f = ::fcntl(fd, F_GETFL);
    if (f == -1) {
        return -1;
    } else {
        auto u = static_cast<unsigned int>(f);
        f = ::fcntl(fd, F_SETFL, u | O_NONBLOCK);
    }

    return (f == -1) ? -1 : 0;
}

// readsize:<0 abandon, 0:read all, >0 read sepcify size
std::string ReadPipe(int fd, int readMaxSize)
{
    int length = 0;
    char buf[BUFFER_READ_SIZE] = { 0 };
    size_t size = sizeof(buf);
    std::shared_ptr<string> buffer(std::make_shared<string>());
    BUS_OOM_EXIT(buffer);
    int lefReadMaxSize = readMaxSize;
    while ((length = (::read(fd, buf, size - 1))) > 0) {
        // append all
        if (lefReadMaxSize == 0) {
            (void)buffer->append(buf, static_cast<size_t>(length));
        } else if (lefReadMaxSize > 0) {    // need to append, not reach maxsize
            // only appen rest size, this is default, append all
            int appendLen = (lefReadMaxSize >= length) ? length : lefReadMaxSize;
            (void)buffer->append(buf, static_cast<size_t>(appendLen));
            lefReadMaxSize = lefReadMaxSize - appendLen;
            // to avoid reach max but 0(read all), then set -1(no need append)
            if (lefReadMaxSize == 0) {
                lefReadMaxSize = -1;
                BUSLOG_WARN("ReadPipe reach max: {},maxsize: {}", fd, readMaxSize);
            }
        }
        bzero(buf, size);
    }
    BUSLOG_DEBUG("fd read: {},{},|{}|", fd, buffer->size(), buffer->c_str());
    return std::move(*buffer);
}

void ReadPipeRealTime(int fd, const std::function<void(const std::string &)> &readPipeCallback)
{
    ssize_t length = 0;
    char buf[BUFFER_READ_SIZE] = { 0 };
    size_t size = sizeof(buf);
    while ((length = (::read(fd, buf, size - 1))) > 0) {
        std::string buffer;
        (void)buffer.append(buf, static_cast<size_t>(length));
        readPipeCallback(buffer);
        bzero(buf, size);
    }
    BUSLOG_DEBUG("fd read: {}", fd);
}

// void PipeReadActor::ReadFromPipe(int fd, std::shared_ptr<Promise<std::string>> promise, const litebus::AID &aid,
// std::string &pipeContent )
void PipeReadActor::ReadFromPipe(int fd, std::shared_ptr<Promise<std::string>> promise, AID aid,
                                 std::shared_ptr<std::string> pipeContent, bool readASync, int readMaxSize)

{
    // not read async, not poll, read all
    if (!readASync) {
        promise->SetValue(std::move(ReadPipe(fd, readMaxSize)));
        BUSLOG_DEBUG("ReadFromPipe finish");
        return;
    }
    // only poll current fd
    struct pollfd clientfds[MAX_POLL_SIZE];
    clientfds[0].fd = fd;
    clientfds[0].events = POLLIN | POLLERR | POLLNVAL | POLLHUP;
    // topoll return at once, 0-no time to wait
    int pollRet = ::poll(clientfds, MAX_POLL_SIZE, 0);
    if (pollRet > 0) {    // has event
        // event is read
        short revents = clientfds[0].revents;
        unsigned short uevents = (unsigned)revents;
        if (uevents & POLLIN) {
            (void)pipeContent->append(ReadPipe(fd, readMaxSize));
        }
        // if size not reach max and can read, then add poll again
        // POLLHUP event occu when pipe closed
        bool needPoll = (!(uevents & POLLNVAL) && !(uevents & POLLERR) && !(uevents & POLLHUP));
        if (needPoll) {
            // reach max size need to abandon
            auto readRemainSize = static_cast<int>(BUFFER_CONTENT_SIZE - pipeContent->size());
            // if remain size =0, then set it to -1(no need read)
            readRemainSize = (readRemainSize == 0) ? -1 : readRemainSize;
            // if size reach max then stop read and return
            if (readRemainSize < 0) {
                BUSLOG_WARN("fd read reach max size,fd: {}", fd);
            }
            (void)AsyncAfter(PIPE_POLL_INTERV, aid, &PipeReadActor::ReadFromPipe, fd, promise, aid, pipeContent, true,
                             readRemainSize);
        } else {
            BUSLOG_DEBUG("fd read finish:fd:{},size:{},|{}|", fd, pipeContent->size(), pipeContent->c_str());
            promise->SetValue(std::move(*pipeContent));
        }
    } else if (pollRet == 0) {    // no event
        (void)AsyncAfter(PIPE_POLL_INTERV, aid, &PipeReadActor::ReadFromPipe, fd, promise, aid, pipeContent, true,
                         readMaxSize);
    } else if (pollRet < 0) {    // error poll
        BUSLOG_ERROR("poll encounter error:{}", errno);
        promise->SetValue(std::move(*pipeContent));
    }
}

void PipeReadActor::ReadFromPipeRealTime(int fd, std::shared_ptr<Promise<std::string>> promise, AID aid,
                                         const std::function<void(const std::string &)> &readPipeCallback)

{
    // only poll current fd
    struct pollfd clientfds[MAX_POLL_SIZE];
    clientfds[0].fd = fd;
    clientfds[0].events = POLLIN | POLLERR | POLLNVAL | POLLHUP;

    // topoll return at once, 0-no time to wait
    int pollResult = ::poll(clientfds, MAX_POLL_SIZE, 0);
    if (pollResult > 0) {    // has event
        // event is read
        short revents = clientfds[0].revents;
        auto uevents = (unsigned short)revents;
        if (uevents & POLLIN) {
            ReadPipeRealTime(fd, readPipeCallback);
        }
        // if size not reach max and can read, then add poll again
        // POLLHUP event occu when pipe closed
        bool isNeedPoll = (!(uevents & POLLNVAL) && !(uevents & POLLERR) && !(uevents & POLLHUP));
        if (isNeedPoll) {
            (void)AsyncAfter(PIPE_POLL_INTERV, aid, &PipeReadActor::ReadFromPipeRealTime, fd, promise, aid,
                             readPipeCallback);
        } else {
            BUSLOG_DEBUG("fd read finish:fd:{}", fd);
            std::string msg = "fd read finish: fd: " + std::to_string(fd);
            promise->SetValue(msg);
        }
    } else if (pollResult == 0) {    // no event
        (void)AsyncAfter(PIPE_POLL_INTERV, aid, &PipeReadActor::ReadFromPipeRealTime, fd, promise, aid,
                         readPipeCallback);
    } else if (pollResult < 0) {    // error poll
        BUSLOG_ERROR("poll encounter error:{}", errno);
        std::string msg = "fd read finish: " + std::to_string(errno);
        promise->SetValue(msg);
    }
}

void PipeReadActor::ImmediatePipeReadOperation(int fd, const std::function<void(const std::string&)>& dataCallback)
{
    ReadPipeRealTime(fd, dataCallback);
    BUSLOG_DEBUG("Completed immediate read operation for fd:{}", fd);
}

// atomic int read actor name
std::atomic<int> g_readActorId(0);

Future<std::string> ReturnWithErr(int fd, string strErr)
{
    std::shared_ptr<Promise<std::string>> promise = std::make_shared<Promise<std::string>>();

    Future<std::string> f = promise->GetFuture();
    BUSLOG_ERROR("IO read dup error:{}", strErr);
    if (fd >= 0) {
        (void)::close(fd);
    }
    f.SetFailed(READ_FAIL);
    return f;
}

litebus::Future<std::string> ReadPipeAsync(int fd, bool readASync)
{
    if (fd < 0) {
        return ReturnWithErr(fd, "fd is invalid");
    }

    int dup = ::dup(fd);
    if (dup < 0) {
        return ReturnWithErr(fd, "IO read dup error");
    }

    fd = dup;
    int cloexec = CloseOnExec(fd);
    if (cloexec < 0) {
        return ReturnWithErr(fd, "IO read set close-on-exec error");
    }

    int noblock = Nonblock(fd);
    if (noblock < 0) {
        return ReturnWithErr(fd, "IO read set noblock error");
    }

    std::shared_ptr<Promise<std::string>> promise = std::make_shared<Promise<std::string>>();
    Future<std::string> f = promise->GetFuture();

    const std::string strPipeReadAsync = "AID" + std::to_string(g_readActorId.fetch_add(1));
    std::shared_ptr<std::string> pipeContent = std::make_shared<std::string>("");
    const AID pipeAsyncReader = litebus::Spawn(std::make_shared<PipeReadActor>(strPipeReadAsync));
    Async(pipeAsyncReader, &PipeReadActor::ReadFromPipe, fd, promise, pipeAsyncReader, pipeContent, readASync,
          BUFFER_CONTENT_SIZE);
    (void)f.OnComplete([pipeAsyncReader, fd]() {
        litebus::Terminate(pipeAsyncReader);
        BUSLOG_DEBUG("ReadPipeAsync read finished");
        (void)::close(fd);
    });
    return f;
}

litebus::Future<std::string> ReadPipeAsyncRealTime(int pipeFd,
                                                   const std::function<void(const std::string &)> &readPipeCallback)
{
    if (pipeFd < 0) {
        return ReturnWithErr(pipeFd, "fd is invalid");
    }

    int dup = ::dup(pipeFd);
    if (dup < 0) {
        return ReturnWithErr(pipeFd, "IO read dup error");
    }

    pipeFd = dup;
    int cloexec = CloseOnExec(pipeFd);
    if (cloexec < 0) {
        return ReturnWithErr(pipeFd, "IO read set close-on-exec error");
    }

    int noblock = Nonblock(pipeFd);
    if (noblock < 0) {
        return ReturnWithErr(pipeFd, "IO read set noblock error");
    }

    std::shared_ptr<Promise<std::string>> promise = std::make_shared<Promise<std::string>>();
    Future<std::string> f = promise->GetFuture();

    const std::string strPipeReadAsync = "AID" + std::to_string(g_readActorId.fetch_add(1));
    const AID pipeAsyncReader = litebus::Spawn(std::make_shared<PipeReadActor>(strPipeReadAsync));
    Async(pipeAsyncReader, &PipeReadActor::ReadFromPipeRealTime, pipeFd, promise, pipeAsyncReader, readPipeCallback);
    (void)f.OnComplete([pipeAsyncReader, pipeFd]() {
        litebus::Terminate(pipeAsyncReader);
        BUSLOG_DEBUG("ReadPipeAsync read finished");
        (void)::close(pipeFd);
    });
    return f;
}

}    // namespace os
}    // namespace litebus
