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
#include <filesystem>
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
using functionsystem::resource_view::CPU_RESOURCE_NAME;
using functionsystem::resource_view::MEMORY_RESOURCE_NAME;

class MockConchExecutor : public ConchExecutor {
public:
    MockConchExecutor(const std::string &name, const litebus::AID &functionAgentAID)
        : ConchExecutor(name, functionAgentAID)
    {
    }

    // Expose protected/private members for testing (access enforced off via -fno-access-control
    // on the UNIT_TEST_MODULE target, same as docker/supervisor executor tests).
    std::string TestBuildUdsHttpRequest(const std::string &method, const std::string &path, const std::string &body)
    {
        return AsyncUdsClient::BuildHttpRequest(method, path, body);
    }

    void TestParseResponse(litebus::Promise<nlohmann::json> promise, std::string response)
    {
        try {
            promise.SetValue(ParseRawResponse(response));
        } catch (std::exception const &) {
            promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        }
    }

    litebus::Future<nlohmann::json> TestRequestAsync(const std::string &socketPath)
    {
        // parser exceptions are caught by CompleteRequest and converted to a failed Future
        // (FailStage::PARSE), so no try/catch is needed here — matches SendRequestToConch.
        return AsyncUdsClient::RequestAsync({ socketPath, "GET", "/", nlohmann::json::object(), "Conch",
            [](const std::string &rawResponse) { return ParseRawResponse(rawResponse); } });
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
                                                                  const std::string &hostUser = "")
    {
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
        return CreateSandbox(request);
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

    std::string GetSandboxIDByRuntimeID(const std::string &runtimeID) const
    {
        auto it = runtime2sandboxID_.find(runtimeID);
        return (it != runtime2sandboxID_.end()) ? it->second : "";
    }
};

class ConchExecutorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        executor_ = std::make_shared<MockConchExecutor>("TestConchExecutor",
                                                        litebus::AID("FunctionAgent", "127.0.0.1:8080"));
        litebus::Spawn(executor_, false);

        // Create test deploy directory
        testDeployDir_ = "/tmp/test-conch-executor";
        litebus::os::Mkdir(testDeployDir_);
        litebus::os::Mkdir(testDeployDir_ + "/layer/func");
        TouchFile(testDeployDir_ + "/layer/func/funcObj");

        // Initialize config. runtimeLogPath points at a directory this suite never provisions
        // itself, but earlier start-path tests create it via Executor::ConfigRuntimeRedirectLog
        // (and CI runners reuse /tmp across builds), so remove any stale copy below — otherwise
        // BindHostLogDir would append a log-dir mount and CreateRequest's filesystem_policy
        // emission would depend on test execution order.
        RuntimeConfig config;
        config.runtimePath = "/tmp";
        config.runtimeLogPath = "/tmp/test-conch-executor-no-log-dir";
        config.runtimeStdLogDir = "std";
        executor_->config_ = config;
        std::filesystem::remove_all(config.runtimeLogPath);
    }

    void TearDown() override
    {
        litebus::os::Rmdir(testDeployDir_);
        // Leave no /tmp log dir behind for the next build to trip over.
        std::filesystem::remove_all(executor_->config_.runtimeLogPath);
        litebus::Terminate(executor_->GetAID());
        litebus::Await(executor_->GetAID());
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

    // Set a scalar resource (e.g. "CPU"/"Memory") with value/limit on the request's
    // runtimeconfig.resources map. Matches how SetRuntimeConfig populates resources
    // from schedulingops in production (struct_transfer.h:SetInstanceInfoResources).
    void SetRuntimeResource(const std::shared_ptr<messages::StartInstanceRequest> &request,
                            const std::string &name, double value, double limit = 0)
    {
        auto resource = request->mutable_runtimeinstanceinfo()
                            ->mutable_runtimeconfig()
                            ->mutable_resources()
                            ->mutable_resources();
        resource_view::Resource r;
        r.set_name(name);
        r.set_type(resource_view::ValueType::Value_Type_SCALAR);
        r.mutable_scalar()->set_value(value);
        r.mutable_scalar()->set_limit(limit);
        (*resource)[name] = std::move(r);
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

    // Build a minimal StartInstanceRequest carrying a rootfs deploy option for CreateRequest tests.
    std::shared_ptr<messages::StartInstanceRequest> GenConchCreateRequest(const std::string &rootfs,
                                                                          const std::string &runtimeID = "rt")
    {
        auto request = std::make_shared<messages::StartInstanceRequest>();
        request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
        auto runtimeInfo = request->mutable_runtimeinstanceinfo();
        runtimeInfo->set_runtimeid(runtimeID);
        if (!rootfs.empty()) {
            (*runtimeInfo->mutable_deploymentconfig()->mutable_deployoptions())[CONTAINER_ROOTFS] = rootfs;
        }
        return request;
    }

    std::string testDeployDir_;
    std::shared_ptr<MockConchExecutor> executor_;
};

/**
 * Feature: ParseResponse
 * Description: Test parsing HTTP responses from the conch backend
 */
TEST_F(ConchExecutorTest, ParseResponse_ValidResponse)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 20\r\n"
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

    // No "\r\n\r\n" separator -> ParseRawResponse rejects the response and fails the promise.
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

    // Empty body (non-204) -> ParseRawResponse fails the promise with ERR_INNER_COMMUNICATION.
    ASSERT_AWAIT_SET(future);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(future.GetErrorCode(), static_cast<int>(StatusCode::ERR_INNER_COMMUNICATION));
}

