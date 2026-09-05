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

#include "runtime_manager/log/log_manager.h"

#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <utime.h>

#include <atomic>
#include <fstream>
#include <regex>

#include "common/logs/logging.h"
#include "runtime_manager/manager/runtime_manager_test_actor.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"
#include "utils/os_utils.hpp"

namespace functionsystem::test {
using namespace functionsystem::runtime_manager;
using namespace ::testing;

namespace {
const std::string LOG_BASE_DIR = "/tmp/snuser/log/";
const std::string LOG_NAME = "dggphis151702";
const std::string EXCEPTION_LOG_DIR = "/tmp/snuser/log/exception/";
const std::string STD_LOG_DIR = "/tmp/snuser/log/instances/";
}  // namespace

class LogManagerActorHelper : public runtime_manager::LogManagerActor {
public:
    explicit LogManagerActorHelper(const std::string &name, const litebus::AID &runtimeManagerAID,
                                   bool logReuse = false)
        : LogManagerActor(name, runtimeManagerAID, logReuse) {};
    MOCK_METHOD(litebus::Future<bool>, IsRuntimeActive, (const std::string &runtimeID), (const));
    MOCK_METHOD(litebus::Future<bool>, IsRuntimeActiveByPid, (const pid_t &pid), (const));
};

class LogManagerTest : public testing::Test {
public:
    void MockCreateJavaRuntimeLogs()
    {
        javaRuntimeID_ = "runtime-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        auto javaLogDir = litebus::os::Join(LOG_BASE_DIR, javaRuntimeID_);
        (void)litebus::os::Mkdir(javaLogDir);

        auto javaRuntimeErrorLog = "java-runtime-error.log";
        auto javaRuntimeErrorLogFile = litebus::os::Join(javaLogDir, javaRuntimeErrorLog);
        auto fd = open(javaRuntimeErrorLogFile.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);
        std::ofstream outfile;
        outfile.open(javaRuntimeErrorLogFile.c_str());
        outfile << "java runtime error log. This is a Test." << std::endl;
        outfile.close();

        auto javaRuntimeWarnLog = "java-runtime-warn.log";
        auto javaRuntimeWarnLogFile = litebus::os::Join(javaLogDir, javaRuntimeWarnLog);
        fd = open(javaRuntimeWarnLogFile.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);
        outfile.open(javaRuntimeWarnLogFile.c_str());
        outfile << "java runtime warn log. This is a Test." << std::endl;
        outfile.close();

        auto javaRuntimeAllLog = "java-runtime-all.log";
        auto javaRuntimeAllLogFile = litebus::os::Join(javaLogDir, javaRuntimeAllLog);
        fd = open(javaRuntimeAllLogFile.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);
        outfile.open(javaRuntimeAllLogFile.c_str());
        outfile << "java runtime all log. This is a Test." << std::endl;
        outfile.close();
    }

    void MockCreateCppRuntimeLogs()
    {
        auto jobId = litebus::uuid_generator::UUID::GetRandomUUID().ToString().substr(0, 8);
        cppRuntimeID_ = "runtime-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        auto cppLogFile = litebus::os::Join(LOG_BASE_DIR, jobId + "-" + cppRuntimeID_ + ".log");

        int fd = open(cppLogFile.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);

        std::ofstream outfile;
        outfile.open(cppLogFile.c_str());
        outfile << "cpp runtime log. This is a Test." << std::endl;
        outfile.close();
    }

    void MockCreateCppRuntimeLogs2()
    {
        auto jobId = "cpp-runtime_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString().substr(0, 8) + "_";
        cppRuntimeID_ = "runtime-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        auto cppLogFile = litebus::os::Join(LOG_BASE_DIR, jobId + cppRuntimeID_ + ".log.gz");

        int fd = open(cppLogFile.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);

        std::ofstream outfile;
        outfile.open(cppLogFile.c_str());
        outfile << "cpp runtime log. This is a Test." << std::endl;
        outfile.close();
    }

    void MockCreateLibRuntimeLogs()
    {
        (void)litebus::os::Mkdir(LOG_BASE_DIR);
        auto jobId = "job-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString().substr(0, 8);
        libRuntimeID_ = "runtime-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        auto logFile = litebus::os::Join(LOG_BASE_DIR, jobId + "-" + libRuntimeID_ + ".log");

        int fd = open(logFile.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);

        std::ofstream outfile;
        outfile.open(logFile.c_str());
        outfile << "cpp runtime log. This is a Test." << std::endl;
        outfile.close();
    }

    void MockCreateDsClientLogs()
    {
        std::vector<std::string> logFiles = {
            "ds_client_12345.DEBUG",
            "ds_client_12345.DEBUG.log",
            "ds_client_12345.INFO",
            "ds_client_12345.INFO.log",
            "ds_client_12345.WARNING",
            "ds_client_12345.WARNING.log",
            "ds_client_12345.ERROR",
            "ds_client_12345.ERROR.log",
            "ds_client_access_12345.log"
        };
        (void)litebus::os::Mkdir(LOG_BASE_DIR);
        for (const auto& logFile : logFiles) {
            auto logPath = litebus::os::Join(LOG_BASE_DIR, logFile);
            auto fd = open(logPath.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
            EXPECT_NE(fd, -1);
            close(fd);
            std::ofstream outfile;
            outfile.open(logPath.c_str());
            outfile << logPath << " This is a Test." << std::endl;
            outfile.close();
        }
    }

    void MockCreateCppRuntimeRollingLogsWithCompression()
    {
        auto jobId = litebus::uuid_generator::UUID::GetRandomUUID().ToString().substr(0, 8);
        cppRollingCompressionRuntimeID_ = "runtime-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        for (int i = 10; i >= 1; --i) {
            std::stringstream logFileName;
            auto cppLogFile = litebus::os::Join(LOG_BASE_DIR, jobId + "-" + cppRollingCompressionRuntimeID_);
            if (i == 1) {
                logFileName << cppLogFile << ".log";
            } else {
                logFileName << cppLogFile << "." << (i - 1) << ".log.gz";
            }
            YRLOG_DEBUG("Creating log files for: {}", cppLogFile);
            std::ofstream outfile(logFileName.str());
            outfile << "cpp runtime log #" << i << ". This is a Test." << std::endl;
            outfile.close();
            YRLOG_DEBUG("Created: {}", logFileName.str());
        }
        YRLOG_DEBUG("Finished creating log files.");
    }

