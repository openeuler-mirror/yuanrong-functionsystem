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

#include "runtime_manager/executor/docker_executor.h"

#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "common/constants/constants.h"
#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"
#include "common/utils/files.h"
#include "utils/future_test_helper.h"

namespace functionsystem::runtime_manager {

using functionsystem::resource_view::CPU_RESOURCE_NAME;
using functionsystem::resource_view::MEMORY_RESOURCE_NAME;
using functionsystem::resource_view::ValueType;
using functionsystem::test::AwaitAssertReady;

class MockDockerExecutor : public DockerExecutor {
public:
    MockDockerExecutor(const std::string &name, const litebus::AID &functionAgentAID)
        : DockerExecutor(name, functionAgentAID)
    {
    }

    // Expose protected/private members for testing
    std::string TestBuildDockerHttpRequest(const std::string &method, const std::string &path, const std::string &body)
    {
        return BuildDockerHttpRequest(method, path, body);
    }

    void TestParseDockerResponse(litebus::Promise<nlohmann::json> promise, std::string response)
    {
        ParseDockerResponse(promise, response);
    }

    int TestConnectDockerSocket()
    {
        return ConnectDockerSocket();
    }

    messages::StartInstanceResponse TestGenSuccessStartInstanceResponse(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &containerID,
        const std::string &port)
    {
        return GenSuccessStartInstanceResponse(request, containerID, port);
    }

    bool TestIsRuntimeActive(const std::string &runtimeID)
    {
        return IsRuntimeActive(runtimeID);
    }

    std::map<std::string, messages::RuntimeInstanceInfo> TestGetRuntimeInstanceInfos()
    {
        return GetRuntimeInstanceInfos();
    }

    nlohmann::json TestBuildCreateContainerRequest(const std::string &image,
        const std::vector<std::string> &command, const std::map<std::string, std::string> &envs,
        const std::vector<std::string> &bindMounts, const std::map<std::string, std::string> &portBindings,
        const std::map<std::string, double> &resources, const std::string &logDir)
    {
        ContainerCreateSpec spec{ image, command, envs, bindMounts, portBindings, resources, logDir };
        return BuildCreateContainerRequest(spec);
    }

    std::string TestGetRuntimeImage(const std::shared_ptr<messages::StartInstanceRequest> &request)
    {
        return GetRuntimeImage(request);
    }

    std::string TestGetDockerApiPrefix()
    {
        return GetDockerApiPrefix();
    }

    std::string TestBuildShellCmdLine(const std::string &execPath, const runtime::v1::StartRequest &startReq)
    {
        return BuildShellCmdLine(execPath, startReq);
    }

    // Set internal state for testing
    void SetRuntimeToContainerID(const std::string &runtimeID, const std::string &containerID)
    {
        runtime2containerID_[runtimeID] = containerID;
    }

    void SetRuntime2PortMapping(const std::string &runtimeID, const std::string &port)
    {
        runtime2portMappings_[runtimeID] = port;
    }

    void SetProxyIP(const std::string &proxyIP)
    {
        config_.proxyIP = proxyIP;
    }

    size_t GetRuntimeToContainerIDMapSize() const
    {
        return runtime2containerID_.size();
    }

    std::string GetContainerIDByRuntimeID(const std::string &runtimeID) const
    {
        auto it = runtime2containerID_.find(runtimeID);
        return (it != runtime2containerID_.end()) ? it->second : "";
    }

    void ClearRuntimeToContainerIDMap()
    {
        runtime2containerID_.clear();
    }

    void SetDockerSocketPath(const std::string &path)
    {
        dockerSocketPath_ = path;
    }

    void SetDockerApiVersion(const std::string &version)
    {
        dockerApiVersion_ = version;
    }
};

class DockerExecutorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto functionAgentAID = litebus::AID("FunctionAgentService", "127.0.0.1:8888");
        executor_ = std::make_shared<MockDockerExecutor>("DockerExecutor_Test", functionAgentAID);
        litebus::Spawn(executor_, false);