TEST_F(ConchExecutorTest, ParseResponse_204NoContent)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string response =
        "HTTP/1.1 204 No Content\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    executor_->TestParseResponse(promise, response);

    // 204 (DELETE success) -> empty object value, not an error.
    ASSERT_AWAIT_READY(future);
    auto json = future.Get();
    EXPECT_TRUE(json.is_object());
    EXPECT_TRUE(json.empty());
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

    // Unparseable JSON body -> ParseRawResponse catches the exception and fails the promise.
    ASSERT_AWAIT_SET(future);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(future.GetErrorCode(), static_cast<int>(StatusCode::ERR_INNER_COMMUNICATION));
}

// A non-2xx must surface the body's root cause as a synthesized {"error_message": ...} value
// (not SetFailed), so OnCreateSandboxDone / OnExecInSandboxDone / OnDeleteSandboxDone propagate
// the detail upstream. The IsError channel stays reserved for transport/protocol failures.
TEST_F(ConchExecutorTest, ParseResponse_Non2xxSynthesizesErrorMessage)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string body = R"({"error":"host_path does not exist: /data/host"})";
    std::string response =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Length: " +
        std::to_string(body.size()) + "\r\n"
        "\r\n" +
        body;

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_READY(future);
    EXPECT_FALSE(future.IsError());
    auto json = future.Get();
    ASSERT_TRUE(json.contains("error_message"));
    EXPECT_EQ(json["error_message"], "host_path does not exist: /data/host");
}

TEST_F(ConchExecutorTest, ParseResponse_Non2xxExtractsMessageField)
{
    // conchd emits {"message": ...}; ExtractHttpErrorDetail recognizes the "message" key too.
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string body = R"({"message":"conchd unreachable"})";
    std::string response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\n\r\n" + body;

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_READY(future);
    EXPECT_FALSE(future.IsError());
    EXPECT_EQ(future.Get()["error_message"], "conchd unreachable");
}

TEST_F(ConchExecutorTest, ParseResponse_Non2xxFallsBackToRawBody)
{
    // Non-JSON body -> fall back to the raw body text.
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string body = "Bad Gateway";
    std::string response = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\n\r\n" + body;

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_READY(future);
    EXPECT_FALSE(future.IsError());
    EXPECT_EQ(future.Get()["error_message"], "Bad Gateway");
}

TEST_F(ConchExecutorTest, ParseResponse_Non2xxEmptyBody)
{
    // Empty body on a non-2xx -> "HTTP <status>" fallback.
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_READY(future);
    EXPECT_FALSE(future.IsError());
    EXPECT_EQ(future.Get()["error_message"], "HTTP 400");
}

