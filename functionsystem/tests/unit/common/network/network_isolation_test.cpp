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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>

#include "common/network/network_isolation.h"
#include "mocks/mock_exec_utils.h"

namespace functionsystem::test {
using ::testing::_;
using ::testing::Return;

class NetworkIsolationTest : public ::testing::Test {
};

class IpsetIpv4NetworkIsolationTest : public NetworkIsolationTest {
public:
    static void SetUpTestCase()
    {
    }

    static void TearDownTestCase()
    {
    }

    void SetUp()
    {
        commandRunner_ = std::make_shared<MockCommandRunner>();
        isolation_ = std::make_shared<IpsetIpv4NetworkIsolation>("test_ipset");
        isolation_->SetCommandRunner(commandRunner_);
    }

    void TearDown()
    {
    }

protected:
    std::shared_ptr<IpsetIpv4NetworkIsolation> isolation_;
    std::shared_ptr<MockCommandRunner> commandRunner_;
};

TEST_F(IpsetIpv4NetworkIsolationTest, IpsetExists)
{
    bool exists = isolation_->IsIpsetExist();
    EXPECT_FALSE(exists);
}

}  // namespace functionsystem::test