    void MockCreatePythonRuntimeRollingLogs()
    {
        pythonRollingRuntimeID_ = "runtime-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        for (int i = 10; i >= 1; --i) {
            std::stringstream logFileName;
            std::string cppLogFile = litebus::os::Join(LOG_BASE_DIR, cppRollingRuntimeID_);
            if (i == 1) {
                logFileName << cppLogFile << ".log";
            } else {
                logFileName << cppLogFile << ".log" << "." << (i - 1);
            }

            YRLOG_DEBUG("Creating log files for: {}", cppLogFile);
            std::ofstream outfile(logFileName.str());
            outfile << "cpp runtime log #" << i << ". This is a Test." << std::endl;
            outfile.close();
            YRLOG_DEBUG("Created: {}", logFileName.str());
        }
        YRLOG_DEBUG("Finished creating log files.");
    }

    void MockCreateCppRuntimeRollingLogs()
    {
        auto jobId = litebus::uuid_generator::UUID::GetRandomUUID().ToString().substr(0, 8);
        cppRollingRuntimeID_ = "runtime-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        for (int i = 10; i >= 1; --i) {
            std::stringstream logFileName;
            std::string cppLogFile = litebus::os::Join(LOG_BASE_DIR, jobId + "-" + cppRollingRuntimeID_);
            if (i == 1) {
                logFileName << cppLogFile << ".log";
            } else {
                logFileName << cppLogFile << "." << (i - 1) << ".log";
            }

            YRLOG_DEBUG("Creating log files for: {}", cppLogFile);
            std::ofstream outfile(logFileName.str());
            outfile << "cpp runtime log #" << i << ". This is a Test." << std::endl;
            outfile.close();
            YRLOG_DEBUG("Created: {}", logFileName.str());
        }
        YRLOG_DEBUG("Finished creating log files.");
    }

    void MockCreatePythonRuntimeLogs()
    {
        pythonRuntimeID_ = "runtime-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        auto pythonLogFile = litebus::os::Join(LOG_BASE_DIR, pythonRuntimeID_ + ".log");

        int fd = open(pythonLogFile.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);

        std::ofstream outfile;
        outfile.open(pythonLogFile.c_str());
        outfile << "python runtime log. This is a Test." << std::endl;
        outfile.close();
    }

    void MockCreateExceptionLogs()
    {
        litebus::os::Mkdir(EXCEPTION_LOG_DIR);
        const std::string &runtimeBackTraceLog = EXCEPTION_LOG_DIR + "/BackTrace_runtime-ID.log";
        auto fd = open(runtimeBackTraceLog.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);
        std::ofstream outfile;
        outfile.open(runtimeBackTraceLog.c_str());
        outfile << "runtime ID backtrace log. This is a Test." << std::endl;
        outfile.close();
    }

    void MockCreateRuntimeStdLogs()
    {
        if (!litebus::os::ExistPath(STD_LOG_DIR)) {
            litebus::os::Mkdir(STD_LOG_DIR);
        }
        const std::string &runtimeStdLog = STD_LOG_DIR + LOG_NAME + "-user_func_std.log";
        auto fd = open(runtimeStdLog.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        EXPECT_NE(fd, -1);
        close(fd);

        std::ofstream outfile;
        outfile.open(runtimeStdLog.c_str());
        outfile << "runtime ID Std log. This is a Test." << std::endl;
        outfile.close();
    }

    std::string MockCreateSeparatedRuntimeStdLog(const std::string &runtimeID, const std::string &extension,
                                                 time_t modificationTime = std::time(nullptr))
    {
        (void)litebus::os::Mkdir("/tmp/snuser");
        (void)litebus::os::Mkdir(LOG_BASE_DIR);
        auto logFile = litebus::os::Join(LOG_BASE_DIR, runtimeID + extension);
        std::ofstream outfile(logFile);
        outfile << "separated runtime std log. This is a Test." << std::endl;
        outfile.close();

        struct utimbuf times = { modificationTime, modificationTime };
        EXPECT_EQ(utime(logFile.c_str(), &times), 0);
        return logFile;
    }

    std::string GenerateNamedRuntimeID(size_t index)
    {
        return "runtime-named-instance-" + std::to_string(index);
    }

    void MockCreateLogs()
    {
        // mock runtime logs
        MockCreateJavaRuntimeLogs();
        MockCreateCppRuntimeLogs();
        MockCreatePythonRuntimeLogs();

        // mock exception log
        MockCreateExceptionLogs();

        // mock runtime std log
        MockCreateRuntimeStdLogs();
    }

    void MockCreatePrefixLogs()
    {
        (void)litebus::os::Mkdir(LOG_BASE_DIR);
        std::vector<std::string> logNameList = {
            "YR_123_000001_abc_libruntime.log",
            "YR_123_000001_abc_libruntime.2025081617281825.log",
            "YR_123_000001_abc_libruntime.2025081801463525.log.gz",
            "YR_123_000001_abc_libruntime.2025082901463525.log.gz.1",
            "YR_123_000002_runtime.log",
            "YR_123_000002_runtime.1.log",
            "YR_123_000002_runtime.log.gz",
            "YR_123_000002_runtime.log.gz.10086",
            "YR_123_000003_runtime.log",
            "YR_123_000003_runtime.1755842461755699.log.gz",
            "YR_123_000003_runtime.1755971816292456.log.gz.1123",
            "YR_123_000003_libruntime.log",
            "YR_123_000003_libruntime.1755842461755699.log.gz",
        };
        for (auto logName : logNameList) {
            std::string logFileName = litebus::os::Join(LOG_BASE_DIR, logName);

            int fd = open(logFileName.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
            EXPECT_NE(fd, -1);
            close(fd);

            std::ofstream outfile;
            outfile.open(logFileName);
            outfile << "runtime log #" << logFileName << ". This is a Test." << std::endl;
            outfile.close();
            YRLOG_DEBUG("Creating log files for: {}", logFileName);
        }
    }

    void MockCreateManyPrefixLogs(int cnt = 100)
    {
        (void)litebus::os::Mkdir(LOG_BASE_DIR);
        for (int i = 0; i < cnt; i++) {
            std::string logFileName = litebus::os::Join(LOG_BASE_DIR, "YR_123_000005_xxx." + std::to_string(i) + ".log");
            int fd = open(logFileName.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
            EXPECT_NE(fd, -1);
            close(fd);

            std::ofstream outfile;
            outfile.open(logFileName);
            outfile << "runtime log #" << logFileName << ". This is a Test." << std::endl;
            outfile.close();
            YRLOG_DEBUG("Creating log files for: {}", logFileName);
        }
    }

    void SetUp() override
    {
        (void)litebus::os::Rmdir(LOG_BASE_DIR);
        testActor_ = std::make_shared<RuntimeManagerTestActor>(GenerateRandomName("randomRuntimeManagerTestActor"));
        litebus::Spawn(testActor_, true);

        helper_ =
            std::make_shared<LogManagerActorHelper>(GenerateRandomName("LogManagerActorHelper"), testActor_->GetAID());
        litebus::Spawn(helper_);
    }

    void TearDown() override
    {
        litebus::Terminate(helper_->GetAID());
        litebus::Await(helper_->GetAID());

        litebus::Terminate(testActor_->GetAID());
        litebus::Await(testActor_->GetAID());
        helper_ = nullptr;
        testActor_ = nullptr;
    }

protected:
    std::shared_ptr<LogManagerActorHelper> helper_;
    std::shared_ptr<RuntimeManagerTestActor> testActor_;
    std::string pythonRuntimeID_;
    std::string javaRuntimeID_;
    std::string cppRuntimeID_;
    std::string libRuntimeID_;
    std::string pythonRollingRuntimeID_;
    std::string cppRollingRuntimeID_;
    std::string cppRollingCompressionRuntimeID_;
};

TEST_F(LogManagerTest, EmptyLogDir)
{
    (void)litebus::os::Mkdir(LOG_BASE_DIR);
    const char *argv[] = { "./runtime-manager", "--runtime_logs_dir=/tmp/snuser/log", "--log_expiration_enable=true",
                           "--log_expiration_cleanup_interval=0", "--log_expiration_max_file_count=100" };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(0);
    });
}

