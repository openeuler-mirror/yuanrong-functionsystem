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

#include "runtime_manager/executor/conch_executor.h"

#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "common/constants/constants.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/sandbox_api.grpc.pb.h"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"
#include "common/utils/files.h"
#include "utils/future_test_helper.h"

namespace functionsystem::runtime_manager {

// Bring in the test helper functions for ASSERT_AWAIT_READY / ASSERT_AWAIT_SET macros
using functionsystem::test::AwaitAssertReady;
using functionsystem::test::AwaitAssertSet;

class MockConchExecutor : public ConchExecutor {
public:
    MockConchExecutor(const std::string &name, const litebus::AID &functionAgentAID)
        : ConchExecutor(name, functionAgentAID)
    {
    }

    // Expose protected/private members for testing
    std::string TestBuildUdsHttpRequest(const std::string &method, const std::string &path, const std::string &body)
    {
        return BuildUdsHttpRequest(method, path, body);
    }

    void TestParseResponse(litebus::Promise<nlohmann::json> promise, std::string response)
    {
        ParseResponse(promise, response);
    }

    int TestConnectUdsSocket(const std::string &socketPath)
    {
        return ConnectUdsSocket(socketPath);
    }

    nlohmann::json TestBuildCommand(const std::shared_ptr<runtime::v1::StartRequest> &start)
    {
        return BuildCommand(start);
    }

    nlohmann::json TestCreateRequest(const std::shared_ptr<messages::StartInstanceRequest> &request)
    {
        return CreateRequest(request);
    }

    messages::StartInstanceResponse TestGenSuccessStartInstanceResponse(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &sandboxID,
        const std::string &sandboxIP = "")
    {
        return GenSuccessStartInstanceResponse(request, sandboxID, sandboxIP);
    }

    litebus::Future<runtime::v1::StartResponse> TestCreateSandbox(const std::string &runtimeID,
                                                                   const std::string &hostUser = "",
                                                                   const std::string &rootfs = "")
    {
        // CreateSandbox takes a StartInstanceRequest; build a minimal one carrying the
        // runtimeID and (optionally) HOST_USER / rootfs deploy options the caller asked for.
        auto request = std::make_shared<messages::StartInstanceRequest>();
        request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
        auto runtimeInfo = request->mutable_runtimeinstanceinfo();
        runtimeInfo->set_runtimeid(runtimeID);
        runtimeInfo->set_requestid("test_request_id");
        runtimeInfo->set_instanceid("test_instance_id");
        runtimeInfo->set_traceid("test_trace_id");
        if (!hostUser.empty()) {
            (*runtimeInfo->mutable_deploymentconfig()->mutable_deployoptions())[HOST_USER] = hostUser;
        }
        if (!rootfs.empty()) {
            (*runtimeInfo->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_ROOTFS] = rootfs;
        }
        return CreateSandbox(request);
    }

    litebus::Future<runtime::v1::StartResponse> TestExecInSandbox(
        const std::string &runtimeID, const std::shared_ptr<runtime::v1::StartRequest> &start,
        const std::string &sandboxId)
    {
        return ExecInSandbox(runtimeID, start, sandboxId);
    }

    litebus::Future<runtime::v1::DeleteResponse> TestDoDeleteSandbox(
        const std::shared_ptr<runtime::v1::DeleteRequest> &req)
    {
        return DoDeleteSandbox(req);
    }

    litebus::Future<runtime::v1::StartResponse> TestStartByRuntimeID(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::map<std::string, std::string> &startRuntimeParams, const std::vector<std::string> &buildArgs,
        const Envs &envs)
    {
        return StartByRuntimeID(request, startRuntimeParams, buildArgs, envs);
    }

    litebus::Future<messages::StartInstanceResponse> TestStartRuntime(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &language, const Envs &envs,
        const std::vector<std::string> &args)
    {
        return StartRuntime(request, language, envs, args);
    }

    bool TestIsRuntimeActive(const std::string &runtimeID)
    {
        return IsRuntimeActive(runtimeID);
    }

    std::map<std::string, messages::RuntimeInstanceInfo> TestGetRuntimeInstanceInfos()
    {
        return GetRuntimeInstanceInfos();
    }

    // Set internal state for testing
    void SetRuntimeToSandboxID(const std::string &runtimeID, const std::string &sandboxID)
    {
        runtime2sandboxID_[runtimeID] = sandboxID;
    }

    void SetRuntime2PortMapping(const std::string &runtimeID, const std::string &port)
    {
        runtime2portMappings_[runtimeID] = port;
    }

    size_t GetRuntimeToSandboxIDMapSize() const
    {
        return runtime2sandboxID_.size();
    }

    std::string GetSandboxIDByRuntimeID(const std::string &runtimeID) const
    {
        auto it = runtime2sandboxID_.find(runtimeID);
        return (it != runtime2sandboxID_.end()) ? it->second : "";
    }

    void ClearRuntimeToSandboxIDMap()
    {
        runtime2sandboxID_.clear();
    }
};

class ConchExecutorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        executor_ = std::make_shared<MockConchExecutor>("TestConchExecutor",
                                                         litebus::AID("FunctionAgent", "127.0.0.1:8080"));

