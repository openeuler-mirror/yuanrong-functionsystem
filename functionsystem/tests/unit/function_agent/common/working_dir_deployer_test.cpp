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

#include "common/utils/path.h"
#include "function_agent/code_deployer/working_dir_deployer.h"

namespace functionsystem::test {

class WorkingDirDeployerTest : public testing::Test {
protected:
    void SetUp() override
    {
        litebus::os::Rmdir(testDir_);
        EXPECT_TRUE(litebus::os::Mkdir(testDir_).IsNone());
    }

    void TearDown() override
    {
        litebus::os::Rmdir(testDir_);
    }

    const std::string testDir_ = "/tmp/working-dir-deployer-test";
};

TEST_F(WorkingDirDeployerTest, PathSchemeAndRawPathResolveToSameDestination)
{
    functionsystem::function_agent::WorkingDirDeployer deployer;
    auto rawDestination = deployer.GetDestination("/tmp/deploy-root", testDir_, "app-id");
    auto schemeDestination = deployer.GetDestination("/tmp/deploy-root", "path://" + testDir_, "app-id");

    EXPECT_EQ(rawDestination, testDir_);
    EXPECT_EQ(schemeDestination, testDir_);
}

TEST_F(WorkingDirDeployerTest, DeployTreatsPathSchemeAsDelegatedDirectory)
{
    functionsystem::function_agent::WorkingDirDeployer deployer;
    auto request = std::make_shared<messages::DeployRequest>();
    request->mutable_deploymentconfig()->set_bucketid("path://" + testDir_);
    request->mutable_deploymentconfig()->set_objectid("app-id");
    request->mutable_deploymentconfig()->set_deploydir("/tmp/deploy-root");

    auto result = deployer.Deploy(request);
    EXPECT_TRUE(result.status.IsOk()) << result.status.ToString();
    EXPECT_EQ(result.destination, testDir_);
}

}  // namespace functionsystem::test

