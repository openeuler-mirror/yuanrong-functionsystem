#ifndef __EXEC_TESTS_UTILS_H__
#define __EXEC_TESTS_UTILS_H__

#include <string>

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <random>
#include <sys/socket.h>
#include <netinet/in.h>

#include <gtest/gtest.h>

#include "async/future.hpp"
#include "async/option.hpp"
#include "async/try.hpp"
#include "litebus.hpp"
#include "exec/reap_process.hpp"

namespace litebus {

inline int find_available_port()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1024, 65535);

    int sock;
    struct sockaddr_in addr;

    while (true) {
        int port = dist(gen);
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1)
            continue;

        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(sock);
            return port;
        }
        close(sock);
    }
}

namespace exectest {

inline bool PidExist(pid_t pid)
{
    int r = ::kill(pid, 0);
    BUSLOG_INFO("r: {}, Error: {}", r, errno);
    return (r == 0 || errno == EPERM);
}

inline int KillPid(pid_t pid)
{
    return ::kill(pid, 9);
}

static std::string GetCWD()
{
    size_t size = 100;

    while (true) {
        char *temp = new char[size];
        if (::getcwd(temp, size) == temp) {
            std::string result(temp);
            delete[] temp;
            return result;
        } else {
            if (errno != ERANGE) {
                delete[] temp;
                return std::string();
            }
            size *= 2;
            delete[] temp;
        }
    }

    return std::string();
}

static int MakeTmpDir(const std::string &path = "tmp")
{
    std::string cmd = "mkdir -p " + path;
    int r = system(cmd.c_str());
    BUSLOG_INFO("tmp dir: {}", path);
    return r;
}

static int ChDir(const std::string &directory)
{
    return ::chdir(directory.c_str());
    BUSLOG_INFO("change DIR: {}", directory);
}

// remove a dir
static void RmDir(const std::string &path)
{
    std::string cmd = "rm -rf " + path;
    int r = system(cmd.c_str());
    EXPECT_GE(r, 0);
    BUSLOG_INFO("remove DIR: {}", path);
}

// close file descriptor
inline int Close(int fd)
{
    if (fd >= 0) {
        return ::close(fd);
    } else {
        return 0;
    }
}

inline int NonBlock(int fd)
{
    int f = ::fcntl(fd, F_GETFL);

    if (f != -1) {
        f = ::fcntl(fd, F_SETFL, f | O_NONBLOCK);
    }

    return f;
}

// file exist?
inline bool FileExists(const std::string &path)
{
    struct stat s;

    if (::lstat(path.c_str(), &s) < 0) {
        return false;
    }
    return true;
}

// generate a file
inline int TouchFile(const std::string &path)
{
    if (!FileExists(path)) {
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

        if (fd > 0) {
            return Close(fd);
        } else {
            return fd;
        }
    }
    // exist alreay
    return 0;
}

inline pid_t OSWaitPid(pid_t pid, int *status, int options)
{
    pid_t subPid = ::waitpid(pid, status, options);
    if (subPid == 0) {
        BUSLOG_WARN("Waitpid not subprocess exist, pid: {}", pid);
    } else if (subPid < 0) {
        BUSLOG_ERROR("Waitpid failed, pid: {}", pid);
    }
    return subPid;
}

inline int Cloexec(int fd)
{
    int flags = ::fcntl(fd, F_GETFD);

    if (flags == -1) {
        return flags;
    }

    if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        return -1;
    }

    return flags;
}

// read from a file
inline Try<std::string> Read(const std::string &path)
{
    FILE *file = ::fopen(path.c_str(), "r");
    if (file == nullptr) {
        BUSLOG_WARN("can not open file: {}", path);
        return Try<std::string>(-1);
    }

    // Use a buffer to read the file in BUFSIZ
    // chunks and append it to the string we return.
    //
    // NOTE: We aren't able to use fseek() / ftell() here
    // to find the file size because these functions don't
    // work properly for in-memory files like /proc/*/stat.
    char *buffer = new char[BUFSIZ];
    std::string result;

    while (true) {
        size_t read = ::fread(buffer, 1, BUFSIZ, file);

        if (::ferror(file)) {
            // NOTE: ferror() will not modify errno if the stream
            // is valid, which is the case here since it is open.
            delete[] buffer;
            ::fclose(file);
            return Try<std::string>(-1);
        }

        result.append(buffer, read);

        if (read != BUFSIZ) {
            assert(feof(file));
            break;
        }
    };

    ::fclose(file);
    delete[] buffer;
    BUSLOG_WARN("read result: {}", result);
    return result;
}

// wirte to a fd
inline int Write(int fd, const char *buffer, size_t count)
{
    size_t offset = 0;
    while (offset < count) {
        ssize_t length = ::write(fd, buffer + offset, count - offset);
        offset += length;
    }
    return offset;
}

inline int Write(const std::string &path, const std::string &message)
{
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (fd < 0) {
        return fd;
    }
    int r = Write(fd, message.data(), message.size());
    Close(fd);
    return r;
}

class TemporaryDirectoryTest : public ::testing::Test {
protected:
    virtual void SetUp()
    {
        BUSLOG_INFO("start");
    }

    virtual void TearDown()
    {
        BUSLOG_INFO("stop");
        // ReaperActor::Finalize();
        litebus::TerminateAll();
        // std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    // A temporary directory for test purposes.
    // Not to be confused with the "sandbox" that tasks are run in.

private:
    std::string cur_dir = "";

    std::string tmp_folder = "tmp";

    std::string tmpdir = "tmp";

protected:
    std::string GetTmpDir()
    {
        return tmpdir;
    }

    inline void SetupDir()
    {
        // Save the current working directory.
        std::string cwd = GetCWD();
        cur_dir = (cur_dir == "") ? cwd : cur_dir;
        tmpdir = cur_dir + "/" + tmp_folder;
        ChDir(cur_dir);
        RmDir(GetTmpDir());
        // Create a temporary directory for the test.
        int r = MakeTmpDir(tmpdir);
        EXPECT_GE(r, 0);
        ASSERT_GE(ChDir(GetTmpDir()), 0);
        BUSLOG_INFO("tmp dir create: {}", GetTmpDir());

        // Run the test out of the temporary directory we created.
    }

    inline void UnSetupDir()
    {
        // Return to previous working directory and cleanup the sandbox.
        ChDir(cur_dir);
        RmDir(GetTmpDir());
        BUSLOG_INFO("tmp dir deleted: {}", GetTmpDir());
    }
};

}    // namespace exectest
}    // namespace litebus

#endif    // __EXEC_TESTS_UTILS_H__