        // Create test deploy directory
        testDeployDir_ = "/tmp/test-conch-executor";
        litebus::os::Mkdir(testDeployDir_);
        litebus::os::Mkdir(testDeployDir_ + "/layer/func");
        TouchFile(testDeployDir_ + "/layer/func/funcObj");

        // Initialize config
        RuntimeConfig config;
        config.runtimePath = "/tmp";
        config.runtimeLogPath = "/tmp/logs";
        config.runtimeStdLogDir = "std";
        executor_->config_ = config;
    }

    void TearDown() override
    {
        litebus::os::Rmdir(testDeployDir_);
    }

    std::shared_ptr<messages::StartInstanceRequest> GenStartInstanceRequest(const std::string &language = "python3",
                                                                            const std::string &execPath = "")
    {
        auto request = std::make_shared<messages::StartInstanceRequest>();
        request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));

        auto runtimeInfo = request->mutable_runtimeinstanceinfo();
        runtimeInfo->set_requestid("test_request_id");
        runtimeInfo->set_instanceid("test_instance_id");
        runtimeInfo->set_traceid("test_trace_id");
        runtimeInfo->set_runtimeid("test_runtime_id");

        auto runtimeConfig = runtimeInfo->mutable_runtimeconfig();
        runtimeConfig->set_language(language);

        auto deployConfig = runtimeInfo->mutable_deploymentconfig();
        deployConfig->set_objectid("test_object_id");
        deployConfig->set_bucketid("test_bucket_id");
        deployConfig->set_deploydir(testDeployDir_);
        deployConfig->set_storagetype("local");
        if (!execPath.empty()) {
            (*deployConfig->mutable_deployoptions())[EXEC_PATH] = execPath;
        }

        auto containerInfo = runtimeInfo->mutable_container();
        containerInfo->set_mountpoint("/opt/func");

        return request;
    }

    std::shared_ptr<messages::StopInstanceRequest> GenStopInstanceRequest(
        const std::string &runtimeID = "test_runtime_id")
    {
        auto request = std::make_shared<messages::StopInstanceRequest>();
        request->set_runtimeid(runtimeID);
        request->set_requestid("test_stop_request_id");
        request->set_traceid("test_stop_trace_id");
        request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
        request->set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));

        return request;
    }

    std::shared_ptr<messages::SnapshotRuntimeRequest> GenSnapshotRuntimeRequest()
    {
        auto request = std::make_shared<messages::SnapshotRuntimeRequest>();
        request->set_requestid("test_snapshot_request_id");
        request->set_runtimeid("test_runtime_id");
        request->set_instanceid("test_instance_id");

        return request;
    }

    std::shared_ptr<messages::UpdateCredRequest> GenUpdateCredRequest()
    {
        auto request = std::make_shared<messages::UpdateCredRequest>();
        request->set_requestid("test_update_cred_request_id");
        request->set_runtimeid("test_runtime_id");
        request->set_token("test_token");
        request->set_salt("test_salt");

        return request;
    }

    std::string testDeployDir_;
    std::shared_ptr<MockConchExecutor> executor_;
};

/**
 * Feature: ParseResponse
 * Description: Test parsing HTTP responses from jiuwenbox (conch backend)
 */
TEST_F(ConchExecutorTest, ParseResponse_ValidResponse)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 19\r\n"
        "\r\n"
        "{\"id\":\"sandbox123\"}";

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_READY(future);
    auto json = future.Get();
    EXPECT_EQ(json["id"], "sandbox123");
}

TEST_F(ConchExecutorTest, ParseResponse_NoHeaderSeparator)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string response = "HTTP/1.1 200 OK";

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_SET(future);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(future.GetErrorCode(), static_cast<int>(StatusCode::ERR_INNER_COMMUNICATION));
}

TEST_F(ConchExecutorTest, ParseResponse_EmptyBody)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_SET(future);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(future.GetErrorCode(), static_cast<int>(StatusCode::ERR_INNER_COMMUNICATION));
}

TEST_F(ConchExecutorTest, ParseResponse_InvalidJson)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "{invalid}";

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_SET(future);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(future.GetErrorCode(), static_cast<int>(StatusCode::ERR_INNER_COMMUNICATION));
}

// conch soft-failure: 201 + phase=error + error_message. ParseResponse admits 201
// (2xx) and CreateSandbox then reads error_message to mark the create as failed.
TEST_F(ConchExecutorTest, ParseResponse_Admits201SoftFailure)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string body = R"({"phase":"error","error_message":"conchd unreachable"})";
    std::string response =
        "HTTP/1.1 201 Created\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(body.length()) + "\r\n\r\n" + body;

    executor_->TestParseResponse(promise, response);

    // 201 is 2xx -> not a transport failure; body parses as JSON carrying error_message.
    ASSERT_AWAIT_READY(future);
    auto json = future.Get();
    EXPECT_EQ(json["phase"], "error");
    EXPECT_EQ(json["error_message"], "conchd unreachable");
}

