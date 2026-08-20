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

#include "runtime_manager/config/build.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "common/datasystem_capability.h"
#include "utils/scoped_env.h"
#include "utils/os_utils.hpp"

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

class BuildTest : public ::testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(BuildTest, GeneratePosixEnvsTest)
{
    litebus::os::SetEnv("RUNTIME_METRICS_CONFIG", "{}");
    litebus::os::SetEnv("RUNTIME_METRICS_CONFIG_FILE", "/tmp/home/snuser/config.json");
    RuntimeConfig runtimeConfig;
    runtimeConfig.runtimeLdLibraryPath = "/runtime/sdk/lib";
    runtimeConfig.ip = "127.0.0.1";
    runtimeConfig.hostIP = "10.0.0.1";
    runtimeConfig.dataSystemPort = "31501";
    runtimeConfig.driverServerPort = "22771";
    runtimeConfig.proxyGrpcServerPort = "22771";

    {
        auto startReq = std::make_shared<messages::StartInstanceRequest>();
        startReq->mutable_runtimeinstanceinfo()->set_instanceid("ins-001");
        startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->set_language("python3.9");
        startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_storagetype("s3");
        startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_bucketid("test");
        startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_objectid("test");
        startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_deploydir("/dcache");
        auto envMap = GeneratePosixEnvs(runtimeConfig, startReq, "21000");
        EXPECT_TRUE(envMap.find("LD_LIBRARY_PATH") != envMap.end());
        EXPECT_TRUE(envMap.find("METRICS_CONFIG") != envMap.end());
        EXPECT_TRUE(envMap.find("ENABLE_METRICS") != envMap.end());
        EXPECT_TRUE(envMap.find("POD_IP") != envMap.end());
        EXPECT_EQ(envMap["PYTHONUNBUFFERED"], "1");
        EXPECT_EQ(envMap["ENABLE_METRICS"], "false");
        EXPECT_EQ(envMap["METRICS_CONFIG"], "{}");
        EXPECT_EQ(envMap["METRICS_CONFIG_FILE"], "/tmp/home/snuser/config.json");
        auto ldPath = envMap["LD_LIBRARY_PATH"];
        EXPECT_EQ(envMap["LD_LIBRARY_PATH"],
                  "/dcache/layer/func/test/test:/dcache/layer/func/test/test/lib:/python/yr:/runtime/sdk/lib");
    }
    {
        auto startReq = std::make_shared<messages::StartInstanceRequest>();
        startReq->mutable_runtimeinstanceinfo()->set_instanceid("ins-002");
        startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->set_language("java1.8");
        auto envMap = GeneratePosixEnvs(runtimeConfig, startReq, "21002");
        EXPECT_TRUE(envMap.find("PYTHONUNBUFFERED") == envMap.end());
    }
    {
        auto startReq = std::make_shared<messages::StartInstanceRequest>();
        startReq->mutable_runtimeinstanceinfo()->set_instanceid("ins-003");
        startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->set_language("python3");
        startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_posixenvs()->insert(
            { "user_key", "user_value" });
        // ovveride default envs
        startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_posixenvs()->insert(
            { "PYTHONUNBUFFERED", "0" });
        // user envs in whitelist can override yuanrong default envs
        startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_posixenvs()->insert(
            { "ENABLE_METRICS", "true" });
        auto envMap = GeneratePosixEnvs(runtimeConfig, startReq, "21003");
        EXPECT_EQ(envMap["user_key"], "user_value");
        EXPECT_EQ(envMap["PYTHONUNBUFFERED"], "0");
        EXPECT_EQ(envMap["ENABLE_METRICS"], "true");
    }
    {
        auto startReq = std::make_shared<messages::StartInstanceRequest>();
        startReq->mutable_runtimeinstanceinfo()->set_instanceid("ins-004");
        startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->set_language("posix-custom-runtime");
        auto envMap = GeneratePosixEnvs(runtimeConfig, startReq, "21003");
        EXPECT_EQ(envMap["PYTHONUNBUFFERED"], "1"); // default
    }
}

TEST_F(BuildTest, GeneratePosixEnvsUsesRequestDataSystemCapability)
{
    RuntimeConfig runtimeConfig;
    runtimeConfig.hostIP = "10.0.0.1";
    runtimeConfig.dataSystemPort = "31501";
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->mutable_runtimeinstanceinfo()->set_instanceid("ins-no-ds");
    auto posixEnvs = request->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_posixenvs();
    (*posixEnvs)["YR_DATASYSTEM_DEPLOYED"] = "false";
    (*posixEnvs)["YR_BYPASS_DATASYSTEM"] = "true";

    auto envs = GeneratePosixEnvs(runtimeConfig, request, "21000");

    EXPECT_EQ(envs.at("YR_DATASYSTEM_DEPLOYED"), "false");
    EXPECT_EQ(envs.at("YR_BYPASS_DATASYSTEM"), "true");
    EXPECT_EQ(envs.count("DATA_SYSTEM_ADDR"), 0);
    EXPECT_EQ(envs.count("YR_DS_ADDRESS"), 0);
}

