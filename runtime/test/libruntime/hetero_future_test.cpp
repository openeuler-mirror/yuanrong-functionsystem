/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock/mock_datasystem_client.h"
#include "src/libruntime/libruntime.h"
#include "src/utility/logger/logger.h"
#define private public
#include "src/libruntime/heterostore/hetero_future.h"

using namespace testing;
using namespace YR::Libruntime;
using namespace YR::utility;

namespace YR {
namespace Libruntime {
YR::Libruntime::AsyncResult ConverDsStatusToAsyncRes(datasystem::Status dsStatus);
}  // namespace Libruntime
}  // namespace YR

namespace YR {
namespace test {
class HeteroFutureTest : public testing::Test {
public:
    HeteroFutureTest(){};
    ~HeteroFutureTest(){};
    void SetUp() override
    {
        Mkdir("/tmp/log");
        LogParam g_logParam = {
            .logLevel = "DEBUG",
            .logDir = "/tmp/log",
            .nodeName = "test-runtime",
            .modelName = "test",
            .maxSize = 100,
            .maxFiles = 1,
            .logFileWithTime = false,
            .logBufSecs = 30,
            .maxAsyncQueueSize = 1048510,
            .asyncThreadCount = 1,
            .alsoLog2Stderr = true,
        };
        InitLog(g_logParam);
    }
    void TearDown() override
    {
        heteroFuture_.reset();
    }

    std::shared_ptr<HeteroFuture> heteroFuture_;
};

TEST_F(HeteroFutureTest, TestConverDsStatusToAsyncRes)
{
    datasystem::Status dsStatus1;
    auto result_1 = ConverDsStatusToAsyncRes(dsStatus1);
    ASSERT_EQ(result_1.error.OK(), true);
    datasystem::Status dsStatus2(datasystem::StatusCode::K_DUPLICATED, "err");
    auto result_2 = ConverDsStatusToAsyncRes(dsStatus2);
    ASSERT_EQ(result_2.error.Code(), YR::Libruntime::ErrorCode::ERR_PARAM_INVALID);
}

} // namespace test
} // namepsace YR