TEST_F(LogManagerTest, LogFileExpirationNotExpired1)
{
    MockCreateLogs();
    const char *argv[] = { "./runtime-manager", "--runtime_logs_dir=/tmp/snuser/log", "--log_expiration_enable=true",
                           "--log_expiration_cleanup_interval=0", "--log_expiration_max_file_count=100" };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(5);
    });
}

TEST_F(LogManagerTest, SeparatedRuntimeStdLogParsing)
{
    const std::string uuidRuntimeID = "runtime-12345678-1234-4abc-8def-123456789abc";
    const std::string uuidRuntimeIDWithSuffix = uuidRuntimeID + "-0123456789ab";
    const std::string namedRuntimeID = "runtime-named-instance-alpha-001";

    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName(uuidRuntimeID + ".out", ""), uuidRuntimeID);
    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName(uuidRuntimeIDWithSuffix + ".err", ""), uuidRuntimeIDWithSuffix);
    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName(namedRuntimeID + ".out", ""), namedRuntimeID);
    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName(namedRuntimeID + ".err", ""), namedRuntimeID);
    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName("runtime-arbitrary-name.out", ""), "runtime-arbitrary-name");

    const std::vector<std::string> invalidLogFiles = {
        namedRuntimeID + ".log",
        namedRuntimeID + ".out.1",
        "runtime-.out",
        "runtime-invalid_name.out",
        "not-runtime-named-instance-alpha-001.err",
    };
    for (const auto &file : invalidLogFiles) {
        EXPECT_TRUE(helper_->GetRuntimeIDFromLogFileName(file, "").empty()) << file;
    }
}

TEST_F(LogManagerTest, BootstrapCmdStdLogParsing)
{
    const std::string uuidRuntimeID = "runtime-12345678-1234-4abc-8def-123456789abc";
    const std::string namedRuntimeID = "runtime-named-instance-alpha-001";

    // agent-dx bootstrap cmd log: {runtimeID}.std plus its logrotate archives
    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName(uuidRuntimeID + ".std", ""), uuidRuntimeID);
    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName(namedRuntimeID + ".std", ""), namedRuntimeID);
    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName(namedRuntimeID + ".std.1", ""), namedRuntimeID);
    EXPECT_EQ(helper_->GetRuntimeIDFromLogFileName(namedRuntimeID + ".std.4", ""), namedRuntimeID);

    const std::vector<std::string> invalidStdLogFiles = {
        "runtime-.std",
        "runtime-invalid_name.std",
        "not-runtime-named-instance-alpha-001.std",
        namedRuntimeID + ".std.log",
    };
    for (const auto &file : invalidStdLogFiles) {
        EXPECT_TRUE(helper_->GetRuntimeIDFromLogFileName(file, "").empty()) << file;
    }
}