TEST_F(BuildTest, ResolveDataSystemCapabilityMatrix)
{
    const std::vector<std::tuple<std::string, std::string, bool>> cases = {
        { "true", "false", true },
        { "true", "true", true },
        { "false", "true", true },
        { "false", "false", false },
    };
    for (const auto &[enabled, bypass, valid] : cases) {
        std::map<std::string, std::string> envs = {
            { datasystem_capability::YR_DATASYSTEM_DEPLOYED, enabled },
            { datasystem_capability::YR_BYPASS_DATASYSTEM, bypass },
        };

        auto capability = datasystem_capability::ResolveCapability(envs);

        EXPECT_EQ(capability.dataSystemDeployed, enabled == "true");
        EXPECT_EQ(capability.bypassDataSystem, bypass == "true");
        EXPECT_EQ(capability.IsValid(), valid);
    }
}

TEST_F(BuildTest, FunctionAgentClientSwitchDoesNotChangeDeploymentCapability)
{
    ScopedEnv dataSystemClientEnabled("DATA_SYSTEM_ENABLE");
    ScopedEnv dataSystemDeployed("YR_DATASYSTEM_DEPLOYED");
    ScopedEnv bypassDataSystem("YR_BYPASS_DATASYSTEM");
    dataSystemClientEnabled.Set("false");
    dataSystemDeployed.Unset();
    bypassDataSystem.Unset();

    auto capability = datasystem_capability::ResolveCapability(std::map<std::string, std::string>{});

    EXPECT_TRUE(capability.dataSystemDeployed);
    EXPECT_FALSE(capability.bypassDataSystem);
}

TEST_F(BuildTest, GeneratePosixEnvsUsesDataSystemDefaults)
{
    ScopedEnv dataSystemDeployed("YR_DATASYSTEM_DEPLOYED");
    ScopedEnv bypassDataSystem("YR_BYPASS_DATASYSTEM");
    dataSystemDeployed.Unset();
    bypassDataSystem.Unset();
    RuntimeConfig runtimeConfig;
    runtimeConfig.hostIP = "10.0.0.1";
    runtimeConfig.dataSystemPort = "31501";
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->mutable_runtimeinstanceinfo()->set_instanceid("ins-default-ds");

    auto envs = GeneratePosixEnvs(runtimeConfig, request, "21000");

    EXPECT_EQ(envs.at("YR_DATASYSTEM_DEPLOYED"), "true");
    EXPECT_EQ(envs.at("YR_BYPASS_DATASYSTEM"), "false");
    EXPECT_EQ(envs.at("DATASYSTEM_ADDR"), "10.0.0.1:31501");
    EXPECT_EQ(envs.at("YR_DS_ADDRESS"), "10.0.0.1:31501");
}

TEST_F(BuildTest, GeneratePosixEnvsFallsBackToRuntimeManagerEnvironment)
{
    ScopedEnv dataSystemDeployed("YR_DATASYSTEM_DEPLOYED");
    ScopedEnv bypassDataSystem("YR_BYPASS_DATASYSTEM");
    dataSystemDeployed.Set("false");
    bypassDataSystem.Set("true");
    RuntimeConfig runtimeConfig;
    runtimeConfig.hostIP = "10.0.0.1";
    runtimeConfig.dataSystemPort = "31501";
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->mutable_runtimeinstanceinfo()->set_instanceid("ins-env-no-ds");

    auto envs = GeneratePosixEnvs(runtimeConfig, request, "21000");

    EXPECT_EQ(envs.at("YR_DATASYSTEM_DEPLOYED"), "false");
    EXPECT_EQ(envs.at("YR_BYPASS_DATASYSTEM"), "true");
    EXPECT_EQ(envs.count("DATA_SYSTEM_ADDR"), 0);
    EXPECT_EQ(envs.count("YR_DS_ADDRESS"), 0);
}

