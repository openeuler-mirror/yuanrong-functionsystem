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

#include "runtime_manager/config/language/language_strategy.h"
#include "runtime_manager/config/language/cpp_strategy.h"
#include "runtime_manager/config/language/python_strategy.h"
#include "runtime_manager/config/language/java_strategy.h"
#include "runtime_manager/config/language/nodejs_strategy.h"
#include "runtime_manager/config/language/go_strategy.h"
#include "runtime_manager/config/command_builder.h"
#include "runtime_manager/config/build.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/resource.pb.h"
#include "common/resource_view/resource_type.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

// ── T9: LanguageStrategy + CommandBuilder tests ───────────────────────────────

class LanguageStrategyTest : public ::testing::Test {
public:
    void SetUp() override
    {
        config_.runtimePath      = "/opt/runtime";
        config_.runtimeLogLevel  = "INFO";
        config_.runtimeConfigPath = "/etc/runtime";
        config_.runtimeLogPath    = "/var/log/runtime";
        config_.hostIP            = "127.0.0.1";
        // Default jvmArgs used for java1.8 — include a small-heap sentinel
        config_.jvmArgs           = { "-Xss512K", "-Xms32m" };
        config_.jvmArgsForJava11  = { "-Xss512K", "-Xms64m" };
        config_.jvmArgsForJava17  = { "-Xss512K", "-Xms64m" };
        config_.jvmArgsForJava21  = { "-Xss512K", "-Xms64m" };
        config_.maxJvmMemory      = 4096.0;
    }

    void TearDown() override {}

    RuntimeConfig config_;
};

// Helper: create a minimal StartInstanceRequest
static messages::StartInstanceRequest MakeMinimalRequest(const std::string &language = "python3.9")
{
    messages::StartInstanceRequest req;
    req.mutable_runtimeinstanceinfo()->set_instanceid("test-instance");
    req.mutable_runtimeinstanceinfo()->set_runtimeid("test-runtime");
    req.mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->set_language(language);
    req.mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_deploydir("/dcache");
    return req;
}

// Helper: check if a string is present in a vector
static bool ContainsStr(const std::vector<std::string> &vec, const std::string &sub)
{
    return std::any_of(vec.begin(), vec.end(),
                       [&sub](const std::string &s) { return s.find(sub) != std::string::npos; });
}

// T9-1: CppStrategy::BuildArgs with minimal request → returns OK and builds exec path
TEST_F(LanguageStrategyTest, CppStrategyBasicRequestReturnsOk)
{
    CppCommandStrategy strategy;
    auto req = MakeMinimalRequest("cpp");

    auto [status, cmdArgs] = strategy.BuildArgs(req, "21000", config_);

    EXPECT_TRUE(status.IsOk());
    EXPECT_FALSE(cmdArgs.execPath.empty());
    // execPath is built from config_.runtimePath + "/cpp/bin/runtime"
    EXPECT_NE(cmdArgs.execPath.find("cpp"), std::string::npos);
}

// T9-2: CppStrategy::BuildArgs with valid runtimePath → OK, execPath derived from config
TEST_F(LanguageStrategyTest, CppStrategyExecPathDerivedFromConfig)
{
    CppCommandStrategy strategy;
    config_.runtimePath = "/custom/rt";
    auto req = MakeMinimalRequest("cpp");

    auto [status, cmdArgs] = strategy.BuildArgs(req, "21000", config_);

    EXPECT_TRUE(status.IsOk());
    EXPECT_EQ(cmdArgs.execPath, "/custom/rt/cpp/bin/runtime");
    // args[0] is the program name "cppruntime"
    ASSERT_FALSE(cmdArgs.args.empty());
    EXPECT_EQ(cmdArgs.args[0], "cppruntime");
}

// T9-3: PythonStrategy::BuildArgs does NOT mutate the original request
TEST_F(LanguageStrategyTest, PythonStrategyDoesNotMutateRequest)
{
    // execLookPath=false avoids PATH lookup in test environment
    PythonCommandStrategy strategy(/*execLookPath=*/false);
    auto req = MakeMinimalRequest("python3.9");
    const std::string origInstanceId = req.runtimeinstanceinfo().instanceid();
    const std::string origLanguage   = req.runtimeinstanceinfo().runtimeconfig().language();

    auto [status, cmdArgs] = strategy.BuildArgs(req, "21000", config_);

    // The request must be unchanged
    EXPECT_EQ(req.runtimeinstanceinfo().instanceid(), origInstanceId);
    EXPECT_EQ(req.runtimeinstanceinfo().runtimeconfig().language(), origLanguage);
}

