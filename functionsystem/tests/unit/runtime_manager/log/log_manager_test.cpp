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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <fcntl.h>
#include <regex>

#include "utils/os_utils.hpp"
#include "logs/logging.h"
#include "../manager/runtime_manager_test_actor.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"

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
    explicit LogManagerActorHelper(const std::string &name, const litebus::AID &runtimeManagerAID)
        : LogManagerActor(name, runtimeManagerAID){};
    MOCK_METHOD(litebus::Future<bool>, IsRuntimeActive, (const std::string &runtimeID), (const));
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
    flags.ParseFlags(5, argv);
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
    flags.ParseFlags(5, argv);
    helper_->SetConfig(flags);

    helper_->ScanLogsRegularly();

    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(5);
    });
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
    flags.ParseFlags(7, argv);
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
    flags.ParseFlags(7, argv);
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
    flags.ParseFlags(7, argv);
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
    flags.ParseFlags(7, argv);
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
    flags.ParseFlags(6, argv);
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
    flags.ParseFlags(7, argv);
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
        "--log_expiration_cleanup_interval=10",  // execute once in this ut case
        "--log_expiration_time_threshold=1",
        "--log_expiration_max_file_count=1",  // keep 1 expired log
        "--runtime_std_log_dir=instances"
    };
    runtime_manager::Flags flags;
    flags.ParseFlags(7, argv);
    helper_->SetConfig(flags);

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));  // wait for log expiration
    helper_->ScanLogsRegularly();
    EXPECT_AWAIT_TRUE([=]() -> bool {
        auto files = litebus::os::Ls(LOG_BASE_DIR);
        return files.Get().size() == static_cast<size_t>(3);  // expect 'exception' and 'instances' dir + 1
    });
}
}