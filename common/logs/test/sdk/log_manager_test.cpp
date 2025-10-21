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

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

#include "logs/sdk/log_handler.h"
#include "logs/sdk/log_param_parser.h"
#include "logs/api/provider.h"
#include "sdk/fileutils.h"
#define private public
#include "logs/sdk/log_manager.h"

namespace observability::test::sdk {
using namespace observability::sdk::logs;

const std::string NODE_NAME = "node";
const std::string MODEL_NAME = "model";
const std::string FILEPATH_NOT_EXIST = "/tmp/filepath_not_exist";
const std::string LOG_CONFIG_JSON = R"(
{
  "level": "DEBUG",
  "rolling": {
    "maxsize": 100,
    "maxfiles": 1
  },
  "async": {
    "logBufSecs": 30,
    "maxQueueSize": 1048510,
    "threadCount": 1
  },
  "alsologtostderr": true
}
)";

class LoggerManagerTest : public ::testing::Test {
protected:
    static void SetUpTestCase()
    {
        param_ = GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false, NODE_NAME + MODEL_NAME);
    }
    static void TearDownTestCase()
    {
    }

    inline static api::logs::LogParam param_;
    std::shared_ptr<LogManager> logManager_;
};

TEST_F(LoggerManagerTest, CompressFileTest)
{
    param_.logDir = "/tmp/logs";
    std::filesystem::remove_all(param_.logDir.c_str());
    if (mkdir(param_.logDir.c_str(), 0750) == -1) {
        std::cerr << "Failed to create dir：" << strerror(errno) << std::endl;
    }
    auto logpath = param_.logDir + "/" + param_.fileNamePattern + ".0.log";

    std::ofstream outfile;
    outfile.open(logpath.c_str());
    outfile << "1";
    outfile.close();
    LogRollingCompress(param_);

    auto filepath = param_.logDir + "/" + param_.fileNamePattern + ".1.log.gz";
    outfile.open(filepath.c_str());
    outfile << "1";
    outfile.close();
    DoLogFileRolling(param_);

    param_.retentionDays = 0;
    DoLogFileRolling(param_);

    std::filesystem::remove_all(param_.logDir.c_str());
}
TEST_F(LoggerManagerTest, FileExist)
{
    bool exist = FileExist(FILEPATH_NOT_EXIST, 0);
    EXPECT_FALSE(exist);
}

TEST_F(LoggerManagerTest, FileSize)
{
    auto size = FileSize(FILEPATH_NOT_EXIST);
    EXPECT_EQ(size, size_t(0));
}

TEST_F(LoggerManagerTest, GetFileModifiedTime)
{
    int64_t timestamp = 0;
    GetFileModifiedTime(FILEPATH_NOT_EXIST, timestamp);
    EXPECT_EQ(timestamp, 0);

    std::ofstream MyFile("/tmp/compress_util.txt");
    MyFile << "util compress";
    MyFile.close();

    GetFileModifiedTime("/tmp/compress_util.txt", timestamp);
    ASSERT_NE(timestamp, 0);
    std::remove("/tmp/compress_util.txt");
}

TEST_F(LoggerManagerTest, Glob)
{
    std::vector<std::string> files;
    Glob(FILEPATH_NOT_EXIST, files);
    EXPECT_EQ(static_cast<int>(files.size()), 0);
}

TEST_F(LoggerManagerTest, CompressFile)
{
    int compress = CompressFile(FILEPATH_NOT_EXIST, "dest");
    EXPECT_EQ(compress, -1);
}

TEST_F(LoggerManagerTest, CompressDir)
{
    std::ofstream outfile;
    std::string filepath = "/tmp/temp.log";
    outfile.open(filepath.c_str());
    outfile << "1";
    outfile.close();

    int compress = CompressFile(filepath, "/tmp/");
    EXPECT_EQ(compress, -1);

    DeleteFile("/tmp/");
}

TEST_F(LoggerManagerTest, StartRollingCompress)
{
    auto param = GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false, NODE_NAME + MODEL_NAME);
    param.compressEnable = false;
    logManager_ = std::make_shared<LogManager>(param);
    logManager_->StartRollingCompress([](observability::api::logs::LogParam param){
        std::cout << param.fileNamePattern << std::endl;
    });
    EXPECT_TRUE(logManager_->state_ == LogManager::State::INITED);

    param.compressEnable = true;
    logManager_ = std::make_shared<LogManager>(param);
    logManager_->state_ = LogManager::State::STOPPED;
    logManager_->StartRollingCompress([](observability::api::logs::LogParam param){
        std::cout << param.fileNamePattern << std::endl;
    });
    EXPECT_TRUE(logManager_->state_ == LogManager::State::STOPPED);

    logManager_->state_ = LogManager::State::INITED;
    logManager_->interval_ = 1;
    logManager_->StartRollingCompress([&](observability::api::logs::LogParam param){
        std::cout << param.fileNamePattern << std::endl;
        logManager_->rcCond_.notify_all();
        logManager_->state_ = LogManager::State::STOPPED;
    });
    logManager_->rollingCompressThread_.join();
    EXPECT_TRUE(logManager_->state_ == LogManager::State::STOPPED);
}

TEST_F(LoggerManagerTest, StopRollingCompress)
{
    auto param = GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false, NODE_NAME + MODEL_NAME);
    logManager_ = std::make_shared<LogManager>(param);

    logManager_->state_ = LogManager::State::INITED;
    logManager_->StopRollingCompress();
    EXPECT_TRUE(logManager_->state_ == LogManager::State::INITED);
}
}  // namespace observability::test::sdk