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

#include "runtime_manager/executor/sandbox/sandbox_command_utils.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace functionsystem::runtime_manager {
namespace {

std::shared_ptr<messages::StartInstanceRequest> MakeRequest(
    const std::string &entrypoint, const std::string &cmd)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    auto *bootstrap = request->mutable_runtimeinstanceinfo()->mutable_bootstrapconfig();
    bootstrap->set_entrypoint(entrypoint);
    bootstrap->set_cmd(cmd);
    return request;
}

TEST(SandboxCommandUtilsTest, BuildBootstrapCommands_ParsesEntrypointAndCmdInOrder)
{
    auto request = MakeRequest("/bin/sh -c", "python app.py --port 8080");

    auto commands = BuildBootstrapCommands(request);

    std::vector<std::string> expected = {
        "/bin/sh", "-c", "python", "app.py", "--port", "8080"
    };
    EXPECT_EQ(commands, expected);
}

TEST(SandboxCommandUtilsTest, BuildBootstrapCommands_EntrypointOnly)
{
    auto request = MakeRequest("/entry --verbose", "");

    auto commands = BuildBootstrapCommands(request);

    std::vector<std::string> expected = {"/entry", "--verbose"};
    EXPECT_EQ(commands, expected);
}

TEST(SandboxCommandUtilsTest, BuildBootstrapCommands_EmptyFieldsReturnsEmpty)
{
    auto request = MakeRequest("", "");

    auto commands = BuildBootstrapCommands(request);

    EXPECT_TRUE(commands.empty());
}

}  // namespace
}  // namespace functionsystem::runtime_manager