TEST_F(LogManagerTest, RotateOversizeLogsWithCopytruncate)
{
    const std::string runtimeID = "runtime-12345678-1234-4abc-8def-123456789abc";
    const std::string namedRuntimeID = "runtime-named-instance-alpha-001";
    MockCreateSeparatedRuntimeStdLog(runtimeID, ".std", std::time(nullptr));
    MockCreateSeparatedRuntimeStdLog(namedRuntimeID, ".std", std::time(nullptr));
    // pad both logs past the rotate threshold
    for (const auto &id : { runtimeID, namedRuntimeID }) {
        auto logFile = litebus::os::Join(LOG_BASE_DIR, id + ".std");
        std::ofstream outfile(logFile, std::ios::app);
        outfile << std::string(6 * 1024 * 1024, 'x');
        outfile.close();
    }

    const char *argv[] = { "./runtime-manager", "--runtime_logs_dir=/tmp/snuser/log",
                           "--runtime_log_rotate_enable=true", "--runtime_log_rotate_max_size_mb=5",
                           "--runtime_log_rotate_max_files=2" };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE_FOR([=]() -> bool {
        auto filesOption = litebus::os::Ls(LOG_BASE_DIR);
        if (filesOption.IsNone()) {
            return false;
        }
        auto files = filesOption.Get();
        // Each oversize active log must gain .1 (copytruncate keeps the active
        // file, truncated to 0); a small log must not rotate.
        bool uuidRotated = false;
        bool namedRotated = false;
        for (const auto &file : files) {
            if (file == runtimeID + ".std.1") {
                uuidRotated = true;
            }
            if (file == namedRuntimeID + ".std.1") {
                namedRotated = true;
            }
        }
        return uuidRotated && namedRotated;
    }, 10000);


    // Active files must be truncated to 0 by copytruncate.
    for (const auto &id : { runtimeID, namedRuntimeID }) {
        auto logFile = litebus::os::Join(LOG_BASE_DIR, id + ".std");
        auto fileInfo = GetFileInfo(logFile);
        ASSERT_TRUE(fileInfo.IsSome());
        EXPECT_EQ(fileInfo.Get().st_size, 0) << logFile;
    }
}

TEST_F(LogManagerTest, RotateOversizeStdOutLog)
{
    const std::string runtimeID = "runtime-12345678-1234-4abc-8def-123456789abc";
    // RuntimeExecutor redirects stdout/stderr to {runtimeID}.out/.err; the
    // active .out must rotate just like .log/.std.
    MockCreateSeparatedRuntimeStdLog(runtimeID, ".out", std::time(nullptr));
    auto logFile = litebus::os::Join(LOG_BASE_DIR, runtimeID + ".out");
    std::ofstream outfile(logFile, std::ios::app);
    outfile << std::string(6 * 1024 * 1024, 'x');
    outfile.close();

    const char *argv[] = { "./runtime-manager", "--runtime_logs_dir=/tmp/snuser/log",
                           "--runtime_log_rotate_enable=true", "--runtime_log_rotate_max_size_mb=5",
                           "--runtime_log_rotate_max_files=2" };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE_FOR([=]() -> bool {
        auto filesOption = litebus::os::Ls(LOG_BASE_DIR);
        if (filesOption.IsNone()) {
            return false;
        }
        for (const auto &file : filesOption.Get()) {
            if (file == runtimeID + ".out.1") {
                return true;
            }
        }
        return false;
    }, 10000);

    // Active file must be truncated to 0 by copytruncate.
    auto fileInfo = GetFileInfo(logFile);
    ASSERT_TRUE(fileInfo.IsSome());
    EXPECT_EQ(fileInfo.Get().st_size, 0) << logFile;
}

TEST_F(LogManagerTest, RotateOversizeLogUnderDaemonUmask)
{
    const std::string runtimeID = "runtime-12345678-1234-4abc-8def-123456789abc";
    // Daemon runs with umask 0000; logrotate ignores a group/world-writable conf.
    const mode_t oldUmask = umask(0000);
    MockCreateSeparatedRuntimeStdLog(runtimeID, ".std", std::time(nullptr));
    auto logFile = litebus::os::Join(LOG_BASE_DIR, runtimeID + ".std");
    std::ofstream outfile(logFile, std::ios::app);
    outfile << std::string(6 * 1024 * 1024, 'x');
    outfile.close();

    const char *argv[] = { "./runtime-manager", "--runtime_logs_dir=/tmp/snuser/log",
                           "--runtime_log_rotate_enable=true", "--runtime_log_rotate_max_size_mb=5",
                           "--runtime_log_rotate_max_files=2" };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE_FOR([=]() -> bool {
        auto filesOption = litebus::os::Ls(LOG_BASE_DIR);
        if (filesOption.IsNone()) {
            return false;
        }
        for (const auto &file : filesOption.Get()) {
            if (file == runtimeID + ".std.1") {
                return true;
            }
        }
        return false;
    }, 10000);
    umask(oldUmask);

    // Active file must be truncated to 0 by copytruncate.
    auto fileInfo = GetFileInfo(logFile);
    ASSERT_TRUE(fileInfo.IsSome());
    EXPECT_EQ(fileInfo.Get().st_size, 0) << logFile;
}