TEST_F(ConchExecutorTest, ParseResponse_Non2xxIsFailure)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    // 400 (invalid bind mount / invalid sandbox_runtime) -> transport failure here.
    std::string response =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Length: 12\r\n"
        "\r\n"
        "{\"detail\":\"\"}";

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_SET(future);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(future.GetErrorCode(), static_cast<int>(StatusCode::ERR_INNER_COMMUNICATION));
}

/**
 * Feature: ConnectUdsSocket
 */
TEST_F(ConchExecutorTest, ConnectUdsSocket_InvalidPath)
{
    int fd = executor_->TestConnectUdsSocket("/nonexistent/socket/path");
    EXPECT_EQ(fd, -1);
}

TEST_F(ConchExecutorTest, ConnectUdsSocket_PathTooLong)
{
    std::string longPath(500, 'a');  // Exceeds sockaddr_un sun_path length
    int fd = executor_->TestConnectUdsSocket(longPath);
    EXPECT_EQ(fd, -1);
}

TEST_F(ConchExecutorTest, ConnectUdsSocket_NoServer)
{
    std::string socketPath = "/tmp/test_conch_socket_" + std::to_string(getpid());
    int fd = executor_->TestConnectUdsSocket(socketPath);
    EXPECT_EQ(fd, -1);
}

/**
 * Feature: GenSuccessStartInstanceResponse
 */
