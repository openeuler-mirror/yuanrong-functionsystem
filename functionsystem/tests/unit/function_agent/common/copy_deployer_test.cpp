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
#include "function_agent/code_deployer/copy_deployer.h"
#include "function_agent/common/constants.h"

namespace functionsystem::test {

class CopyDeployerTest : public testing::Test {

};

/**
 * Feature: DeployWithPackage
 * Description: deploy without system function
 * Steps:
 * 1、deploy with not existed package
 * 2、deploy with not existed package
 * 3、clear package
 * Expectation:
 * 1、deploy failed
 * 2、deploy success
 */
TEST_F(CopyDeployerTest, DeployWithPackage)
{
    std::string filePath = "/tmp/test-deployDir-123";
    litebus::os::Rmdir(filePath);
    auto deployer = std::make_shared<functionsystem::function_agent::CopyDeployer>();
    deployer->SetBaseDeployDir("/tmp");
    auto request = std::make_shared<messages::DeployRequest>();
    request->mutable_deploymentconfig()->set_storagetype(function_agent::COPY_STORAGE_TYPE);
    request->mutable_deploymentconfig()->set_deploydir(filePath);
    request->mutable_deploymentconfig()->set_objectid(filePath);
    auto res = deployer->Deploy(request);
    EXPECT_TRUE(res.status.IsError());
    litebus::os::Mkdir(filePath);
    res = deployer->Deploy(request);
    auto dstPath = res.destination;
    EXPECT_TRUE(deployer->IsDeployed(dstPath, false));
    deployer->Clear(dstPath, "");
    EXPECT_FALSE(litebus::os::ExistPath(dstPath));
}
}