TEST_F(ConchExecutorTest, ParseResponse_Non2xxTruncatesLongDetail)
{
    // A root cause longer than MAX_HTTP_ERROR_DETAIL_LEN is truncated with a sentinel suffix.
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    const std::string longMsg(600, 'a');
    std::string body = R"({"error":")" + longMsg + R"("})";
    std::string response = "HTTP/1.1 400 Bad Request\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\n\r\n" + body;

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_READY(future);
    EXPECT_FALSE(future.IsError());
    std::string err = future.Get()["error_message"];
    EXPECT_THAT(err, testing::HasSubstr("...(truncated)"));
    EXPECT_EQ(err.substr(0, 512), std::string(512, 'a'));
}

TEST_F(ConchExecutorTest, ParseResponse_Admits201SoftFailure)
{
    // jiuwenbox returns 201 + phase=error + error_message on soft failures (conchd unreachable,
    // SDK not installed, template missing). 201 is 2xx, so it is admitted as a value (carrying
    // error_message) for OnCreateSandboxDone to treat as a failure — not a transport error.
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> future = promise.GetFuture();

    std::string body = R"({"id":"sbx1","phase":"error","error_message":"conch sdk not installed"})";
    std::string response = "HTTP/1.1 201 Created\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\n\r\n" + body;

    executor_->TestParseResponse(promise, response);

    ASSERT_AWAIT_READY(future);
    EXPECT_FALSE(future.IsError());
    auto json = future.Get();
    EXPECT_EQ(json["error_message"], "conch sdk not installed");
}

/**
 * Feature: RequestAsync connect failure
 * Description: With no jiuwenbox listening, RequestAsync must resolve the Future to an error
 *              (SetFailed) rather than hanging.
 */
TEST_F(ConchExecutorTest, RequestAsync_FailsWhenConchUnavailable)
{
    auto future = executor_->TestRequestAsync("/nonexistent/socket/path");
    ASSERT_AWAIT_SET_FOR(future, TEST_AWAIT_TIMEOUT);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(AsyncUdsClient::DecodeErrorStage(future.GetErrorCode()), AsyncUdsClient::RequestErrorStage::CONNECT);
}

TEST_F(ConchExecutorTest, RequestAsync_FailsWhenPathTooLong)
{
    std::string longPath(500, 'a');  // Exceeds sockaddr_un sun_path length
    auto future = executor_->TestRequestAsync(longPath);
    ASSERT_AWAIT_SET_FOR(future, TEST_AWAIT_TIMEOUT);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(AsyncUdsClient::DecodeErrorStage(future.GetErrorCode()), AsyncUdsClient::RequestErrorStage::CONNECT);
}