        // Set up a temp deploy directory
        deployDir_ = "/tmp/docker_executor_test_deploy";
        litebus::os::Mkdir(deployDir_);
    }

    void TearDown() override
    {
        litebus::os::Rmdir(deployDir_);
        litebus::Terminate(executor_->GetAID());
        litebus::Await(executor_->GetAID());
    }

    std::shared_ptr<messages::StartInstanceRequest> CreateStartInstanceRequest(const std::string &runtimeID,
                                                                               const std::string &language)
    {
        auto request = std::make_shared<messages::StartInstanceRequest>();
        auto *info = request->mutable_runtimeinstanceinfo();
        info->set_runtimeid(runtimeID);
        info->set_instanceid("instance-" + runtimeID);
        info->set_requestid("req-" + runtimeID);
        info->set_traceid("trace-" + runtimeID);
        auto *runtimeConfig = info->mutable_runtimeconfig();
        runtimeConfig->set_language(language);
        auto *deploymentConfig = info->mutable_deploymentconfig();
        deploymentConfig->set_deploydir(deployDir_);
        request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::DOCKER));
        return request;
    }

    std::shared_ptr<MockDockerExecutor> executor_;
    std::string deployDir_;
};

// ---- BuildDockerHttpRequest ----

TEST_F(DockerExecutorTest, TestBuildDockerHttpRequestGetNoBody)
{
    std::string result = executor_->TestBuildDockerHttpRequest("GET", "/v1.45/containers/json", "");
    EXPECT_THAT(result, testing::HasSubstr("GET /v1.45/containers/json HTTP/1.1"));
    EXPECT_THAT(result, testing::HasSubstr("Host: localhost"));
    EXPECT_THAT(result, testing::HasSubstr("Content-Length: 0"));
    EXPECT_THAT(result, testing::HasSubstr("Connection: close"));
}

TEST_F(DockerExecutorTest, TestBuildDockerHttpRequestPostWithBody)
{
    nlohmann::json body = {{"Image", "python:3.11"}};
    std::string result = executor_->TestBuildDockerHttpRequest("POST", "/v1.45/containers/create", body.dump());
    EXPECT_THAT(result, testing::HasSubstr("POST /v1.45/containers/create HTTP/1.1"));
    EXPECT_THAT(result, testing::HasSubstr("Content-Type: application/json"));
    EXPECT_THAT(result, testing::HasSubstr("Content-Length: " + std::to_string(body.dump().length())));
    EXPECT_THAT(result, testing::HasSubstr(body.dump()));
}

// ---- ParseDockerResponse ----