TEST_F(LogManagerTest, RotateSkipsUndersizeLogs)
{
    const std::string runtimeID = "runtime-12345678-1234-4abc-8def-123456789abc";
    MockCreateSeparatedRuntimeStdLog(runtimeID, ".std", std::time(nullptr));

    const char *argv[] = { "./runtime-manager", "--runtime_logs_dir=/tmp/snuser/log",
                           "--runtime_log_rotate_enable=true", "--runtime_log_rotate_max_size_mb=5",
                           "--runtime_log_rotate_max_files=2" };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    // Wait longer than the rotate path could ever need; no archive may appear.
    EXPECT_AWAIT_TRUE_FOR([=]() -> bool {
        auto filesOption = litebus::os::Ls(LOG_BASE_DIR);
        if (filesOption.IsNone()) {
            return false;
        }
        for (const auto &file : filesOption.Get()) {
            if (file == runtimeID + ".std.1") {
                return false;
            }
        }
        return true;
    }, 2000);

    auto files = litebus::os::Ls(LOG_BASE_DIR);
    EXPECT_EQ(files.Get().size(), static_cast<size_t>(1));
}

TEST_F(LogManagerTest, ExpiredUuidRuntimeStdLogsAreRecycled)
{
    const std::string runtimeID = "runtime-12345678-1234-4abc-8def-123456789abc";
    MockCreateSeparatedRuntimeStdLog(runtimeID, ".out", std::time(nullptr) - 60);
    MockCreateSeparatedRuntimeStdLog(runtimeID, ".err", std::time(nullptr) - 60);
    EXPECT_CALL(*helper_, IsRuntimeActive(Eq(runtimeID))).Times(2).WillRepeatedly(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=30",
        "--log_expiration_time_threshold=10",
        "--log_expiration_max_file_count=0"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.IsSome() && files.Get().empty();
    });
}

TEST_F(LogManagerTest, ExpiredActiveNamedRuntimeStdLogIsKept)
{
    const std::string runtimeID = "runtime-named-instance-active-001";
    auto logFile = MockCreateSeparatedRuntimeStdLog(runtimeID, ".err", std::time(nullptr) - 60);
    std::atomic<bool> runtimeChecked = false;
    EXPECT_CALL(*helper_, IsRuntimeActive(Eq(runtimeID))).WillOnce([&runtimeChecked](const std::string &) {
        runtimeChecked = true;
        return litebus::Future<bool>(true);
    });

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=30",
        "--log_expiration_time_threshold=10",
        "--log_expiration_max_file_count=0"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([&]() -> bool {
        return runtimeChecked && litebus::os::ExistPath(logFile) && helper_->expiredLogQueue_->GetLogCount() == 0;
    });
}

TEST_F(LogManagerTest, ExpiredInactiveNamedRuntimeStdLogEntersQueue)
{
    const std::string runtimeID = "runtime-named-instance-inactive-001";
    auto logFile = MockCreateSeparatedRuntimeStdLog(runtimeID, ".out", std::time(nullptr) - 60);
    EXPECT_CALL(*helper_, IsRuntimeActive(Eq(runtimeID))).WillOnce(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=30",
        "--log_expiration_time_threshold=10",
        "--log_expiration_max_file_count=50"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        return litebus::os::ExistPath(logFile) && helper_->expiredLogQueue_->GetLogCount() == 1;
    });
    auto expiredLog = helper_->expiredLogQueue_->PopLogFile();
    ASSERT_NE(expiredLog, nullptr);
    EXPECT_EQ(expiredLog->GetRuntimeID(), runtimeID);
    EXPECT_EQ(expiredLog->GetFilePath(), logFile);
}

TEST_F(LogManagerTest, NamedRuntimeStdLogGcKeepsMaxFileCountNewestExpiredFiles)
{
    constexpr size_t maxFileCount = 50;
    constexpr size_t totalFileCount = maxFileCount + 2;
    const auto baseModificationTime = std::time(nullptr) - 1000;
    std::vector<std::string> logFiles;
    for (size_t i = 0; i < totalFileCount; ++i) {
        auto runtimeID = GenerateNamedRuntimeID(i);
        logFiles.emplace_back(
            MockCreateSeparatedRuntimeStdLog(runtimeID, ".out", baseModificationTime + static_cast<time_t>(i)));
    }
    EXPECT_CALL(*helper_, IsRuntimeActive(_)).Times(totalFileCount).WillRepeatedly(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=30",
        "--log_expiration_time_threshold=10",
        "--log_expiration_max_file_count=50"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.IsSome() && files.Get().size() == maxFileCount;
    });
    EXPECT_FALSE(litebus::os::ExistPath(logFiles[0]));
    EXPECT_FALSE(litebus::os::ExistPath(logFiles[1]));
    for (size_t i = 2; i < totalFileCount; ++i) {
        EXPECT_TRUE(litebus::os::ExistPath(logFiles[i])) << logFiles[i];
    }
}

TEST_F(LogManagerTest, InvalidSeparatedRuntimeStdLogsAreIgnored)
{
    const std::vector<std::pair<std::string, std::string>> invalidLogFiles = {
        { "runtime-named-instance-alpha-001", ".log" },
        { "runtime-invalid_name", ".out" },
        { "runtime-", ".err" },
        { "not-runtime-named-instance-alpha-001", ".out" },
    };
    std::vector<std::string> logFiles;
    for (const auto &[runtimeID, extension] : invalidLogFiles) {
        logFiles.emplace_back(MockCreateSeparatedRuntimeStdLog(runtimeID, extension, std::time(nullptr) - 60));
    }
    EXPECT_CALL(*helper_, IsRuntimeActive(_)).Times(0);

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=30",
        "--log_expiration_time_threshold=10",
        "--log_expiration_max_file_count=0"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.IsSome() && files.Get().size() == invalidLogFiles.size();
    });
    for (const auto &logFile : logFiles) {
        EXPECT_TRUE(litebus::os::ExistPath(logFile)) << logFile;
    }
}