TEST_F(ConchExecutorTest, GenSuccessStartInstanceResponse)
{
    auto request = GenStartInstanceRequest();
    std::string sandboxID = "sandbox123";
    executor_->SetRuntime2PortMapping("test_runtime_id", "8080");

    auto response = executor_->TestGenSuccessStartInstanceResponse(request, sandboxID);

    EXPECT_EQ(response.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(response.message(), "start instance success");
    EXPECT_EQ(response.requestid(), "test_request_id");
    EXPECT_EQ(response.startruntimeinstanceresponse().runtimeid(), "test_runtime_id");
    EXPECT_EQ(response.startruntimeinstanceresponse().containerid(), sandboxID);
    EXPECT_EQ(response.startruntimeinstanceresponse().pid(), 0);
    EXPECT_EQ(response.startruntimeinstanceresponse().port(), "8080");
    EXPECT_EQ(response.startruntimeinstanceresponse().executortype(), static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
}

TEST_F(ConchExecutorTest, GenSuccessStartInstanceResponse_NoPortMapping)
{
    auto request = GenStartInstanceRequest();
    std::string sandboxID = "sandbox123";

    auto response = executor_->TestGenSuccessStartInstanceResponse(request, sandboxID);

    EXPECT_EQ(response.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(response.startruntimeinstanceresponse().port(), "");
}

TEST_F(ConchExecutorTest, GenSuccessStartInstanceResponse_EmptySandboxID)
{
    auto request = GenStartInstanceRequest();
    std::string sandboxID = "";

    auto response = executor_->TestGenSuccessStartInstanceResponse(request, sandboxID);

    EXPECT_EQ(response.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(response.startruntimeinstanceresponse().containerid(), "");
}

TEST_F(ConchExecutorTest, GenSuccessStartInstanceResponse_MultipleRuntimes)
{
    auto request1 = GenStartInstanceRequest();
    request1->mutable_runtimeinstanceinfo()->set_runtimeid("runtime1");
    executor_->SetRuntime2PortMapping("runtime1", "8080");

    auto request2 = GenStartInstanceRequest();
    request2->mutable_runtimeinstanceinfo()->set_runtimeid("runtime2");
    request2->mutable_runtimeinstanceinfo()->set_requestid("test_request_id2");
    request2->mutable_runtimeinstanceinfo()->set_instanceid("test_instance_id2");
    executor_->SetRuntime2PortMapping("runtime2", "8081");

    auto response1 = executor_->TestGenSuccessStartInstanceResponse(request1, "sandbox1");
    auto response2 = executor_->TestGenSuccessStartInstanceResponse(request2, "sandbox2");

    EXPECT_EQ(response1.startruntimeinstanceresponse().runtimeid(), "runtime1");
    EXPECT_EQ(response1.startruntimeinstanceresponse().port(), "8080");
    EXPECT_EQ(response2.startruntimeinstanceresponse().runtimeid(), "runtime2");
    EXPECT_EQ(response2.startruntimeinstanceresponse().port(), "8081");
}

/**
 * Feature: IsRuntimeActive
 */
TEST_F(ConchExecutorTest, IsRuntimeActive_RuntimeExists)
{
    std::string runtimeID = "active_runtime_id";
    executor_->SetRuntimeToSandboxID(runtimeID, "sandbox123");

    bool isActive = executor_->TestIsRuntimeActive(runtimeID);
    EXPECT_TRUE(isActive);
}

TEST_F(ConchExecutorTest, IsRuntimeActive_RuntimeNotExists)
{
    std::string runtimeID = "inactive_runtime_id";

    bool isActive = executor_->TestIsRuntimeActive(runtimeID);
    EXPECT_FALSE(isActive);
}

TEST_F(ConchExecutorTest, IsRuntimeActive_EmptyRuntimeID)
{
    std::string runtimeID = "";

    bool isActive = executor_->TestIsRuntimeActive(runtimeID);
    EXPECT_FALSE(isActive);
}

TEST_F(ConchExecutorTest, IsRuntimeActive_AddAndRemove)
{
    std::string runtimeID = "test_runtime_id";

    EXPECT_FALSE(executor_->TestIsRuntimeActive(runtimeID));

    executor_->SetRuntimeToSandboxID(runtimeID, "sandbox123");
    EXPECT_TRUE(executor_->TestIsRuntimeActive(runtimeID));
}

/**
 * Feature: GetRuntimeInstanceInfos
 */
TEST_F(ConchExecutorTest, GetRuntimeInstanceInfos_Empty)
{
    auto infos = executor_->TestGetRuntimeInstanceInfos();
    EXPECT_TRUE(infos.empty());
}

/**
 * Feature: SnapshotRuntime (unsupported for conch)
 */
TEST_F(ConchExecutorTest, SnapshotRuntime_Unsupported)
{
    auto request = GenSnapshotRuntimeRequest();

    auto responseFuture = executor_->SnapshotRuntime(request);

    ASSERT_AWAIT_READY(responseFuture);
    auto response = responseFuture.Get();
    EXPECT_EQ(response.code(), static_cast<int32_t>(StatusCode::GRPC_UNIMPLEMENTED));
    EXPECT_EQ(response.message(), "Snapshot is not supported for conch-based runtime");
    EXPECT_EQ(response.requestid(), "test_snapshot_request_id");
}

/**
 * Feature: UpdateCredForRuntime
 */
TEST_F(ConchExecutorTest, UpdateCredForRuntime)
{
    auto request = GenUpdateCredRequest();

    auto responseFuture = executor_->UpdateCredForRuntime(request);

    ASSERT_AWAIT_READY(responseFuture);
    auto response = responseFuture.Get();
    EXPECT_EQ(response.code(), 0);
    EXPECT_EQ(response.message(), "update credentials success");
    EXPECT_EQ(response.requestid(), "test_update_cred_request_id");
}

TEST_F(ConchExecutorTest, UpdateCredForRuntime_EmptyCredentials)
{
    auto request = std::make_shared<messages::UpdateCredRequest>();
    request->set_requestid("test_request_id");
    request->set_runtimeid("test_runtime_id");
    request->set_token("");
    request->set_salt("");

    auto responseFuture = executor_->UpdateCredForRuntime(request);

    ASSERT_AWAIT_READY(responseFuture);
    auto response = responseFuture.Get();
    EXPECT_EQ(response.code(), 0);
    EXPECT_EQ(response.message(), "update credentials success");
}

/**
 * Feature: NotifyInstancesDiskUsageExceedLimit
 */
TEST_F(ConchExecutorTest, NotifyInstancesDiskUsageExceedLimit)
{
    std::string description = "Disk usage exceeded 80%";
    int limit = 80;

    auto statusFuture = executor_->NotifyInstancesDiskUsageExceedLimit(description, limit);

    ASSERT_AWAIT_READY(statusFuture);
    auto status = statusFuture.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::SUCCESS);
}

TEST_F(ConchExecutorTest, NotifyInstancesDiskUsageExceedLimit_ZeroLimit)
{
    std::string description = "Disk usage at 0%";
    int limit = 0;

    auto statusFuture = executor_->NotifyInstancesDiskUsageExceedLimit(description, limit);

    ASSERT_AWAIT_READY(statusFuture);
    auto status = statusFuture.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::SUCCESS);
}

/**
 * Feature: StopInstance
 */
TEST_F(ConchExecutorTest, StopInstance_CleansUpMappingsOnDeleteFailure)
{
    // Without a running jiuwenbox UDS server, DoDeleteSandbox fails and StopInstance
    // resolves to ERR_INNER_COMMUNICATION. The success path needs a UDS mock; here we
    // deterministically assert the failure path: a non-OK status AND the runtime→sandbox
    // mapping is cleared regardless (so a failed stop never leaks an orphan mapping).
    std::string runtimeID = "test_runtime_id_success";
    std::string sandboxID = "sandbox_success_123";
    executor_->SetRuntimeToSandboxID(runtimeID, sandboxID);

    auto request = GenStopInstanceRequest(runtimeID);

    auto statusFuture = executor_->StopInstance(request, false);

    ASSERT_AWAIT_READY(statusFuture);
    auto status = statusFuture.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::ERR_INNER_COMMUNICATION);
    EXPECT_EQ(executor_->GetSandboxIDByRuntimeID(runtimeID), "");
}

TEST_F(ConchExecutorTest, StopInstance_RuntimeNotExists)
{
    auto request = GenStopInstanceRequest("nonexistent_runtime_id");

    auto statusFuture = executor_->StopInstance(request, false);

    ASSERT_AWAIT_READY(statusFuture);
    auto status = statusFuture.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::SUCCESS);
}

