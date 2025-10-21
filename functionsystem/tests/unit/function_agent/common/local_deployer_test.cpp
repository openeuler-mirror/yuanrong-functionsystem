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
#include "function_agent/code_deployer/local_deployer.h"
#include "function_agent/common/constants.h"

namespace functionsystem::test {

class LocalDeployerTest : public testing::Test {

};

/**
 * Feature: DeployWithNotExistedPackage
 * Description: deploy without system function
 * Steps:
 * does not have system function zip file
 * Expectation:
 * deploy success
 */
TEST_F(LocalDeployerTest, DeployWithNotExistedPackage)
{
    std::string filePath = "/tmp/test-deployDir";
    if (!litebus::os::ExistPath(filePath)) {
        EXPECT_TRUE(litebus::os::Mkdir(filePath).IsNone());
    }

    auto deployer = std::make_shared<functionsystem::function_agent::LocalDeployer>();
    auto request = std::make_shared<messages::DeployRequest>();
    request->mutable_deploymentconfig()->set_storagetype(function_agent::LOCAL_STORAGE_TYPE);
    request->mutable_deploymentconfig()->set_deploydir(filePath);

    auto res = deployer->Deploy(request);
    EXPECT_TRUE(res.destination == filePath);
    deployer->Clear(filePath, "objectKey");
    EXPECT_TRUE(litebus::os::Rmdir(filePath).IsNone());
}

/**
 * Feature: DeployWithoutDeployDir
 * Description: deploy without deployDir
 * Steps:
 * request does not contain deployDir
 * Expectation:
 * deploy success
 */
TEST_F(LocalDeployerTest, DeployWithoutDeployDir)
{
    std::string filePath = "/tmp/test-deployDir";
    if (litebus::os::ExistPath(filePath)) {
        EXPECT_TRUE(litebus::os::Rmdir(filePath).IsNone());
    }

    auto deployer = std::make_shared<functionsystem::function_agent::LocalDeployer>();
    auto request = std::make_shared<messages::DeployRequest>();
    request->mutable_deploymentconfig()->set_storagetype(function_agent::LOCAL_STORAGE_TYPE);
    request->mutable_deploymentconfig()->set_deploydir(filePath);

    auto res = deployer->Deploy(request);
    EXPECT_TRUE(res.destination == filePath);

    EXPECT_TRUE(deployer->Clear("filepath", "objectKey"));
}

}