TEST_F(LogManagerTest, LogFileExpirationNotExpired2)
{
    MockCreateLogs();
    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActive(_))
        .WillRepeatedly([javaRuntimeID(javaRuntimeID_)](const std::string &runtimeID) {
            if (runtimeID == javaRuntimeID) {
                return litebus::Future<bool>(false);
            }
            return litebus::Future<bool>(true);
        });

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=10",  // execute once in this ut case
        "--log_expiration_time_threshold=3",
        "--log_expiration_max_file_count=0",  // delete all expired log
        "--runtime_std_log_dir=instances"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // wait for log expiration
    helper_->ScanLogsRegularly();
    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(5); // java runtime log not deleted
    });
}

TEST_F(LogManagerTest, LogFileExpirationNotExpired3)
{
    MockCreateLogs();
    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActive(_))
        .WillRepeatedly([javaRuntimeID(javaRuntimeID_)](const std::string &runtimeID) {
            if (runtimeID == javaRuntimeID) {
                return litebus::Future<bool>(false);
            }
            return litebus::Future<bool>(true);
        });

    const char *argv[] = { "./runtime-manager",
                           "--runtime_logs_dir=/tmp/snuser/log",
                           "--log_expiration_enable=true",
                           "--log_expiration_cleanup_interval=10",  // execute once in this ut case
                           "--log_expiration_time_threshold=1",
                           "--log_expiration_max_file_count=10",
                           "--runtime_std_log_dir=instances"};
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // wait for log expiration
    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(5); // java runtime log not deleted
    });
}

TEST_F(LogManagerTest, LogFileExpirationExpired1)
{
    MockCreateLogs();
    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActive(_))
        .WillRepeatedly([javaRuntimeID(javaRuntimeID_)](const std::string &runtimeID) {
            if (runtimeID == javaRuntimeID) {
                return litebus::Future<bool>(false);
            }
            return litebus::Future<bool>(true);
        });

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=10",  // execute once in this ut case
        "--log_expiration_time_threshold=1",
        "--log_expiration_max_file_count=0",  // delete all expired log
        "--runtime_std_log_dir=instances"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // wait for log expiration
    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(4); // java runtime log deleted
    });
}

TEST_F(LogManagerTest, LogFileExpirationExpired2)
{
    MockCreateLogs();
    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActive(_)).WillRepeatedly(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=10",  // execute once in this ut case
        "--log_expiration_time_threshold=1",
        "--log_expiration_max_file_count=2",  // keep 2 expired log
        "--runtime_std_log_dir=instances"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // wait for log expiration
    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(4)
               || files.Get().size()
                      == static_cast<size_t>(3);  // 2(except) + 2, when left is dir and inner log file is 3
    });
}

TEST_F(LogManagerTest, LogFileExpirationExpired3)
{
    // mock runtime logs
    MockCreateLibRuntimeLogs();
    MockCreateCppRuntimeLogs2();

    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActive(_)).WillRepeatedly(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=10",  // execute once in this ut case
        "--log_expiration_time_threshold=0",
        "--log_expiration_max_file_count=0"  // delete all expired log
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // wait for log expiration
    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(0);
    });
}

TEST_F(LogManagerTest, LogFileExpirationExpiredAsync)
{
    MockCreateLogs();

    // Set runtime inActive
    litebus::Promise<bool> javaPromise;
    litebus::Promise<bool> cppPromise;
    EXPECT_CALL(*helper_, IsRuntimeActive(_))
        .WillRepeatedly([javaRuntimeID(javaRuntimeID_), cppRuntimeID(cppRuntimeID_), &javaPromise,
                         &cppPromise](const std::string &runtimeID) {
            if (runtimeID == javaRuntimeID) {
                std::cout << "Checking Java runtime status..." << std::endl;
                return javaPromise.GetFuture();
            } else if (runtimeID == cppRuntimeID) {
                std::cout << "Checking C++ runtime status..." << std::endl;
                return cppPromise.GetFuture();
            }
            return litebus::Future<bool>(true);
        });

    // async IsRuntimeActive
    std::thread javaThread([&javaPromise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        javaPromise.SetValue(false);
    });

    std::thread cppThread([&cppPromise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        cppPromise.SetValue(false);
    });

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=1",  // execute once in this ut case
        "--log_expiration_time_threshold=0",
        "--log_expiration_max_file_count=0",  // delete all expired log
        "--runtime_std_log_dir=instances"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);
    helper_->ScanLogsRegularly();

    // wait async threads done
    javaThread.join();
    cppThread.join();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(3);  // cpp and java runtime log deleted
    });
}

/*
 * Test Steps:
 * 1. Simulate runtime log generation: Create multiple logs, named according to runtime logs and compressed files
 * 2. Set log aging deletion configuration
 * 3. In the waiting time, without expiration
 * 4. Call the CleanLogs function to check if the unexpired file has been deleted
 */
TEST_F(LogManagerTest, LogFileExpirationComplexCaseWithRollingCompressionTest)
{
    MockCreateLogs();

    MockCreateCppRuntimeRollingLogs();
    MockCreatePythonRuntimeRollingLogs();
    MockCreateCppRuntimeRollingLogsWithCompression();

    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActive(_)).WillRepeatedly(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=1",
        "--log_expiration_time_threshold=1",
        "--log_expiration_max_file_count=1",  // keep 1 expired log
        "--runtime_std_log_dir=instances"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));  // wait for log expiration
    helper_->ScanLogsRegularly();
    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(3);  // expect 'exception' and 'instances' dir + 1
    });
    auto files = litebus::os::Ls(LOG_BASE_DIR);
}