// T9-4: JavaStrategy::BuildArgs: jvmArgs from config contains "-Xss512K" (small-memory setup)
TEST_F(LanguageStrategyTest, JavaStrategySmallMemoryContainsXss)
{
    JavaCommandStrategy strategy(/*execLookPath=*/false);
    auto req = MakeMinimalRequest("java1.8");

    // Set a small memory resource (< 256 MB) — adds -Xmx<small>m
    auto *resources = req.mutable_runtimeinstanceinfo()
                         ->mutable_runtimeconfig()
                         ->mutable_resources()
                         ->mutable_resources();
    resources::Resource memVal;
    memVal.set_type(resources::Value_Type_SCALAR);
    memVal.mutable_scalar()->set_value(128.0);
    (*resources)[resource_view::MEMORY_RESOURCE_NAME] = memVal;

    auto [status, cmdArgs] = strategy.BuildArgs(req, "21000", config_);

    EXPECT_TRUE(status.IsOk());
    // config_.jvmArgs contains "-Xss512K" which SelectJvmArgs returns for java1.8
    EXPECT_TRUE(ContainsStr(cmdArgs.args, "-Xss512K"));
}

// T9-5: JavaStrategy::BuildArgs: memory >= 256 MB → args contain "-Xmx" with value suffix
TEST_F(LanguageStrategyTest, JavaStrategyLargeMemoryContainsXmx)
{
    JavaCommandStrategy strategy(/*execLookPath=*/false);
    auto req = MakeMinimalRequest("java1.8");

    // Set memory = 512 MB
    auto *resources = req.mutable_runtimeinstanceinfo()
                         ->mutable_runtimeconfig()
                         ->mutable_resources()
                         ->mutable_resources();
    resources::Resource memVal;
    memVal.set_type(resources::Value_Type_SCALAR);
    memVal.mutable_scalar()->set_value(512.0);
    (*resources)[resource_view::MEMORY_RESOURCE_NAME] = memVal;

    auto [status, cmdArgs] = strategy.BuildArgs(req, "21000", config_);

    EXPECT_TRUE(status.IsOk());
    // BuildArgs appends "-Xmx512m" from memory resource
    EXPECT_TRUE(ContainsStr(cmdArgs.args, "-Xmx"));
    EXPECT_TRUE(ContainsStr(cmdArgs.args, "-Xmx512m"));
}

// T9-6: NodejsStrategy::BuildArgs (no LookPath) → succeeds, args contain wrapper.js
TEST_F(LanguageStrategyTest, NodejsStrategyBuildsArgsSuccessfully)
{
    NodejsCommandStrategy strategy(/*execLookPath=*/false);
    auto req = MakeMinimalRequest("nodejs");

    auto [status, cmdArgs] = strategy.BuildArgs(req, "21000", config_);

    EXPECT_TRUE(status.IsOk());
    EXPECT_FALSE(cmdArgs.execPath.empty());
    EXPECT_TRUE(ContainsStr(cmdArgs.args, "wrapper.js"));
}

// T9-7: GoStrategy::BuildArgs → always returns OK (exec path derived from config)
TEST_F(LanguageStrategyTest, GoStrategyAlwaysReturnsOk)
{
    GoCommandStrategy strategy;
    auto req = MakeMinimalRequest("go");

    auto [status, cmdArgs] = strategy.BuildArgs(req, "21000", config_);

    EXPECT_TRUE(status.IsOk());
    EXPECT_FALSE(cmdArgs.execPath.empty());
    EXPECT_NE(cmdArgs.execPath.find("go"), std::string::npos);
}

// T9-8: CommandBuilder::BuildArgs for language "cpp" → dispatches to CppStrategy, returns OK
TEST_F(LanguageStrategyTest, CommandBuilderDispatchesToCppStrategy)
{
    CommandBuilder cmdBuilder(/*execLookPath=*/false);
    cmdBuilder.SetRuntimeConfig(config_);

    auto req = MakeMinimalRequest("cpp");
    auto [status, cmdArgs] = cmdBuilder.BuildArgs("cpp", "21000", req);

    EXPECT_TRUE(status.IsOk());
    EXPECT_FALSE(cmdArgs.execPath.empty());
    EXPECT_NE(cmdArgs.execPath.find("cpp"), std::string::npos);
}

// T9-9: CommandBuilder::BuildArgs for unknown language → returns error
TEST_F(LanguageStrategyTest, CommandBuilderUnknownLanguageReturnsError)
{
    CommandBuilder cmdBuilder(/*execLookPath=*/false);
    cmdBuilder.SetRuntimeConfig(config_);

    auto req = MakeMinimalRequest("unknown-lang-xyz");
    auto [status, cmdArgs] = cmdBuilder.BuildArgs("unknown-lang-xyz", "21000", req);

    EXPECT_FALSE(status.IsOk());
}

// T9-10: CommandBuilder::BuildArgs is a pure function: same input → same output
TEST_F(LanguageStrategyTest, CommandBuilderPureFunctionSameInputSameOutput)
{
    CommandBuilder cmdBuilder(/*execLookPath=*/false);
    cmdBuilder.SetRuntimeConfig(config_);

    auto req = MakeMinimalRequest("cpp");
    auto [status1, cmdArgs1] = cmdBuilder.BuildArgs("cpp", "21000", req);
    auto [status2, cmdArgs2] = cmdBuilder.BuildArgs("cpp", "21000", req);

    EXPECT_EQ(status1.IsOk(), status2.IsOk());
    EXPECT_EQ(cmdArgs1.execPath, cmdArgs2.execPath);
    EXPECT_EQ(cmdArgs1.args, cmdArgs2.args);
}

}  // namespace functionsystem::test