TEST_F(ConchExecutorTest, RequestAsync_FailsWhenNoServer)
{
    std::string socketPath = "/tmp/test_conch_socket_" + std::to_string(getpid());
    auto future = executor_->TestRequestAsync(socketPath);
    ASSERT_AWAIT_SET_FOR(future, TEST_AWAIT_TIMEOUT);
    EXPECT_TRUE(future.IsError());
    EXPECT_EQ(AsyncUdsClient::DecodeErrorStage(future.GetErrorCode()), AsyncUdsClient::RequestErrorStage::CONNECT);
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

/**
 * Feature: IsRuntimeActive
 */
TEST_F(ConchExecutorTest, IsRuntimeActive_RuntimeExists)
{
    std::string runtimeID = "active_runtime_id";
    executor_->SetRuntimeToSandboxID(runtimeID, "sandbox123");

    EXPECT_TRUE(executor_->TestIsRuntimeActive(runtimeID));
}

TEST_F(ConchExecutorTest, IsRuntimeActive_RuntimeNotExists)
{
    EXPECT_FALSE(executor_->TestIsRuntimeActive("inactive_runtime_id"));
}

TEST_F(ConchExecutorTest, IsRuntimeActive_EmptyRuntimeID)
{
    EXPECT_FALSE(executor_->TestIsRuntimeActive(""));
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
 * Feature: StopInstance
 */
TEST_F(ConchExecutorTest, StopInstance_RuntimeNotExists)
{
    auto request = GenStopInstanceRequest("nonexistent_runtime_id");

    auto statusFuture = executor_->StopInstance(request, false);

    ASSERT_AWAIT_READY(statusFuture);
    auto status = statusFuture.Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::SUCCESS);
}

TEST_F(ConchExecutorTest, StopInstance_CleansUpOnDeleteFailure)
{
    // No jiuwenbox listening: the delete future resolves to a value carrying error_message
    // (non-2xx has no socket here, so the request fails at connect -> IsError -> delete fails).
    std::string runtimeID = "test_runtime_id_stop";
    std::string sandboxID = "sandbox_stop_123";
    executor_->SetRuntimeToSandboxID(runtimeID, sandboxID);

    auto request = GenStopInstanceRequest(runtimeID);
    auto statusFuture = executor_->StopInstance(request, false);

    ASSERT_AWAIT_READY(statusFuture);
    auto status = statusFuture.Get();
    EXPECT_NE(status.StatusCode(), StatusCode::SUCCESS);
    EXPECT_EQ(executor_->GetSandboxIDByRuntimeID(runtimeID), "");  // mapping cleared regardless
}

/**
 * Feature: StartInstance with different languages (smoke; full path needs a live jiuwenbox)
 */
TEST_F(ConchExecutorTest, StartInstance_PythonLanguage)
{
    auto request = GenStartInstanceRequest("python3");
    std::vector<int> cardIDs;
    auto responseFuture = executor_->StartInstance(request, cardIDs);
    (void)responseFuture;  // exercises the python dynamic-runtimePath arg construction
}

TEST_F(ConchExecutorTest, StartInstance_CppLanguage)
{
    auto request = GenStartInstanceRequest("cpp");
    std::vector<int> cardIDs;
    auto responseFuture = executor_->StartInstance(request, cardIDs);
    (void)responseFuture;
}

/**
 * Feature: CreateSandbox
 * Description: With no jiuwenbox listening, CreateSandbox resolves to a failure StartResponse
 *              (code != SUCCESS, not SetFailed) so StartByRuntimeID/OnCreateSandboxDone can
 *              propagate the message. No sandbox is registered on failure.
 */
TEST_F(ConchExecutorTest, CreateSandbox_FailsWhenConchUnavailable)
{
    std::string runtimeID = "test_cs_runtime_id";
    auto future = executor_->TestCreateSandbox(runtimeID, "host_user");

    ASSERT_AWAIT_READY_FOR(future, TEST_AWAIT_TIMEOUT);
    auto rsp = future.Get();
    EXPECT_EQ(rsp.code(), static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
    EXPECT_EQ(rsp.message(), "Failed to create sandbox");
    EXPECT_FALSE(executor_->TestIsRuntimeActive(runtimeID));
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
 * Feature: BuildUdsHttpRequest with special characters in body
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

/**
 * Feature: BuildCommand tolerant redirect
 * Description: Empty stdout -> bare exec (no redirect). Non-empty -> probe writability in a
 *              subshell, append on success, fall back to inherited fds on failure.
 */
TEST_F(ConchExecutorTest, BuildCommand_EmptyStdoutBareExec)
{
    auto start = std::make_shared<runtime::v1::StartRequest>();
    start->add_command("python3");
    start->add_command("-c");
    start->add_command("print('hi')");

    auto cmd = executor_->TestBuildCommand(start);

    ASSERT_TRUE(cmd.is_array());
    ASSERT_EQ(cmd.size(), 3);
    EXPECT_EQ(cmd[0], "sh");
    EXPECT_EQ(cmd[1], "-c");
    std::string cmdLine = cmd[2].get<std::string>();
    EXPECT_THAT(cmdLine, testing::StartsWith("exec "));
    EXPECT_THAT(cmdLine, testing::Not(testing::HasSubstr("if ( ")));  // no probe when path empty
}

TEST_F(ConchExecutorTest, BuildCommand_NonEmptyStdoutTolerantRedirect)
{
    auto start = std::make_shared<runtime::v1::StartRequest>();
    start->add_command("python3");
    start->set_stdout("/tmp/conch_test_log");

    auto cmd = executor_->TestBuildCommand(start);

    ASSERT_EQ(cmd.size(), 3);
    EXPECT_EQ(cmd[0], "sh");
    EXPECT_EQ(cmd[1], "-c");
    std::string cmdLine = cmd[2].get<std::string>();
    // Probe + append redirect + 2>&1 merge + fallback branch.
    EXPECT_THAT(cmdLine, testing::HasSubstr("if ( >>"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("2>/dev/null"));
    EXPECT_THAT(cmdLine, testing::HasSubstr(">>"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("2>&1"));
    EXPECT_THAT(cmdLine, testing::HasSubstr("else exec "));
}

/**
 * Feature: CreateRequest (the conch-specific contract)
 * Description: CreateRequest must emit sandbox_runtime:"conch", policy.conch.template_name
 *              from imageurl, bind_mounts under policy.conch.filesystem_policy.bind_mounts,
 *              and guest env under policy.conch.env. It must NOT emit the supervisor-only
 *              policy.environment / process / namespace / cgroup segments (ConchPolicy is
 *              extra="forbid").
 */
TEST_F(ConchExecutorTest, CreateRequest_EmitsConchRuntimeAndTemplateName)
{
    auto request = GenConchCreateRequest(R"({"type":"image","imageurl":"sha256:abc123"})");

    auto req = executor_->TestCreateRequest(request);

    // top-level routing field routes jiuwenbox to the conch backend
    EXPECT_EQ(req["sandbox_runtime"], "conch");
    EXPECT_EQ(req["policy_mode"], "append");
    // template_name sourced from rootfs imageurl
    ASSERT_TRUE(req["policy"]["conch"].contains("template_name"));
    EXPECT_EQ(req["policy"]["conch"]["template_name"], "sha256:abc123");
}

TEST_F(ConchExecutorTest, CreateRequest_EmptyImageurlYieldsEmptyTemplateName)
{
    // No rootfs -> ParseRootfsImageUrl returns "" -> conch.template_name stays empty, jiuwenbox
    // falls back to JIUWENBOX_CONCH_TEMPLATE_NAME / conchd default_template_name.
    auto request = GenConchCreateRequest("");

    auto req = executor_->TestCreateRequest(request);

    EXPECT_EQ(req["sandbox_runtime"], "conch");
    EXPECT_EQ(req["policy"]["conch"]["template_name"], "");
}

TEST_F(ConchExecutorTest, CreateRequest_BindMountsUnderConchFilesystemPolicy)
{
    auto request = GenConchCreateRequest(
        R"({"type":"image","imageurl":"sha256:abc","mounts":[)"
        R"({"source":"/data/host","target":"/data","readonly":true}]})");

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
    auto request = GenConchCreateRequest(R"({"type":"image","imageurl":"sha256:abc"})");

    auto req = executor_->TestCreateRequest(request);

    // With no rootfs mounts and no host log dir (guaranteed absent by SetUp),
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
    auto request = GenConchCreateRequest(R"({"type":"image","imageurl":"sha256:abc"})");

    auto req = executor_->TestCreateRequest(request);

    // conch policy is extra="forbid": supervisor-only segments must NOT appear at top level.
    EXPECT_FALSE(req["policy"].contains("environment"));
    EXPECT_FALSE(req["policy"].contains("process"));
    EXPECT_FALSE(req["policy"].contains("namespace"));
    EXPECT_FALSE(req["policy"].contains("cgroup"));
    EXPECT_FALSE(req["policy"].contains("filesystem_policy"));  // bwrap-only, top-level
    // and not under policy.conch either
    EXPECT_FALSE(req["policy"]["conch"].contains("environment"));
    EXPECT_FALSE(req["policy"]["conch"].contains("process"));
    EXPECT_FALSE(req["policy"]["conch"].contains("namespace"));
    EXPECT_FALSE(req["policy"]["conch"].contains("cgroup"));
}

TEST_F(ConchExecutorTest, CreateRequest_MapsRuntimeResourcesToVcpuNumAndRamMb)
{
    // CPU is milli-cores (2000 -> 2 vCPU), Memory is MB (4096 -> ram_mb 4096).
    // vcpu_max is emitted equal to vcpu_num: conchd defaults an omitted one to 2 and
    // rejects vcpu_max < vcpu_num, so e.g. a 4-vCPU sandbox would fail to start.
    auto request = GenConchCreateRequest(R"({"type":"image","imageurl":"sha256:abc"})");
    SetRuntimeResource(request, CPU_RESOURCE_NAME, 2000);
    SetRuntimeResource(request, MEMORY_RESOURCE_NAME, 4096);

    auto req = executor_->TestCreateRequest(request);

    EXPECT_EQ(req["policy"]["conch"]["vcpu_num"], 2);
    EXPECT_EQ(req["policy"]["conch"]["vcpu_max"], 2);
    EXPECT_EQ(req["policy"]["conch"]["ram_mb"], 4096);
}

TEST_F(ConchExecutorTest, CreateRequest_LimitPrecedenceForVcpu)
{
    // A positive scalar limit takes precedence over value (same semantics as docker BuildResources).
    auto request = GenConchCreateRequest(R"({"type":"image","imageurl":"sha256:abc"})");
    SetRuntimeResource(request, CPU_RESOURCE_NAME, 2000, 3000);  // value=2000, limit=3000

    auto req = executor_->TestCreateRequest(request);

    EXPECT_EQ(req["policy"]["conch"]["vcpu_num"], 3);  // ceil(3000/1000)
    EXPECT_EQ(req["policy"]["conch"]["vcpu_max"], 3);
}

TEST_F(ConchExecutorTest, CreateRequest_SubCoreCpuRoundsUpToOneVcpu)
{
    auto request = GenConchCreateRequest(R"({"type":"image","imageurl":"sha256:abc"})");
    SetRuntimeResource(request, CPU_RESOURCE_NAME, 500);  // 0.5 core -> rounds up to 1 vCPU

    auto req = executor_->TestCreateRequest(request);

    EXPECT_EQ(req["policy"]["conch"]["vcpu_num"], 1);
    EXPECT_EQ(req["policy"]["conch"]["vcpu_max"], 1);
}

TEST_F(ConchExecutorTest, CreateRequest_OmitsResourcesWhenAbsentOrNonPositive)
{
    // No resources set -> vcpu_num / vcpu_max / ram_mb omitted so conch applies its own default.
    auto request = GenConchCreateRequest(R"({"type":"image","imageurl":"sha256:abc"})");

    auto req = executor_->TestCreateRequest(request);

    EXPECT_FALSE(req["policy"]["conch"].contains("vcpu_num"));
    EXPECT_FALSE(req["policy"]["conch"].contains("vcpu_max"));
    EXPECT_FALSE(req["policy"]["conch"].contains("ram_mb"));
}

TEST_F(ConchExecutorTest, CreateRequest_CarriesTraceAndInstanceIds)
{
    // The agentos sandbox-naming/trace feature: the create body carries trace_id /
    // instance_id / runtime_id, and sandbox_id == instance_id so "jiuwenbox sandbox ls" maps
    // 1:1 to the user-facing instance.
    auto request = std::make_shared<messages::StartInstanceRequest>();
    request->set_type(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
    auto runtimeInfo = request->mutable_runtimeinstanceinfo();
    runtimeInfo->set_runtimeid("rt-1");
    runtimeInfo->set_instanceid("inst-1");
    runtimeInfo->set_traceid("trace-1");

    auto req = executor_->TestCreateRequest(request);

    EXPECT_EQ(req["trace_id"], "trace-1");
    EXPECT_EQ(req["instance_id"], "inst-1");
    EXPECT_EQ(req["runtime_id"], "rt-1");
    EXPECT_EQ(req["sandbox_id"], "inst-1");
}

}  // namespace functionsystem::runtime_manager
