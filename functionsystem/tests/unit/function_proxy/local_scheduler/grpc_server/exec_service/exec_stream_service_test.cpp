/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#include "function_proxy/local_scheduler/grpc_server/exec_service/exec_stream_service.h"

namespace functionsystem {
namespace test {

class ExecStreamServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_ = std::make_unique<ExecStreamService>();
    }

    void TearDown() override {
        if (service_) {
            service_->CloseAllSessions();
        }
    }

protected:
    std::unique_ptr<ExecStreamService> service_;
};

// Test service creation
TEST_F(ExecStreamServiceTest, ServiceCreation) {
    ASSERT_NE(service_, nullptr);
    EXPECT_EQ(service_->GetActiveSessionCount(), 0);
}

}  // namespace test
}  // namespace functionsystem