TEST_F(ConchExecutorTest, StopInstance_OomKilled)
{
    // oomKilled is currently unused by ConchExecutor::StopInstance (conch has no stop;
    // both paths go straight to DELETE). Assert the same deterministic behavior as the
    // non-oom case so the two-branch if/else that accepted both OK and failure is gone.
    std::string runtimeID = "test_runtime_id_oom_killed";
    std::string sandboxID = "sandbox_oom_123";
    executor_->SetRuntimeToSandboxID(runtimeID, sandboxID);

    auto request = GenStopInstanceRequest(runtimeID);

    auto statusFuture = executor_->StopInstance(request, true);

    ASSERT_AWAIT_READY(statusFuture);
    auto status = statusFuture.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::ERR_INNER_COMMUNICATION);
    EXPECT_EQ(executor_->GetSandboxIDByRuntimeID(runtimeID), "");
}

/**
 * Feature: Constructor
 */
TEST_F(ConchExecutorTest, Constructor)
{
    litebus::AID aid("FunctionAgent", "127.0.0.1:8080");
    auto executor = std::make_shared<MockConchExecutor>("TestExecutor", aid);

    EXPECT_EQ(executor->GetAID().Name(), "TestExecutor");
}

/**
 * Feature: CreateRequest (the conch-specific contract)
 * Description: CreateRequest must emit sandbox_runtime:"conch", policy.conch.template_id
 *              from imageurl, bind_mounts under policy.conch.filesystem_policy.bind_mounts,
 *              and guest env under policy.conch.env. It must NOT emit the supervisor-only
 *              policy.environment / process / namespace / cgroup segments (ConchPolicy is
 *              extra="forbid").
 */
TEST_F(ConchExecutorTest, CreateRequest_EmitsConchRuntimeAndTemplateId)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    auto runtimeInfo = request->mutable_runtimeinstanceinfo();
    runtimeInfo->set_runtimeid("rt");
    (*runtimeInfo->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_ROOTFS] =
        R"({"type":"image","imageurl":"sha256:abc123"})";

    auto req = executor_->TestCreateRequest(request);

    // top-level routing field routes jiuwenbox to the conch backend
    EXPECT_EQ(req["sandbox_runtime"], "conch");
    EXPECT_EQ(req["policy_mode"], "append");
    // template_id sourced from rootfs imageurl
    ASSERT_TRUE(req["policy"]["conch"].contains("template_id"));
    EXPECT_EQ(req["policy"]["conch"]["template_id"], "sha256:abc123");
}

TEST_F(ConchExecutorTest, CreateRequest_EmptyImageurlYieldsEmptyTemplateId)
{
    // No rootfs -> ParseRootfsImageUrl returns "" -> conch.template_id stays empty,
    // conchd falls back to JIUWENBOX_CONCH_TEMPLATE_ID / sandbox.default_template_id.
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    request->mutable_runtimeinstanceinfo()->set_runtimeid("rt");

    auto req = executor_->TestCreateRequest(request);

    EXPECT_EQ(req["sandbox_runtime"], "conch");
    EXPECT_EQ(req["policy"]["conch"]["template_id"], "");
}

TEST_F(ConchExecutorTest, CreateRequest_BindMountsUnderConchFilesystemPolicy)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    auto runtimeInfo = request->mutable_runtimeinstanceinfo();
    runtimeInfo->set_runtimeid("rt");
    // rootfs carries mounts[]; BindHostLogDir adds the log dir mount too (host path must
    // exist for conch, but CreateRequest only assembles the JSON here).
    (*runtimeInfo->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_ROOTFS] =
        R"({"type":"image","imageurl":"sha256:abc","mounts":[)"
        R"({"source":"/data/host","target":"/data","readonly":true})]}";

    auto req = executor_->TestCreateRequest(request);

    // bind_mounts MUST be under policy.conch.filesystem_policy.bind_mounts (NOT top-level).
    ASSERT_TRUE(req["policy"]["conch"].contains("filesystem_policy"));
    ASSERT_TRUE(req["policy"]["conch"]["filesystem_policy"].contains("bind_mounts"));
    auto bindMounts = req["policy"]["conch"]["filesystem_policy"]["bind_mounts"];
    ASSERT_TRUE(bindMounts.is_array());
    ASSERT_GE(bindMounts.size(), 1);
    bool foundRootfsMount = false;
    for (const auto &m : bindMounts) {
        if (m["host_path"] == "/data/host") {
            foundRootfsMount = true;
            EXPECT_EQ(m["sandbox_path"], "/data");
            EXPECT_EQ(m["mode"], "ro");  // readonly:true -> "ro"
        }
    }
    EXPECT_TRUE(foundRootfsMount);
    // top-level filesystem_policy must NOT carry bind_mounts (conch ignores it)
    EXPECT_FALSE(req["policy"].contains("filesystem_policy"));
}

TEST_F(ConchExecutorTest, CreateRequest_NoBindMountsOmitsFilesystemPolicy)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    auto runtimeInfo = request->mutable_runtimeinstanceinfo();
    runtimeInfo->set_runtimeid("rt");
    (*runtimeInfo->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_ROOTFS] =
        R"({"type":"image","imageurl":"sha256:abc"})";

    auto req = executor_->TestCreateRequest(request);

    // With no rootfs mounts and no host log dir (config path doesn't exist in test),
    // ParseBindMounts is empty -> conch.filesystem_policy is omitted (not an empty object).
    EXPECT_FALSE(req["policy"]["conch"].contains("filesystem_policy"));
}