TEST_F(LogManagerTest, DsClientLogFileExpirationNotExpired)
{
    MockCreateDsClientLogs();

    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActiveByPid(_)).WillRepeatedly(Return(true));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=10",  // execute once in this ut case
        "--log_expiration_time_threshold=1",
        "--log_expiration_max_file_count=0",  // delete all expired log
        "--runtime_std_log_dir=instances"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // wait for log expiration
    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(9); // nothing deleted
    });
}

TEST_F(LogManagerTest, DsClientLogFileExpirationExpired)
{
    MockCreateDsClientLogs();

    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActiveByPid(_)).WillRepeatedly(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=10",  // execute once in this ut case
        "--log_expiration_time_threshold=1",
        "--log_expiration_max_file_count=0",  // delete all expired log
        "--runtime_std_log_dir=instances"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));  // wait for log expiration
    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(0); // everything is deleted
    });
}

TEST_F(LogManagerTest, AcquireLogPrefix)
{
    auto pid = std::to_string(getpid());
    // 0. mock actor_->logCount_ > MaxCount
    helper_->logCount_ = 999998;
    auto res1 = helper_->AcquireLogPrefix("runtime1");
    auto exp1 = "YR_" + pid + "_000000";
    EXPECT_EQ(res1.Get(), exp1);
    EXPECT_EQ(helper_->logPrefix2RuntimeID_[exp1], "runtime1");
    EXPECT_EQ(helper_->runtimeID2LogPrefix_["runtime1"], exp1);

    // 1. get and generate
    auto res2 = helper_->AcquireLogPrefix("runtime2");
    auto res3 = helper_->AcquireLogPrefix("runtime3");

    auto exp2 = "YR_" + pid + "_000001";
    auto exp3 = "YR_" + pid + "_000002";
    auto exp4 = "YR_" + pid + "_000003";

    EXPECT_EQ(res2.Get(), exp2);
    EXPECT_EQ(res3.Get(), exp3);
    EXPECT_EQ(helper_->logPrefix2RuntimeID_[exp2], "runtime2");
    EXPECT_EQ(helper_->runtimeID2LogPrefix_["runtime2"], exp2);
    EXPECT_EQ(helper_->logPrefix2RuntimeID_[exp3], "runtime3");
    EXPECT_EQ(helper_->runtimeID2LogPrefix_["runtime3"], exp3);

    // 2. get from queue and then generate
    helper_->ReleaseLogPrefix("runtime2");
    helper_->ReleaseLogPrefix("runtime3");

    res1 = helper_->AcquireLogPrefix("runtime4");
    res2 = helper_->AcquireLogPrefix("runtime5");

    EXPECT_EQ(res1.Get(), exp3);
    EXPECT_EQ(res2.Get(), exp2);
    EXPECT_EQ(helper_->logPrefix2RuntimeID_[exp3], "runtime4");
    EXPECT_EQ(helper_->runtimeID2LogPrefix_["runtime4"], exp3);
    EXPECT_EQ(helper_->logPrefix2RuntimeID_[exp2], "runtime5");
    EXPECT_EQ(helper_->runtimeID2LogPrefix_["runtime5"], exp2);

    res2 = helper_->AcquireLogPrefix("runtime6");
    EXPECT_EQ(res2.Get(), exp4);
    EXPECT_EQ(helper_->logPrefix2RuntimeID_[exp4], "runtime6");
    EXPECT_EQ(helper_->runtimeID2LogPrefix_["runtime6"], exp4);
}

TEST_F(LogManagerTest, ReleaseLogPrefix)
{
    auto pid = std::to_string(getpid());

    auto res1 = helper_->AcquireLogPrefix("runtime1");
    auto exp1 = "YR_" + pid + "_000001";
    EXPECT_EQ(res1.Get(), exp1);
    EXPECT_EQ(helper_->logPrefix2RuntimeID_[exp1], "runtime1");
    EXPECT_EQ(helper_->runtimeID2LogPrefix_["runtime1"], exp1);

    // try to release failed
    helper_->ReleaseLogPrefix("runtime2");
    EXPECT_EQ(helper_->logPrefix2RuntimeID_[exp1], "runtime1");
    EXPECT_EQ(helper_->runtimeID2LogPrefix_["runtime1"], exp1);

    // try to release success
    helper_->ReleaseLogPrefix("runtime1");
    EXPECT_EQ(helper_->logPrefix2RuntimeID_.find(exp1), helper_->logPrefix2RuntimeID_.end());
    EXPECT_EQ(helper_->runtimeID2LogPrefix_.find("runtime1"), helper_->runtimeID2LogPrefix_.end());
    EXPECT_EQ(helper_->logPrefixDequeue_.front(), exp1);

    // try to release but not match
    helper_->runtimeID2LogPrefix_["runtime2"] = "log1111";
    helper_->logPrefix2RuntimeID_["log2222"] = "runtime2";
    helper_->runtimeID2LogPrefix_["runtime3"] = "log2222";

    helper_->ReleaseLogPrefix("runtime2");
    EXPECT_TRUE(helper_->logPrefix2RuntimeID_.find("log2222") != helper_->logPrefix2RuntimeID_.end());

    helper_->ReleaseLogPrefix("runtime3");
    EXPECT_TRUE(helper_->logPrefix2RuntimeID_.find("log2222") != helper_->logPrefix2RuntimeID_.end());
}

TEST_F(LogManagerTest, AddLogAndPopLogTest)
{
    size_t toDeleteCount = 200;
    for (size_t i = 0; i < toDeleteCount; i++) {
        helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/tmp/xxx_"+std::to_string(i), 1756216109, false),
                                              false);
    }
    std::vector<std::shared_ptr<RuntimeLogFile>> toBeDeleteFiles;
    for (size_t i = 0; i < toDeleteCount; ++i) {
        auto file = helper_->expiredLogQueue_->PopLogFile();
        if (file == nullptr){
            break;
        }
        toBeDeleteFiles.emplace_back(file);
    }
    EXPECT_TRUE(toBeDeleteFiles.size() == toDeleteCount);

}

