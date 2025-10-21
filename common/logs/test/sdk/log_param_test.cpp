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

#include "logs/sdk/log_param_parser.h"

#include <gtest/gtest.h>
#include <regex>

namespace observability::test::sdk {

class LogParamTest : public ::testing::Test {};

TEST_F(LogParamTest, GetLogParam)
{
    std::string confJson =
        "{\"filepath\": \"/tmp\",\"level\": \"DEBUG\", \"rolling\": {\"maxsize\": 40,\"maxfiles\": "
        "50,\"retentionDays\": 30}, \"async\": {\"logBufSecs\": 30,\"maxQueueSize\": 51200,\"threadCount\": 1}, "
        "\"alsologtostderr\": false}";
    std::cout << "confJson: " << confJson << std::endl;
    auto param = ::observability::sdk::logs::GetLogParam(confJson, "node", "mode");

    EXPECT_EQ(param.logDir, "/tmp");
    EXPECT_EQ(param.logLevel, "DEBUG");
    EXPECT_EQ(param.maxSize, 40);
    EXPECT_EQ(param.maxFiles, 50);
    EXPECT_EQ(param.retentionDays, 30);
    EXPECT_EQ(param.alsoLog2Std, false);
    EXPECT_EQ(param.stdLogLevel, "ERROR");
}

TEST_F(LogParamTest, GetLogFile)
{
    std::string confJson =
        "{\"filepath\": \"/tmp\",\"level\": \"DEBUG\", \"rolling\": {\"maxsize\": 40,\"maxfiles\": "
        "50,\"retentionDays\": 30}, \"async\": {\"logBufSecs\": 30,\"maxQueueSize\": 51200,\"threadCount\": 1}, "
        "\"alsologtostderr\": false}";
    std::cout << "confJson: " << confJson << std::endl;
    auto param = ::observability::sdk::logs::GetLogParam(confJson, "node", "mode", false, "functionName@serviceID@version@podName@time#logGroupId#logStreamId");
    auto fileName = ::observability::sdk::logs::GetLogFile(param);
    EXPECT_EQ(fileName, "/tmp/functionName@serviceID@version@podName@time#logGroupId#logStreamId.log");

    param = ::observability::sdk::logs::GetLogParam(confJson, "node", "mode", false);
    fileName = ::observability::sdk::logs::GetLogFile(param);
    EXPECT_EQ(fileName, "/tmp/node-mode.log");

    param = ::observability::sdk::logs::GetLogParam(confJson, "node", "mode", true);
    fileName = ::observability::sdk::logs::GetLogFile(param);
    std::cout << fileName << std::endl;
    std::regex fileNamePattern(R"(/tmp/node-mode-\d{14}\.log$)");
    EXPECT_TRUE(std::regex_match(fileName, fileNamePattern));
}
}  // namespace observability::test::sdk