TEST_F(ConchExecutorTest, CreateRequest_PutsEnvsUnderConchEnv)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    auto runtimeInfo = request->mutable_runtimeinstanceinfo();
    runtimeInfo->set_runtimeid("rt");
    auto posixEnvs = runtimeInfo->mutable_runtimeconfig()->mutable_posixenvs();
    (*posixEnvs)["AGENT_SERVER_HOST"] = "0.0.0.0";
    (*posixEnvs)["AGENT_SERVER_PORT"] = "18092";
    (*posixEnvs)["YR_TENANT_ID"] = "dev";

    auto req = executor_->TestCreateRequest(request);

    // Guest env goes to policy.conch.env (NOT top-level policy.environment, which is bwrap-only).
    ASSERT_TRUE(req["policy"]["conch"].contains("env"));
    EXPECT_EQ(req["policy"]["conch"]["env"]["AGENT_SERVER_HOST"], "0.0.0.0");
    EXPECT_EQ(req["policy"]["conch"]["env"]["AGENT_SERVER_PORT"], "18092");
    EXPECT_EQ(req["policy"]["conch"]["env"]["YR_TENANT_ID"], "dev");
    // JIUWENSWARM_HOME derived from host_user (default "agentos")
    EXPECT_EQ(req["policy"]["conch"]["env"]["JIUWENSWARM_HOME"], "/home/agentos");
}

TEST_F(ConchExecutorTest, CreateRequest_PosixEnvsCoexistWithHostUser)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    auto runtimeInfo = request->mutable_runtimeinstanceinfo();
    runtimeInfo->set_runtimeid("rt");
    (*runtimeInfo->mutable_deploymentconfig()->mutable_deployoptions())[HOST_USER] = "User9876";
    (*runtimeInfo->mutable_runtimeconfig()->mutable_posixenvs())["AGENT_SERVER_PORT"] = "18092";

    auto req = executor_->TestCreateRequest(request);

    EXPECT_EQ(req["policy"]["conch"]["env"]["AGENT_SERVER_PORT"], "18092");
    EXPECT_EQ(req["policy"]["conch"]["env"]["JIUWENSWARM_HOME"], "/home/User9876");
}

TEST_F(ConchExecutorTest, CreateRequest_OmitsSupervisorOnlyPolicySegments)
{
    // ConchPolicy is extra="forbid"; none of the supervisor-only segments may appear at
    // the top level of policy (environment/process/namespace/cgroup) nor inside conch.
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    auto runtimeInfo = request->mutable_runtimeinstanceinfo();
    runtimeInfo->set_runtimeid("rt");

    auto req = executor_->TestCreateRequest(request);

    const auto &policy = req["policy"];
    EXPECT_FALSE(policy.contains("environment"));   // bwrap-only
    EXPECT_FALSE(policy.contains("process"));
    EXPECT_FALSE(policy.contains("namespace"));
    EXPECT_FALSE(policy.contains("cgroup"));
    EXPECT_FALSE(policy.contains("filesystem_policy"));  // conch uses conch.filesystem_policy
    // conch object carries only template_id (+ env / filesystem_policy when non-empty)
    ASSERT_TRUE(policy.contains("conch"));
}

/**
 * Feature: CreateRequest default (no posixenvs, no host_user)
 */
TEST_F(ConchExecutorTest, CreateRequest_DefaultPolicy)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    request->mutable_runtimeinstanceinfo()->set_runtimeid("rt");

    auto req = executor_->TestCreateRequest(request);

    EXPECT_EQ(req["sandbox_runtime"], "conch");
    EXPECT_EQ(req["policy_mode"], "append");
    EXPECT_EQ(req["policy"]["conch"]["template_id"], "");
    // env always carries the default home dir; no user envs are merged in
    ASSERT_TRUE(req["policy"]["conch"].contains("env"));
    EXPECT_EQ(req["policy"]["conch"]["env"]["JIUWENSWARM_HOME"], "/home/agentos");
}

/**
 * Feature: BuildCommand
 */