TEST_F(BuildTest, GenerateEnvsTest)
{
    RuntimeConfig runtimeConfig;
    runtimeConfig.runtimeLdLibraryPath = "/runtime/sdk/lib";
    runtimeConfig.hostIP = "10.0.0.1";
    runtimeConfig.dataSystemPort = "31501";
    runtimeConfig.driverServerPort = "22771";
    runtimeConfig.proxyGrpcServerPort = "22771";
    auto startReq = std::make_shared<messages::StartInstanceRequest>();
    startReq->mutable_runtimeinstanceinfo()->set_instanceid("ins-001");
    startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_storagetype("s3");
    startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_bucketid("test");
    startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_objectid("test/a/b/c");
    startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_deploydir("/dcache");
    auto layer1 = startReq->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->add_layers();
    layer1->set_bucketid("test");
    layer1->set_objectid("layer/a/b");
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_tlsconfig()->set_dsauthenable(true);
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_tlsconfig()->set_serverauthenable(true);
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_tlsconfig()->set_enableservermode(true);
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_userenvs()->insert(
        { "func-LD_LIBRARY_PATH", "/dcache" });
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_userenvs()->insert(
        { "func-POSIX_LISTEN_ADDR", "/dcache" });
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_userenvs()->insert(
        { "func-YR_DATASYSTEM_DEPLOYED", "false" });
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_userenvs()->insert(
        { "func-YR_BYPASS_DATASYSTEM", "true" });
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_userenvs()->insert(
        { "func-NPU-DEVICE-IDS", "0,1,3" });
    startReq->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->mutable_userenvs()->insert(
        { "func-GPU-DEVICE-IDS", "1,2,3" });
    auto env = GenerateEnvs(runtimeConfig, startReq, "21000", {0, 4, 6, 7});
    EXPECT_TRUE(env.customResourceEnvs["ENABLE_DS_AUTH"] == "true");
    EXPECT_TRUE(env.customResourceEnvs["ENABLE_SERVER_AUTH"] == "true");
    EXPECT_TRUE(env.customResourceEnvs["ENABLE_SERVER_MODE"] == "true");
    EXPECT_TRUE(env.userEnvs["LD_LIBRARY_PATH"] == "/dcache");
    EXPECT_TRUE(env.userEnvs.find("POSIX_LISTEN_ADDR") == env.userEnvs.end());
    EXPECT_TRUE(env.userEnvs.find("YR_DATASYSTEM_DEPLOYED") == env.userEnvs.end());
    EXPECT_TRUE(env.userEnvs.find("YR_BYPASS_DATASYSTEM") == env.userEnvs.end());
    EXPECT_TRUE(env.posixEnvs["YR_FUNCTION_LIB_PATH"] == "/dcache/layer/func/test/test-a-b-c");
    EXPECT_TRUE(env.posixEnvs["LAYER_LIB_PATH"] == "/dcache/layer/test/layer-a-b");
    EXPECT_TRUE(env.userEnvs["ASCEND_RT_VISIBLE_DEVICES"] == "0,1,3");
    EXPECT_TRUE(env.userEnvs["CUDA_VISIBLE_DEVICES"] == "1,2,3");
    EXPECT_TRUE(env.customResourceEnvs.find("YR_LOG_PREFIX") == env.customResourceEnvs.end());

    startReq->set_logprefix("YR_123_000001");
    env = GenerateEnvs(runtimeConfig, startReq, "21000", {0, 4, 6, 7});
    EXPECT_EQ(env.customResourceEnvs["YR_LOG_PREFIX"], "YR_123_000001");
    EXPECT_EQ(env.customResourceEnvs["DATASYSTEM_CLIENT_LOG_NAME"], "YR_123_000001_ds_client");
    EXPECT_EQ(env.customResourceEnvs["DATASYSTEM_CLIENT_ACCESS_LOG_NAME"], "YR_123_000001_ds_client_access");
}
TEST_F(BuildTest, SelectRealIDsTest_CardsIDsAndEnvSizeNotTheSame)
{
    const std::vector<int> &cardsIDs = { 0, 4, 6, 7 };
    const std::string &env = "0,1,2";
    std::string result = SelectRealIDs(env, cardsIDs);
    EXPECT_EQ(result, "0,4,6");
}

TEST_F(BuildTest, SelectRealIDsTest_CardsIDsAndEnvSizeTheSame)
{
    const std::vector<int> &cardsIDs = { 0, 4, 6, 7 };
    const std::string &env = "0,1,2,3";
    std::string result = SelectRealIDs(env, cardsIDs);
    EXPECT_EQ(result, "0,4,6,7");
}

TEST_F(BuildTest, SelectRealIDsTest_EmptyEnv)
{
    const std::vector<int> &cardsIDs = { 0, 4, 6, 7 };
    const std::string &env = "";
    std::string result = SelectRealIDs(env, cardsIDs);
    EXPECT_EQ(result, "");
}
}  // namespace functionsystem::test