TEST_F(DockerExecutorTest, TestParseDockerResponseSuccessWithBody)
{
    litebus::Promise<nlohmann::json> promise;
    std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 29\r\n\r\n{\"Id\":\"abc123\",\"Warnings\":[]}";
    executor_->TestParseDockerResponse(promise, response);

    auto future = promise.GetFuture();
    ASSERT_AWAIT_READY(future);
    auto result = future.Get();
    EXPECT_TRUE(result.contains("Id"));
    EXPECT_EQ(result["Id"].get<std::string>(), "abc123");
    EXPECT_TRUE(result.contains("__http_status"));
    EXPECT_EQ(result["__http_status"].get<int>(), 200);
}

TEST_F(DockerExecutorTest, TestParseDockerResponseNonJsonChunkedBody)
{
    // /images/create returns a 2xx with a chunked, non-JSON progress stream; the status code alone
    // must remain valid so callers can treat the pull as accepted.
    litebus::Promise<nlohmann::json> promise;
    std::string response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nOK\r\n0\r\n\r\n";
    executor_->TestParseDockerResponse(promise, response);

    auto future = promise.GetFuture();
    ASSERT_AWAIT_READY(future);
    auto result = future.Get();
    EXPECT_TRUE(result.contains("__http_status"));
    EXPECT_EQ(result["__http_status"].get<int>(), 200);
}

TEST_F(DockerExecutorTest, TestParseDockerResponseNoContent)
{
    litebus::Promise<nlohmann::json> promise;
    std::string response = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    executor_->TestParseDockerResponse(promise, response);

    auto future = promise.GetFuture();
    ASSERT_AWAIT_READY(future);
    auto result = future.Get();
    EXPECT_TRUE(result.contains("__http_status"));
    EXPECT_EQ(result["__http_status"].get<int>(), 204);
}

TEST_F(DockerExecutorTest, TestParseDockerResponseError404)
{
    litebus::Promise<nlohmann::json> promise;
    std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 27\r\n\r\n{\"message\":\"No such image\"}";
    executor_->TestParseDockerResponse(promise, response);

    auto future = promise.GetFuture();
    ASSERT_AWAIT_READY(future);
    auto result = future.Get();
    EXPECT_TRUE(result.contains("__http_status"));
    EXPECT_EQ(result["__http_status"].get<int>(), 404);
    EXPECT_TRUE(result.contains("__docker_error"));
}

TEST_F(DockerExecutorTest, TestParseDockerResponseInvalidNoHeaderSeparator)
{
    litebus::Promise<nlohmann::json> promise;
    std::string response = "invalid response without separator";
    executor_->TestParseDockerResponse(promise, response);

    auto future = promise.GetFuture();
    ASSERT_AWAIT_READY(future);
    auto result = future.Get();
    EXPECT_TRUE(result.contains("__http_status"));
    EXPECT_EQ(result["__http_status"].get<int>(), 0);
    EXPECT_TRUE(result.value("__parse_failed", false));
}

// ---- GetDockerApiPrefix ----

TEST_F(DockerExecutorTest, TestGetDockerApiPrefixDefault)
{
    EXPECT_EQ(executor_->TestGetDockerApiPrefix(), "/v1.45");
}

TEST_F(DockerExecutorTest, TestGetDockerApiPrefixCustom)
{
    executor_->SetDockerApiVersion("v1.43");
    EXPECT_EQ(executor_->TestGetDockerApiPrefix(), "/v1.43");
}

// ---- IsRuntimeActive ----

TEST_F(DockerExecutorTest, TestIsRuntimeActiveWhenEmpty)
{
    EXPECT_FALSE(executor_->TestIsRuntimeActive("rt-001"));
}

TEST_F(DockerExecutorTest, TestIsRuntimeActiveWhenPresent)
{
    executor_->SetRuntimeToContainerID("rt-001", "container-abc");
    EXPECT_TRUE(executor_->TestIsRuntimeActive("rt-001"));
    EXPECT_FALSE(executor_->TestIsRuntimeActive("rt-002"));
}

// ---- GetRuntimeInstanceInfos ----

TEST_F(DockerExecutorTest, TestGetRuntimeInstanceInfosWhenEmpty)
{
    auto infos = executor_->TestGetRuntimeInstanceInfos();
    EXPECT_TRUE(infos.empty());
}

// ---- GenSuccessStartInstanceResponse ----

TEST_F(DockerExecutorTest, TestGenSuccessStartInstanceResponse)
{
    auto request = CreateStartInstanceRequest("rt-001", "python3");
    auto response = executor_->TestGenSuccessStartInstanceResponse(request, "container-abc123", "22978");

    EXPECT_EQ(response.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(response.message(), "start instance success");
    EXPECT_EQ(response.requestid(), "req-rt-001");
    EXPECT_EQ(response.startruntimeinstanceresponse().runtimeid(), "rt-001");
    EXPECT_EQ(response.startruntimeinstanceresponse().containerid(), "container-abc123");
    EXPECT_EQ(response.startruntimeinstanceresponse().pid(), 0);
    EXPECT_EQ(response.startruntimeinstanceresponse().executortype(), static_cast<int32_t>(EXECUTOR_TYPE::DOCKER));
}

TEST_F(DockerExecutorTest, TestGenSuccessStartInstanceResponseWithPortMapping)
{
    auto request = CreateStartInstanceRequest("rt-001", "python3");
    executor_->SetRuntime2PortMapping("rt-001", "[\"tcp:40001:8080\"]");
    auto response = executor_->TestGenSuccessStartInstanceResponse(request, "container-abc123", "22978");

    EXPECT_EQ(response.startruntimeinstanceresponse().port(), "[\"tcp:40001:8080\"]");
}

TEST_F(DockerExecutorTest, TestGenSuccessStartInstanceResponseReportsRuntimeAddress)
{
    auto request = CreateStartInstanceRequest("rt-001", "python3");
    executor_->SetProxyIP("127.0.0.1");
    auto response = executor_->TestGenSuccessStartInstanceResponse(request, "container-abc123", "22978");

    EXPECT_EQ(response.startruntimeinstanceresponse().address(), "127.0.0.1:22978");
}

// ---- BuildCreateContainerRequest ----

TEST_F(DockerExecutorTest, TestBuildCreateContainerRequestBasic)
{
    std::vector<std::string> command = {"python3", "-u", "/python/yr/main/yr_runtime_main.py"};
    std::map<std::string, std::string> envs = {{ "YR_RUNTIME_ID", "rt-001" }, { "YR_ONLY_STDOUT", "true" }};
    std::vector<std::string> bindMounts = {};
    std::map<std::string, std::string> portBindings = {};
    std::map<std::string, double> resources = {};
    std::string logDir = "/tmp/logs";

    auto req = executor_->TestBuildCreateContainerRequest(
        "yuanrong/python-runtime:latest", command, envs, bindMounts, portBindings, resources, logDir);

    EXPECT_EQ(req["Image"].get<std::string>(), "yuanrong/python-runtime:latest");
    EXPECT_EQ(req["Cmd"].size(), 3u);
    EXPECT_EQ(req["Cmd"][0].get<std::string>(), "python3");
    EXPECT_EQ(req["Env"].size(), 2u);
    EXPECT_EQ(req["HostConfig"]["AutoRemove"].get<bool>(), false);
    EXPECT_EQ(req["HostConfig"]["NetworkMode"].get<std::string>(), "bridge");
    EXPECT_TRUE(req["HostConfig"].contains("LogConfig"));
    EXPECT_EQ(req["HostConfig"]["LogConfig"]["Type"].get<std::string>(), "json-file");
}

TEST_F(DockerExecutorTest, TestBuildCreateContainerRequestWithPortBindings)
{
    std::vector<std::string> command = {"java", "-jar", "app.jar"};
    std::map<std::string, std::string> envs = {};
    std::vector<std::string> bindMounts = {"/tmp/code:/opt/code:ro"};
    std::map<std::string, std::string> portBindings = {{ "8080/tcp", "40001" }};
    std::map<std::string, double> resources = {{ "cpu", 500.0 }, { "memory", 256.0 }};
    std::string logDir = "";

    auto req = executor_->TestBuildCreateContainerRequest(
        "yuanrong/java-runtime:latest", command, envs, bindMounts, portBindings, resources, logDir);

    EXPECT_TRUE(req["ExposedPorts"].contains("8080/tcp"));
    EXPECT_TRUE(req["HostConfig"]["PortBindings"].contains("8080/tcp"));
    EXPECT_EQ(req["HostConfig"]["Binds"].size(), 1u);
    EXPECT_EQ(req["HostConfig"]["NanoCpus"].get<int64_t>(), 500LL * 1000000);
    EXPECT_EQ(req["HostConfig"]["Memory"].get<int64_t>(), 256 * 1024 * 1024);
    EXPECT_EQ(req["HostConfig"]["MemorySwap"].get<int64_t>(), 256 * 1024 * 1024);
    EXPECT_EQ(req["HostConfig"]["PidsLimit"].get<int>(), 4096);
}

// ---- BuildResources (proto map uses uppercase CPU/Memory keys) ----

TEST_F(DockerExecutorTest, TestBuildResourcesReadsUppercaseKeys)
{
    auto request = CreateStartInstanceRequest("rt-res", "python3");
    auto *info = request->mutable_runtimeinstanceinfo();
    auto *resMap = info->mutable_runtimeconfig()->mutable_resources()->mutable_resources();
    auto &cpu = (*resMap)[CPU_RESOURCE_NAME];
    cpu.set_type(ValueType::Value_Type_SCALAR);
    cpu.mutable_scalar()->set_value(1000.0);
    auto &mem = (*resMap)[MEMORY_RESOURCE_NAME];
    mem.set_type(ValueType::Value_Type_SCALAR);
    mem.mutable_scalar()->set_value(512.0);
    mem.mutable_scalar()->set_limit(1024.0);

    auto resources = executor_->BuildResources(*info);
    EXPECT_DOUBLE_EQ(resources["cpu"], 1000.0);
    EXPECT_DOUBLE_EQ(resources["memory"], 512.0);
    EXPECT_DOUBLE_EQ(resources["memory_limit"], 1024.0);
}

TEST_F(DockerExecutorTest, TestBuildResourcesEmptyWithoutConfig)
{
    auto request = CreateStartInstanceRequest("rt-empty", "python3");
    auto resources = executor_->BuildResources(request->runtimeinstanceinfo());
    EXPECT_TRUE(resources.empty());
}

// ---- GetRuntimeImage ----

TEST_F(DockerExecutorTest, TestGetRuntimeImageFromDeployOptions)
{
    auto request = CreateStartInstanceRequest("rt-001", "python3");
    auto *opts = request->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->mutable_deployoptions();
    (*opts)[CONTAINER_ROOTFS] = R"({"type":"image","imageurl":"custom/python:v2"})";

    std::string image = executor_->TestGetRuntimeImage(request);
    EXPECT_EQ(image, "custom/python:v2");
}

TEST_F(DockerExecutorTest, TestGetRuntimeImageEmptyWithoutConfig)
{
    // No deployOptions["rootfs"] and no DOCKER_RUNTIME_IMAGE env (unset in CI) -> empty.
    auto request = CreateStartInstanceRequest("rt-001", "python3");
    std::string image = executor_->TestGetRuntimeImage(request);
    EXPECT_TRUE(image.empty());
}

// ---- SnapshotRuntime (unsupported) ----

TEST_F(DockerExecutorTest, TestSnapshotRuntimeUnsupported)
{
    auto request = std::make_shared<messages::SnapshotRuntimeRequest>();
    request->set_requestid("req-snapshot-001");
    request->set_runtimeid("rt-001");

    auto response = executor_->SnapshotRuntime(request);
    ASSERT_AWAIT_READY(response);
    auto result = response.Get();
    EXPECT_EQ(result.code(), static_cast<int32_t>(StatusCode::GRPC_UNIMPLEMENTED));
    EXPECT_THAT(result.message(), testing::HasSubstr("not supported"));
}

// ---- ConnectDockerSocket (will fail in test env without Docker) ----

TEST_F(DockerExecutorTest, TestConnectDockerSocketFailsWithoutDocker)
{
    // Use a non-existent socket path
    executor_->SetDockerSocketPath("/tmp/nonexistent_docker.sock");
    int fd = executor_->TestConnectDockerSocket();
    EXPECT_LT(fd, 0);  // Should fail since socket doesn't exist
}

// ---- BuildShellCmdLine (tolerant redirect, issue #93) ----

/**
 * Feature: BuildShellCmdLine
 * Description: With non-empty stdout/stderr, the command line wraps the runtime exec in a
 *              tolerant redirect: a subshell probes whether the redirect target is writable,
 *              then execs the runtime with the redirect on success or without it on failure.
 *              argv tokens and redirect paths are POSIX single-quoted (ShellQuote) so user
 *              content is taken literally; injection payloads must not appear as bare commands.
 */
TEST_F(DockerExecutorTest, BuildShellCmdLine_QuotedArgvAndTolerantRedirect)
{
    // execPath is the interpreter ("python3"); command holds the args passed to it. BuildContainerCommand
    // prepends execPath then appends command, so command must NOT repeat execPath (else it appears twice).
    runtime::v1::StartRequest startReq;
    startReq.add_command("-c");
    // Injection payload bundling several shell metacharacter vectors at once: ';', command
    // substitution $(...), backticks, '&&', '||', and a newline. ShellQuote must wrap the whole
    // token in single quotes and escape every internal single quote via '\'' so sh -c takes it as
    // one literal argv element with no metacharacter interpretation.
    startReq.add_command("print('hello; rm -rf /') $(pwn) `bad` && steal || die\nnewline");
    startReq.set_stdout("/tmp/log dir/a'b.out");   // space + single quote in path
    startReq.set_stderr("/tmp/log dir/a'b.err");

    std::string cmdLine = executor_->TestBuildShellCmdLine("python3", startReq);

    // Tolerant redirect skeleton, closed with ; fi.
    EXPECT_THAT(cmdLine, testing::StartsWith("if ( >"));
    EXPECT_THAT(cmdLine, testing::HasSubstr(") 2>/dev/null; then exec "));
    EXPECT_THAT(cmdLine, testing::HasSubstr("; else exec "));
    EXPECT_THAT(cmdLine, testing::EndsWith("; fi"));

    // argv tokens quoted verbatim, execPath appears once (not duplicated by command).
    EXPECT_THAT(cmdLine, testing::HasSubstr("'python3'"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("'-c'"));
    // single quote inside payload escaped via '\'' rather than closing the wrapper.
    EXPECT_THAT(cmdLine, testing::HasSubstr("'print('\\''hello"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("; rm -rf /'\\''"));
    // redirect paths quoted too (space + single quote survived).
    EXPECT_THAT(cmdLine, testing::HasSubstr(">'/tmp/log dir/a'\\''b.out'"));
    // stderr merged into stdout via 2>&1
    EXPECT_THAT(cmdLine, testing::HasSubstr("2>&1"));
    // The ';' vector's bare sequence "; rm -rf /')" (the ' sitting right before ')' in the payload)
    // is broken by the '\'' insertion, so the unescaped form must NOT appear.
    EXPECT_THAT(cmdLine, testing::Not(testing::HasSubstr("; rm -rf /')")));
    // The other vectors ($(), ``, &&, ||, newline) sit inside the payload without an adjacent single
    // quote, so ShellQuote leaves them byte-for-byte literal inside the wrapping quotes — proving
    // they are quoted (not interpreted) is showing they still exist as substrings.
    EXPECT_THAT(cmdLine, testing::HasSubstr("$(pwn)"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("`bad`"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("&& steal"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("|| die"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("die\nnewline"));
}

/**
 * Feature: BuildShellCmdLine
 * Description: Empty stdout or stderr (log dir preparation failed) skips the redirect entirely;
 *              the line is just `exec <quoted argv>` so sh -c never fails on >''.
 */
TEST_F(DockerExecutorTest, BuildShellCmdLine_EmptyPathsSkipsRedirect)
{
    runtime::v1::StartRequest startReq;
    startReq.add_command("-m");
    startReq.add_command("http.server");
    // both paths empty
    std::string cmdLine = executor_->TestBuildShellCmdLine("python3", startReq);

    EXPECT_EQ(cmdLine, "exec 'python3' '-m' 'http.server'");
    EXPECT_THAT(cmdLine, testing::Not(testing::HasSubstr("if (")));
    EXPECT_THAT(cmdLine, testing::Not(testing::HasSubstr(" >")));
    EXPECT_THAT(cmdLine, testing::Not(testing::HasSubstr(" 2>")));
}

/**
 * Feature: BuildShellCmdLine
 * Description: Empty argv (no execPath, no command) still yields a syntactically closed shell line:
 *              tolerant skeleton with then/else branches both present and ; fi at the tail.
 */
TEST_F(DockerExecutorTest, BuildShellCmdLine_EmptyArgv)
{
    runtime::v1::StartRequest startReq;
    startReq.set_stdout("/tmp/out.log");
    startReq.set_stderr("/tmp/err.log");
    std::string cmdLine = executor_->TestBuildShellCmdLine("", startReq);

    // Tolerant skeleton fully closed: then-branch (exec with redirect), else-branch (bare exec), ; fi tail.
    EXPECT_THAT(cmdLine, testing::StartsWith("if ( >"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("then exec  >"));   // empty argv: exec followed by the redirect
    EXPECT_THAT(cmdLine, testing::HasSubstr(" 2>&1"));          // stderr merged into stdout
    EXPECT_THAT(cmdLine, testing::HasSubstr("; else exec ;"));  // else branch present and closed
    EXPECT_THAT(cmdLine, testing::EndsWith("; fi"));
}

}  // namespace functionsystem::runtime_manager