TEST_F(ConchExecutorTest, BuildCommand_ShellQuotesTokensAndRedirects)
{
    auto start = std::make_shared<runtime::v1::StartRequest>();
    start->add_command("python3");
    start->add_command("-c");
    start->add_command("print('hello; rm -rf /')");
    start->set_stdout("/tmp/log dir/a'b.out");
    start->set_stderr("/tmp/log dir/a'b.err");

    auto command = executor_->TestBuildCommand(start);
    ASSERT_EQ(command.size(), 3);
    EXPECT_EQ(command[0], "sh");
    EXPECT_EQ(command[1], "-c");
    std::string cmdLine = command[2];

    EXPECT_THAT(cmdLine, testing::HasSubstr("'python3'"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("'-c'"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("'print('\\''hello; rm -rf /'\\''"));
    EXPECT_THAT(cmdLine, testing::HasSubstr(" >'/tmp/log dir/a'\\''b.out'"));
    EXPECT_THAT(cmdLine, testing::HasSubstr(" 2>'/tmp/log dir/a'\\''b.err'"));
    EXPECT_THAT(cmdLine, testing::Not(testing::HasSubstr("; rm -rf /')")));
}

TEST_F(ConchExecutorTest, BuildCommand_EmptyTokensAndPaths)
{
    auto start = std::make_shared<runtime::v1::StartRequest>();
    start->add_command("");
    start->add_command("arg with space");

    auto command = executor_->TestBuildCommand(start);
    ASSERT_EQ(command.size(), 3);
    std::string cmdLine = command[2];
    // Empty stdout/stderr (log dir prep failed): Layer 1 bare-run, no redirect, so
    // sh -c never fails on >'' / 2>'' (ENOENT).
    EXPECT_EQ(cmdLine, "exec '' 'arg with space'");
}

/**
 * Feature: BuildUdsHttpRequest
 */
TEST_F(ConchExecutorTest, BuildUdsHttpRequest_SpecialChars)
{
    std::string method = "POST";
    std::string path = "/api/v1/sandboxes";
    std::string body = R"({"key":"value with spaces & special chars !@#$%"})";

    std::string request = executor_->TestBuildUdsHttpRequest(method, path, body);

    EXPECT_THAT(request, testing::HasSubstr("POST /api/v1/sandboxes HTTP/1.1"));
    EXPECT_THAT(request, testing::HasSubstr("Content-Length: " + std::to_string(body.length())));
    EXPECT_THAT(request, testing::HasSubstr(body));
}

TEST_F(ConchExecutorTest, BuildUdsHttpRequest_EmptyBody)
{
    std::string request = executor_->TestBuildUdsHttpRequest("DELETE", "/api/v1/sandboxes/abc", "");

    EXPECT_THAT(request, testing::HasSubstr("DELETE /api/v1/sandboxes/abc HTTP/1.1"));
    EXPECT_THAT(request, testing::HasSubstr("Content-Length: 0"));
}

/**
 * Feature: CreateSandbox (failure path - jiuwenbox UDS unavailable)
 */
TEST_F(ConchExecutorTest, CreateSandbox_FailsWhenJiuwenboxUnavailable)
{
    std::string runtimeID = "test_cs_runtime_id";
    auto future = executor_->TestCreateSandbox(runtimeID, "host_user");

    ASSERT_AWAIT_READY_FOR(future, TEST_AWAIT_TIMEOUT);
    auto rsp = future.Get();
    // CreateSandbox resolves failures as a value (code != SUCCESS), not SetFailed
    EXPECT_EQ(rsp.code(), static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
    EXPECT_EQ(rsp.message(), "Failed to create sandbox");
    EXPECT_FALSE(executor_->TestIsRuntimeActive(runtimeID));
}

TEST_F(ConchExecutorTest, CreateSandbox_FailsWithEmptyHostUser)
{
    std::string runtimeID = "test_cs_empty_host";
    auto future = executor_->TestCreateSandbox(runtimeID, "");

    ASSERT_AWAIT_READY_FOR(future, TEST_AWAIT_TIMEOUT);
    auto rsp = future.Get();
    EXPECT_EQ(rsp.code(), static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
}

/**
 * Feature: DoDeleteSandbox (failure path - UDS unavailable -> SetFailed)
 */
TEST_F(ConchExecutorTest, DoDeleteSandbox_FailsWhenJiuwenboxUnavailable)
{
    auto req = std::make_shared<runtime::v1::DeleteRequest>();
    req->set_id("sandbox_to_delete");

    auto future = executor_->TestDoDeleteSandbox(req);

    ASSERT_AWAIT_SET_FOR(future, TEST_AWAIT_TIMEOUT);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(future.GetErrorCode(), static_cast<int>(StatusCode::ERR_INNER_COMMUNICATION));
}

/**
 * Feature: ExecInSandbox (failure path -> triggers sandbox cleanup)
 */
TEST_F(ConchExecutorTest, ExecInSandbox_FailsAndTriggersCleanup)
{
    std::string runtimeID = "test_exec_runtime_id";
    std::string sandboxId = "sandbox_exec_failure";

    executor_->SetRuntimeToSandboxID(runtimeID, sandboxId);

    auto start = std::make_shared<runtime::v1::StartRequest>();
    start->add_command("python3");
    start->add_command("-c");
    start->add_command("print('hello')");
    start->mutable_envs()->insert({ "KEY", "VALUE" });

    auto future = executor_->TestExecInSandbox(runtimeID, start, sandboxId);

    ASSERT_AWAIT_READY_FOR(future, TEST_AWAIT_TIMEOUT);
    auto rsp = future.Get();
    EXPECT_EQ(rsp.code(), static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
    EXPECT_EQ(rsp.message(), "Failed to execute command in sandbox");
    EXPECT_EQ(rsp.id(), "");
    EXPECT_EQ(executor_->GetSandboxIDByRuntimeID(runtimeID), "");
    EXPECT_FALSE(executor_->TestIsRuntimeActive(runtimeID));
}

/**
 * Feature: StartByRuntimeID
 */
TEST_F(ConchExecutorTest, StartByRuntimeID_RejectsIllegalCommandChars)
{
    auto request = GenStartInstanceRequest("python3");
    std::map<std::string, std::string> startParams = { { PARAM_EXEC_PATH, "/bin/echo$()" },
                                                       { PARAM_LANGUAGE, "python3" } };
    std::vector<std::string> buildArgs = { "/bin/echo$()", "arg" };
    Envs envs;

    auto future = executor_->TestStartByRuntimeID(request, startParams, buildArgs, envs);

    ASSERT_AWAIT_READY_FOR(future, TEST_AWAIT_TIMEOUT);
    auto rsp = future.Get();
    EXPECT_EQ(rsp.code(), static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
    EXPECT_THAT(rsp.message(), testing::HasSubstr("/bin/echo$()"));
}

TEST_F(ConchExecutorTest, StartByRuntimeID_ReturnsFailureResponseWhenCreateSandboxFails)
{
    auto request = GenStartInstanceRequest("python3");
    std::map<std::string, std::string> startParams = { { PARAM_EXEC_PATH, "/usr/bin/python3" },
                                                       { PARAM_LANGUAGE, "python3" } };
    std::vector<std::string> buildArgs = { "/usr/bin/python3" };
    Envs envs;

    auto future = executor_->TestStartByRuntimeID(request, startParams, buildArgs, envs);

    ASSERT_AWAIT_READY_FOR(future, TEST_AWAIT_TIMEOUT);
    auto rsp = future.Get();
    EXPECT_EQ(rsp.code(), static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
    EXPECT_EQ(rsp.message(), "Failed to create sandbox");
}

/**
 * Feature: StartRuntime
 */
TEST_F(ConchExecutorTest, StartRuntime_PropagatesFailureResponseWithCreateExecFailed)
{
    auto request = GenStartInstanceRequest("python3");
    Envs envs;
    std::vector<std::string> args = { "/usr/bin/python3" };

    auto future = executor_->TestStartRuntime(request, "python3", envs, args);

    ASSERT_AWAIT_READY_FOR(future, TEST_AWAIT_TIMEOUT);
    auto rsp = future.Get();
    EXPECT_NE(rsp.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(rsp.startruntimeinstanceresponse().executortype(), static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    EXPECT_THAT(rsp.message(), testing::HasSubstr("Failed to create sandbox"));
    EXPECT_EQ(rsp.requestid(), "test_request_id");
}

TEST_F(ConchExecutorTest, StartRuntime_RejectsIllegalCommandChars)
{
    auto request = GenStartInstanceRequest("python3", "/bin/echo$()");
    Envs envs;
    std::vector<std::string> args = { "arg" };

    auto future = executor_->TestStartRuntime(request, "python3", envs, args);

    ASSERT_AWAIT_READY_FOR(future, TEST_AWAIT_TIMEOUT);
    auto rsp = future.Get();
    EXPECT_NE(rsp.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(rsp.startruntimeinstanceresponse().executortype(), static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    EXPECT_THAT(rsp.message(), testing::HasSubstr("/bin/echo$()"));
}

/**
 * Feature: StopAllSandboxes
 */
TEST_F(ConchExecutorTest, StopAllSandboxes_NoSandboxes)
{
    EXPECT_EQ(executor_->GetRuntimeToSandboxIDMapSize(), 0);

    auto resultFuture = executor_->StopAllSandboxes();

    ASSERT_AWAIT_READY(resultFuture);
    auto result = resultFuture.Get();
    EXPECT_TRUE(result);
    EXPECT_EQ(executor_->GetRuntimeToSandboxIDMapSize(), 0);
}

TEST_F(ConchExecutorTest, StopAllSandboxes_StateTransition)
{
    executor_->SetRuntimeToSandboxID("runtime1", "sandbox1");
    executor_->SetRuntimeToSandboxID("runtime2", "sandbox2");

    EXPECT_EQ(executor_->GetRuntimeToSandboxIDMapSize(), 2);
    EXPECT_TRUE(executor_->TestIsRuntimeActive("runtime1"));
    EXPECT_TRUE(executor_->TestIsRuntimeActive("runtime2"));

    auto resultFuture = executor_->StopAllSandboxes();
    ASSERT_AWAIT_READY(resultFuture);
    auto result = resultFuture.Get();
    EXPECT_TRUE(result);
    EXPECT_EQ(executor_->GetRuntimeToSandboxIDMapSize(), 0);
}

TEST_F(ConchExecutorTest, StopAllSandboxes_AfterStopAll)
{
    executor_->SetRuntimeToSandboxID("runtime1", "sandbox1");
    executor_->SetRuntimeToSandboxID("runtime2", "sandbox2");

    auto resultFuture1 = executor_->StopAllSandboxes();
    ASSERT_AWAIT_READY(resultFuture1);
    // StopAllSandboxes clears runtime2sandboxID_ itself once all deletes resolve; no
    // manual map clearing should be needed (that would mask a cleanup defect).
    EXPECT_EQ(executor_->GetRuntimeToSandboxIDMapSize(), 0);

    auto resultFuture2 = executor_->StopAllSandboxes();
    ASSERT_AWAIT_READY(resultFuture2);
    auto result2 = resultFuture2.Get();
    EXPECT_TRUE(result2);
    EXPECT_EQ(executor_->GetRuntimeToSandboxIDMapSize(), 0);
}

}  // namespace functionsystem::runtime_manager