TEST_F(LogManagerTest, AddAndPopLogTest)
{
    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/tmp/xxx_0", 1756216109, false),
                                          true);
    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/java/file_"+std::to_string(0), 1756216110, false),
                                          true);
    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/java/file_"+std::to_string(1), 1756216110, false),
                                          true);
    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/java/file_"+std::to_string(2), 1756216110, false),
                                          true);
    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/javaDir/", 1756216110, true),
                                          true);
    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/tmp/xxx_100", 1756216111, false),
                                          true);
    std::vector<bool> expectDir = {false, false, false, false, true, false};
    std::vector<std::string> expectStr = {"/tmp/xxx_0", "/java/file_", "/java/file_", "/java/file_", "/javaDir/", "/tmp/xxx_100",};
    for (size_t i = 0; i < 6; ++i) {
        auto file = helper_->expiredLogQueue_->PopLogFile();
        EXPECT_TRUE(file->GetFilePath().substr(0, expectStr[i].size()) == expectStr[i]);
        EXPECT_EQ(file->IsDir(), expectDir[i]);
    }
    ASSERT_TRUE(helper_->expiredLogQueue_->PopLogFile() == nullptr);

    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/tmp/xxx_1", 1756216109, false), true);
    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/tmp/xxx_2", 1756216109, false), true);
    helper_->expiredLogQueue_->AddLogFile(std::make_shared<RuntimeLogFile>("", "/tmp/xxx_3", 1756216109, false), true);
    helper_->expiredLogQueue_->Reset();
    ASSERT_TRUE(helper_->expiredLogQueue_->PopLogFile() == nullptr);
}

TEST_F(LogManagerTest, RecycleReuseLogTest)
{
    MockCreatePrefixLogs();
    MockCreateDsClientLogs();
    MockCreateManyPrefixLogs(200);
    helper_->logReuse_ = true;

    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActive(_)).WillRepeatedly(Return(false));
    EXPECT_CALL(*helper_, IsRuntimeActiveByPid(_)).WillRepeatedly(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=10",
        "--log_expiration_time_threshold=0",
        "--log_expiration_max_file_count=0"  // delete all expired log
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // wait for log expiration
    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(0);
    });
    auto files = litebus::os::Ls(LOG_BASE_DIR);
}

TEST_F(LogManagerTest, RecycleReuseLogWhileReusingTest)
{
    MockCreatePrefixLogs();
    MockCreateDsClientLogs();
    MockCreateManyPrefixLogs(200);
    helper_->logReuse_ = true;
    helper_->logPrefix2RuntimeID_["YR_123_000003"] = "runtime3";
    helper_->logPrefix2RuntimeID_["YR_123_000002"] = "runtime2";

    // Set runtime inActive
    EXPECT_CALL(*helper_, IsRuntimeActive(_))
        .WillRepeatedly([](const std::string &runtimeID) {
            if (runtimeID == "runtime3") {
                return litebus::Future<bool>(true);
            }
            return litebus::Future<bool>(false);
        });
    EXPECT_CALL(*helper_, IsRuntimeActiveByPid(_)).WillOnce(Return(true)).WillRepeatedly(Return(false));

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=30",  // execute once in this ut case
        "--log_expiration_time_threshold=0",
        "--log_expiration_max_file_count=3"  // reverse 3
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // wait for log expiration
    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE_FOR([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(6); // 2 reuse log + 1 ds log + reverse 3
    }, 5000);
    auto files = litebus::os::Ls(LOG_BASE_DIR);
}

TEST_F(LogManagerTest, RecycleReuseLogWhileReusingAsyncTest)
{
    MockCreatePrefixLogs();
    MockCreateDsClientLogs();
    MockCreateManyPrefixLogs(200);
    helper_->logReuse_ = true;
    helper_->logPrefix2RuntimeID_["YR_123_000003"] = "runtime3";
    helper_->logPrefix2RuntimeID_["YR_123_000002"] = "runtime2";

    // Set runtime inActive
    litebus::Promise<bool> promise1;
    litebus::Promise<bool> promise2;
    EXPECT_CALL(*helper_, IsRuntimeActive(_))
        .WillRepeatedly([javaRuntimeID(javaRuntimeID_), cppRuntimeID(cppRuntimeID_), &promise1,
                         &promise2](const std::string &runtimeID) {
            if (runtimeID == "runtime3") {
                return promise1.GetFuture();
            } else if (runtimeID == "runtime2") {
                return promise2.GetFuture();
            }
            return litebus::Future<bool>(true);
        });
    EXPECT_CALL(*helper_, IsRuntimeActiveByPid(_)).WillOnce(Return(true)).WillRepeatedly(Return(false));


    // async IsRuntimeActive
    std::thread runtimeThread1([&promise1]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        promise1.SetValue(true);
    });

    std::thread runtimeThread2([&promise2]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        promise2.SetValue(false);
    });

    const char *argv[] = {
        "./runtime-manager",
        "--runtime_logs_dir=/tmp/snuser/log",
        "--log_expiration_enable=true",
        "--log_expiration_cleanup_interval=30",
        "--log_expiration_time_threshold=0",
        "--log_expiration_max_file_count=3"  // reverse 3
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(std::size(argv), argv);
    helper_->SetConfig(flags);
    helper_->ScanLogsRegularly();
    // wait async threads done
    runtimeThread1.join();
    runtimeThread2.join();

    EXPECT_AWAIT_TRUE_FOR([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(6); // 2 reuse log + 1 ds log + reverse 3
    }, 5000);
    auto files = litebus::os::Ls(LOG_BASE_DIR);
}
}
