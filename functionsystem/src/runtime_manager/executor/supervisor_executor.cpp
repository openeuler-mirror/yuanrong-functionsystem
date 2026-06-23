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

#include "supervisor_executor.h"

#include <sys/un.h>
#include <sys/wait.h>

#include <csignal>
#include <cstring>
#include <sstream>
#include <utility>

#include "async/asyncafter.hpp"
#include "common/logs/logging.h"
#include "common/metadata/metadata_type.h"
#include "common/utils/collect_status.h"
#include "common/utils/exec_utils.h"
#include "common/utils/files.h"
#include "common/utils/generate_message.h"
#include "common/utils/time_utils.h"
#include "exec/exec.hpp"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "nlohmann/json.hpp"
#include "utils/os_utils.hpp"

namespace functionsystem::runtime_manager {
constexpr int64_t RECONNECT_SUPERVISOR_INTERVAL_MS = 5000;
constexpr int64_t HEALTH_CHECK_INTERVAL_MS = 100;
constexpr int64_t HTTP_TIMEOUT_MS = 30000;
constexpr unsigned short DEFAULT_SUPERVISOR_PORT = 9321;
const std::string YR_ONLY_STDOUT = "YR_ONLY_STDOUT";
const std::string SUPERVISOR_SANDBOX_PREFIX = "/api/v1/sandboxes";
const std::string SUPERVISOR_LOG_PATH = "/tmp/supervisor";
const std::string ERR_LOG = litebus::os::Join(SUPERVISOR_LOG_PATH, "supervisor_stderr.log");
const std::string OUT_LOG = litebus::os::Join(SUPERVISOR_LOG_PATH, "supervisor_stdout.log");
const std::string SUPERVISOR_UDS_SOCKET = "/run/jiuwenbox/jiuwenbox.sock";
const std::string SUPERVISOR_LISTEN_URL = "unix:///run/jiuwenbox/jiuwenbox.sock";

SupervisorExecutor::SupervisorExecutor(const std::string &name, const litebus::AID &functionAgentAID)
    : Executor(name), functionAgentAID_(functionAgentAID)
{
}

void SupervisorExecutor::Init()
{
    YRLOG_INFO("Start init SupervisorExecutor");
}

void SupervisorExecutor::Finalize()
{
    YRLOG_INFO("Start finalize SupervisorExecutor");
    runtime2portMappings_.clear();
    StopSupervisorProcess();
    Executor::Finalize();
}

void SupervisorExecutor::InitConfig()
{
    cmdBuilder_.SetRuntimeConfig(config_);

    LaunchSupervisorProcess();
}

void SupervisorExecutor::LaunchSupervisorProcess()
{
    auto supervisorListenUrl = litebus::os::GetEnv("SUPERVISOR_LISTEN_URL");
    if (supervisorListenUrl.IsNone()) {
        YRLOG_INFO("supervisor executor disabled, no supervisorListenUrl found");
        return;
    }

    CreateSupervisorLogs();

    std::string supervisorListenUrlStr =
        supervisorListenUrl.IsSome() ? supervisorListenUrl.Get() : SUPERVISOR_LISTEN_URL;

    // Use UDS (Unix Domain Socket) to start jiuwenbox service
    std::vector<std::string> argv = { "env", "JIUWENBOX_LISTEN=" + supervisorListenUrlStr, "python", "-m",
                                      "jiuwenbox.server.launcher" };
    auto exec =
        litebus::Exec::CreateExec("env", argv, litebus::None(), litebus::ExecIO::CreateFileIO("/dev/null"),
                                  litebus::ExecIO::CreateFileIO(OUT_LOG), litebus::ExecIO::CreateFileIO(ERR_LOG),
                                  { litebus::ChildInitHook::EXITWITHPARENT() });
    if (!exec) {
        YRLOG_ERROR("failed to create exec");
        return;
    }

    supervisorPid_ = exec->GetPid();
    exec_ = std::move(exec);
    YRLOG_INFO("success to started supervisor process, pid={},uds={}", supervisorPid_, SUPERVISOR_LISTEN_URL);
}

void SupervisorExecutor::StopSupervisorProcess()
{
    if (supervisorPid_ > 0) {
        YRLOG_INFO("stopping supervisor process with pid: {}", supervisorPid_);
        kill(supervisorPid_, SIGTERM);
        int status;
        waitpid(supervisorPid_, &status, 0);
        YRLOG_INFO("process stopped, pid={}", supervisorPid_);
        supervisorPid_ = -1;
    }
}

void SupervisorExecutor::CreateSupervisorLogs()
{
    litebus::os::Mkdir(SUPERVISOR_LOG_PATH);

    if (litebus::os::ExistPath(ERR_LOG)) {
        litebus::os::Rm(ERR_LOG);
    }
    TouchFile(ERR_LOG);

    if (litebus::os::ExistPath(OUT_LOG)) {
        litebus::os::Rm(OUT_LOG);
    }
    TouchFile(OUT_LOG);
}

void SupervisorExecutor::ParseResponse(litebus::Promise<nlohmann::json> promise, std::string response)
{
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        YRLOG_ERROR("invalid HTTP response (no header/body separator)");
        promise.SetValue(nlohmann::json::object());
        return;
    }
    std::string respBody = response.substr(headerEnd + 4);
    if (respBody.empty()) {
        YRLOG_ERROR("HTTP response body is empty");
        promise.SetValue(nlohmann::json::object());
        return;
    }
    try {
        YRLOG_INFO("UDS request success, response: {}", respBody);
        auto jsonResp = nlohmann::json::parse(respBody);
        promise.SetValue(jsonResp);
    } catch (std::exception const &e) {
        YRLOG_ERROR("failed to parse response: {}", e.what());
        promise.SetValue(nlohmann::json::object());
    }
}

litebus::Future<nlohmann::json> SupervisorExecutor::SendRequestToSupervisor(const std::string &method,
                                                                            const std::string &path,
                                                                            const nlohmann::json &body)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> result = promise.GetFuture();
    int fd = ConnectUdsSocket(SUPERVISOR_UDS_SOCKET);
    if (fd < 0) {
        YRLOG_ERROR("failed to connect to UDS socket: {}", SUPERVISOR_UDS_SOCKET);
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return result;
    }
    std::string httpRequest = BuildUdsHttpRequest(method, path, body.dump());
    if (ssize_t sent = send(fd, httpRequest.c_str(), httpRequest.length(), 0);
        sent < 0 || static_cast<size_t>(sent) != httpRequest.length()) {
        YRLOG_ERROR("failed to send request to UDS socket: {}", std::strerror(errno));
        (void)close(fd);
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return result;
    }
    // Receive response
    std::string response;
    char buf[4096];
    ssize_t received;
    while ((received = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[received] = '\0';
        response += buf;
        // Check if we have complete HTTP response (headers + body)
        size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            continue;
        }
        std::string headers = response.substr(0, headerEnd);
        size_t bodyStart = headerEnd + 4;
        // Parse Content-Length from headers
        size_t contentLengthPos = headers.find("Content-Length:");
        if (contentLengthPos == std::string::npos) {
            // No Content-Length, assume we have complete response if connection closed
            break;
        }
        size_t crlfPos = headers.find("\r\n", contentLengthPos);
        std::string contentLengthStr = headers.substr(contentLengthPos + 15, crlfPos - contentLengthPos - 15);
        size_t contentLength = std::stoul(contentLengthStr);
        if (response.length() - bodyStart >= contentLength) {
            break;
        }
    }
    if (received < 0) {
        YRLOG_ERROR("failed to receive response from UDS socket: {}", std::strerror(errno));
        (void)close(fd);
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return result;
    }
    (void)close(fd);
    // Parse HTTP response
    ParseResponse(promise, response);
    return result;
}

int SupervisorExecutor::ConnectUdsSocket(const std::string &socketPath)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        YRLOG_ERROR("failed to create UDS socket: {}", std::strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (socketPath.length() >= sizeof(addr.sun_path)) {
        YRLOG_ERROR("socket path too long: {}", socketPath);
        (void)close(fd);
        return -1;
    }
    (void)strncpy_s(addr.sun_path, sizeof(addr.sun_path), socketPath.c_str(), socketPath.length());
    if (::connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        YRLOG_ERROR("failed to connect to UDS {}: {}", socketPath, std::strerror(errno));
        (void)close(fd);
        return -1;
    }

    YRLOG_DEBUG("connected to UDS socket: {}", socketPath);
    return fd;
}

std::string SupervisorExecutor::BuildUdsHttpRequest(const std::string &method, const std::string &path,
                                                    const std::string &body)
{
    std::ostringstream oss;
    oss << method << " " << path << " HTTP/1.1\r\n";
    oss << "Host: localhost\r\n";
    oss << "Content-Type: application/json\r\n";
    oss << "Content-Length: " << body.length() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

litebus::Future<messages::StartInstanceResponse> SupervisorExecutor::StartInstance(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs)
{
    const auto &info = request->runtimeinstanceinfo();

    auto runtimeID = info.runtimeid();
    auto instanceID = info.instanceid();

    std::string language = info.runtimeconfig().language();
    std::string port;
    auto tlsConfig = info.runtimeconfig().tlsconfig();
    RuntimeFeatures features;
    if (tlsConfig.enableservermode()) {
        port = tlsConfig.posixport();
        features.serverMode = false;
    }
    std::vector<std::string> args;
    if (auto status = cmdBuilder_.GetBuildArgs(language, port, request, args); status.IsError()) {
        YRLOG_ERROR("{}|{}|get build args failed, can not start instanceID({}), runtimeID({})", info.traceid(),
                    info.requestid(), info.instanceid(), runtimeID);
        auto response = GenFailStartInstanceResponse(request, status.StatusCode(), status.GetMessage());
        response.mutable_startruntimeinstanceresponse()->set_executortype(
            static_cast<int32_t>(EXECUTOR_TYPE::SUPERVISOR));

        return response;
    }

    if (language.find(PYTHON_LANGUAGE) != std::string::npos) {
        auto execPath = cmdBuilder_.GetExecPathFromRuntimeConfig(info.runtimeconfig());
        std::string pythonServerPath = "/python/yr/main/yr_runtime_main.py";
        args.insert(args.begin(), { execPath, "-u", config_.runtimePath + pythonServerPath });
    }

    runtime2portMappings_[info.runtimeid()] = port;

    YRLOG_INFO("begin to start sandbox for runtime({}) instance({})", runtimeID, instanceID);
    return StartRuntime(request, language, GenerateEnvs(config_, request, port, cardIDs, features), args);
}

inline std::string GetPythonExecPath(const google::protobuf::Map<std::string, std::string> &options,
                                     const messages::RuntimeInstanceInfo &info, CommandBuilder &cmdBuilder)
{
    // 插件返回的execPath不局限于python，后续可以统一处理execPath，不需要再分语言了
    auto execPathIter = options.find(EXEC_PATH);
    if (execPathIter != options.end()) {
        YRLOG_INFO("{}|{}|python execPath: {}", info.traceid(), info.requestid(), execPathIter->second);
        return execPathIter->second;
    }

    return cmdBuilder.GetExecPathFromRuntimeConfig(info.runtimeconfig());
}

litebus::Future<messages::StartInstanceResponse> SupervisorExecutor::StartRuntime(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &language, const Envs &envs,
    const std::vector<std::string> &args)
{
    const auto &info = request->runtimeinstanceinfo();
    std::string execPath;
    if (!litebus::strings::StartsWithPrefix(language, PYTHON_LANGUAGE)) {
        execPath = cmdBuilder_.GetExecPathFromRuntimeConfig(info.runtimeconfig());
    } else {
        const auto &options = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
        execPath = GetPythonExecPath(options, info, cmdBuilder_);
        YRLOG_INFO("{}|{}|python: {} use execPath: {}", info.traceid(), info.requestid(), language, execPath);
    }

    return StartByRuntimeID(request, { { PARAM_EXEC_PATH, execPath }, { PARAM_LANGUAGE, language } }, args, envs)
        .Then(litebus::Defer(GetAID(), &SupervisorExecutor::OnStartRuntime, std::placeholders::_1, request));
}

litebus::Future<std::string> SupervisorExecutor::CreateSandbox(const std::string &runtimeID)
{
    nlohmann::json createRequest = nlohmann::json::object();
    return SendRequestToSupervisor("POST", SUPERVISOR_SANDBOX_PREFIX, createRequest)
        .Then([this, runtimeID](litebus::Try<nlohmann::json> createResponse) -> litebus::Future<std::string> {
            if (!createResponse.IsOK()) {
                YRLOG_ERROR("{}|Create sandbox failed: {}", runtimeID, static_cast<int>(createResponse.GetErrorCode()));
                litebus::Promise<std::string> promise;
                promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                return promise.GetFuture();
            }

            auto createResp = createResponse.Get();
            if (!createResp.contains("id") || !createResp["id"].is_string()) {
                YRLOG_ERROR("{}|Create sandbox failed: response not contains id", runtimeID);
                litebus::Promise<std::string> promise;
                promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                return promise.GetFuture();
            }

            std::string sandboxId = createResp["id"];
            YRLOG_INFO("{}|Create sandbox success: {}", runtimeID, sandboxId);
            runtime2sandboxID_.emplace(runtimeID, sandboxId);
            return sandboxId;
        });
}

litebus::Future<runtime::v1::StartResponse> SupervisorExecutor::ExecInSandbox(
    const std::string &runtimeID, const std::shared_ptr<runtime::v1::StartRequest> &start, const std::string &sandboxId)
{
    nlohmann::json execRequest = nlohmann::json::object();

    auto command = nlohmann::json::array();
    for (const auto &cmd : start->funcruntime().command()) {
        command.push_back(cmd);
    }
    execRequest["command"] = command;

    auto envs = nlohmann::json::object();
    for (const auto &env : start->funcruntime().runtimeenvs()) {
        envs[env.first] = env.second;
    }
    for (const auto &env : start->userenvs()) {
        envs[env.first] = env.second;
    }
    if (!envs.empty()) {
        execRequest["env"] = envs;
    }

    std::string execPath = SUPERVISOR_SANDBOX_PREFIX + "/" + sandboxId + "/exec_background";
    YRLOG_INFO("{}|Executing command: {} in sandbox: {}", runtimeID, execRequest.dump(), sandboxId);

    return SendRequestToSupervisor("POST", execPath, execRequest)
        .Then([this, sandboxId,
               runtimeID](litebus::Try<nlohmann::json> execResponse) -> litebus::Future<runtime::v1::StartResponse> {
            runtime::v1::StartResponse rsp{};
            if (!execResponse.IsOK()) {
                YRLOG_ERROR("{}|Failed to exec command in sandbox {}: {}", runtimeID, sandboxId,
                            static_cast<int>(execResponse.GetErrorCode()));
                auto deleteReq = std::make_shared<runtime::v1::DeleteRequest>();
                deleteReq->set_id(sandboxId);
                DoDeleteSandbox(deleteReq).OnComplete(
                    [runtimeID, sandboxId](const litebus::Future<runtime::v1::DeleteResponse> &deleteFuture) {
                        if (deleteFuture.IsError()) {
                            YRLOG_WARN("{}|Failed to cleanup sandbox {} after exec failure", runtimeID, sandboxId);
                        }
                    });
                rsp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                rsp.set_message("Failed to execute command in sandbox");
                return rsp;
            }

            rsp.set_code(0);
            rsp.set_message("success");
            rsp.set_id(sandboxId);
            return rsp;
        });
}

litebus::Future<messages::StartInstanceResponse> SupervisorExecutor::OnStartInstanceCompleted(
    const std::string &runtimeID, const messages::StartInstanceResponse &response)
{
    (void)inProgressStarts_.erase(runtimeID);
    if (pendingDeletes_.erase(runtimeID) > 0) {
        YRLOG_INFO("runtime({}) finish start, but has pending delete, start cleaning up", runtimeID);
        if (response.code() == static_cast<int32_t>(StatusCode::SUCCESS)) {
            auto stopReq = std::make_shared<messages::StopInstanceRequest>();
            stopReq->set_runtimeid(runtimeID);
            stopReq->set_requestid("cleanup-" + runtimeID);
            litebus::Async(GetAID(), &SupervisorExecutor::StopInstance, stopReq, false);
        }
    }
    return response;
}

litebus::Future<Status> SupervisorExecutor::StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                                         bool oomKilled)
{
    auto runtimeID = request->runtimeid();
    YRLOG_INFO("{}|begin to stop sandbox for runtime({})", request->requestid(), runtimeID);

    // Get sandbox ID
    auto sandboxIDIter = runtime2sandboxID_.find(runtimeID);
    if (sandboxIDIter == runtime2sandboxID_.end()) {
        YRLOG_ERROR("sandbox ID not found for runtime({})", runtimeID);
        return Status::OK();
    }
    std::string sandboxID = sandboxIDIter->second;
    // Create delete request
    auto deleteReq = std::make_shared<runtime::v1::DeleteRequest>();
    deleteReq->set_id(sandboxID);
    // Delete sandbox
    return DoDeleteSandbox(deleteReq).Then(
        [this, runtimeID](const litebus::Future<runtime::v1::DeleteResponse> &future) -> litebus::Future<Status> {
            if (future.IsError()) {
                YRLOG_ERROR("failed to delete sandbox for runtime({})", runtimeID);
                return Status::OK();
            }

            // Remove from maps
            runtime2sandboxID_.erase(runtimeID);
            runtimeInstanceInfoMap_.erase(runtimeID);

            YRLOG_INFO("successfully stopped sandbox for runtime({})", runtimeID);
            return Status::OK();
        });
}

litebus::Future<runtime::v1::DeleteResponse> SupervisorExecutor::DoDeleteSandbox(
    const std::shared_ptr<runtime::v1::DeleteRequest> &req)
{
    std::string path = SUPERVISOR_SANDBOX_PREFIX + "/" + req->id();
    return SendRequestToSupervisor("DELETE", path, nlohmann::json::object())
        .Then([](litebus::Try<nlohmann::json>) -> litebus::Future<runtime::v1::DeleteResponse> {
            return runtime::v1::DeleteResponse{};
        });
}

litebus::Future<messages::SnapshotRuntimeResponse> SupervisorExecutor::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(request->requestid());
    response.set_code(static_cast<int32_t>(StatusCode::GRPC_UNIMPLEMENTED));
    response.set_message("Snapshot is not supported for process-based runtime");
    YRLOG_WARN("{}|SnapshotRuntime is not supported for RuntimeExecutor", request->requestid());
    return response;
}

std::map<std::string, messages::RuntimeInstanceInfo> SupervisorExecutor::GetRuntimeInstanceInfos()
{
    return runtimeInstanceInfoMap_;
}

void SupervisorExecutor::ConfigRuntimeRedirectLog(std::string &stdOut, std::string &stdErr,
                                                  const std::string &runtimeID)
{
    auto path = litebus::os::Join(config_.runtimeLogPath, config_.runtimeStdLogDir);
    if (!litebus::os::ExistPath(path)) {
        YRLOG_WARN("{}|std log path {} not found, try to make dir", runtimeID, path);
        if (!litebus::os::Mkdir(path).IsNone()) {
            YRLOG_WARN("{}|failed to make dir {}, msg: {}", runtimeID, path, litebus::os::Strerror(errno));
            return;
        }
    }

    stdOut = litebus::os::Join(path, fmt::format("{}.out", runtimeID));
    if (!litebus::os::ExistPath(stdOut) && TouchFile(stdOut) != 0) {
        YRLOG_WARN("create std out log file {} failed: {}", stdOut, litebus::os::Strerror(errno));
        return;
    }

    stdErr = litebus::os::Join(path, fmt::format("{}.err", runtimeID));
    if (!litebus::os::ExistPath(stdErr) && TouchFile(stdErr) != 0) {
        YRLOG_WARN("create std err log file {} failed: {}", stdErr, litebus::os::Strerror(errno));
    }
}

void SupervisorExecutor::BuildRuntimeCommands(runtime::v1::FunctionRuntime *funcRt,
                                              const std::vector<std::string> &buildArgs)
{
    // Build commands for runtime
    for (const auto &arg : buildArgs) {
        funcRt->add_command(arg);
    }
}

void SupervisorExecutor::SetRequestEnvsAndLogsForStart(runtime::v1::StartRequest *req, const Envs &envs,
                                                       const std::string &runtimeID)
{
    const std::map<std::string, std::string> combineEnvs = cmdBuilder_.CombineEnvs(envs);
    req->mutable_userenvs()->insert(combineEnvs.begin(), combineEnvs.end());
    (*req->mutable_userenvs())[YR_ONLY_STDOUT] = "true";

    std::string stdOut;
    std::string stdErr;
    ConfigRuntimeRedirectLog(stdOut, stdErr, runtimeID);
    req->set_stdout(stdOut);
    req->set_stderr(stdErr);
}

Envs BuildMountForCodes(const std::shared_ptr<runtime::v1::StartRequest> &start,
                        const std::shared_ptr<messages::StartInstanceRequest> &request, const Envs &envs)
{
    Envs updateEnv = envs;
    auto workingDirIter = envs.posixEnvs.find(UNZIPPED_WORKING_DIR);
    if (workingDirIter == envs.posixEnvs.end() || workingDirIter->second.empty()) {
        return updateEnv;
    }
    auto deploySpec = request->runtimeinstanceinfo().deploymentconfig();
    auto layerPath = litebus::os::Join(deploySpec.deploydir(), RUNTIME_LAYER_DIR_NAME);
    auto funcPath = litebus::os::Join(layerPath, RUNTIME_FUNC_DIR_NAME);
    auto code = start->add_mounts();
    code->set_type("bind");

    auto libPathIter = envs.posixEnvs.find("YR_FUNCTION_LIB_PATH");
    if (libPathIter != envs.posixEnvs.end() && !libPathIter->second.empty()) {
        funcPath = libPathIter->second;
    }
    code->set_host_path(workingDirIter->second);
    std::string funcPathTarget = funcPath;
    std::replace(funcPathTarget.begin(), funcPathTarget.end(), '/', '-');
    code->set_target(request->runtimeinstanceinfo().container().mountpoint());

    updateEnv.posixEnvs[UNZIPPED_WORKING_DIR] = code->target();
    updateEnv.posixEnvs["YR_FUNCTION_LIB_PATH"] = code->target();
    updateEnv.posixEnvs["FUNCTION_LIB_PATH"] = code->target();

    for (auto &layer : GenerateLayerPath(request->runtimeinstanceinfo())) {
        auto code = start->add_mounts();
        code->set_type("bind");
        code->set_host_path(layer);
        std::string target = layer;
        std::replace(target.begin(), target.end(), '/', '-');
        code->set_target(litebus::os::Join("/opt", target));
    }
    return updateEnv;
}

litebus::Future<runtime::v1::StartResponse> SupervisorExecutor::StartByRuntimeID(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
    const Envs &envs)
{
    const auto &execPath = startRuntimeParams.at(PARAM_EXEC_PATH);
    auto language = startRuntimeParams.at(PARAM_LANGUAGE);
    const auto &runtimeID = request->runtimeinstanceinfo().runtimeid();

    std::string cmd = execPath;
    runtime::v1::StartResponse rsp{};
    // java has jvm args check so ignore here
    if (language.find(JAVA_LANGUAGE_PREFIX) == std::string::npos && !CheckIllegalChars(cmd)) {
        rsp.set_code(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
        rsp.set_message(fmt::format("invalid cmd: {}", cmd));
        return rsp;
    }
    YRLOG_INFO("start {} runtime({}), execute final cmd: {}", language, runtimeID, cmd);
    auto start = std::make_shared<runtime::v1::StartRequest>();

    BuildRuntimeCommands(start->mutable_funcruntime(), buildArgs);

    auto updateEnv = BuildMountForCodes(start, request, envs);
    SetRequestEnvsAndLogsForStart(start.get(), updateEnv, runtimeID);

    return CreateSandbox(runtimeID).Then(
        [this, start, runtimeID](const std::string &sandboxId) { return ExecInSandbox(runtimeID, start, sandboxId); });
}

litebus::Future<Status> SupervisorExecutor::TerminateSandbox(const std::string &runtimeID, const std::string &sandboxID)
{
    // Terminate sandbox
    YRLOG_INFO("terminate sandbox({}) for runtime({})", sandboxID, runtimeID);

    auto deleteReq = std::make_shared<runtime::v1::DeleteRequest>();
    deleteReq->set_id(sandboxID);

    return DoDeleteSandbox(deleteReq).Then([]() -> litebus::Future<Status> { return Status::OK(); });
}

messages::StartInstanceResponse SupervisorExecutor::GenSuccessStartInstanceResponse(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &sandboxID)
{
    messages::StartInstanceResponse response;
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("start instance success");
    auto info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();

    response.set_requestid(info.requestid());
    response.mutable_startruntimeinstanceresponse()->set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::SUPERVISOR));

    auto instanceResponse = response.mutable_startruntimeinstanceresponse();
    instanceResponse->set_runtimeid(runtimeID);
    instanceResponse->set_containerid(sandboxID);
    YRLOG_DEBUG("{}|{}|instance({}) runtime({}) with container({})", info.traceid(), info.requestid(),
                info.instanceid(), runtimeID, sandboxID);

    // set to be zero
    instanceResponse->set_pid(0);
    auto portMappingsIter = runtime2portMappings_.find(runtimeID);
    if (portMappingsIter != runtime2portMappings_.end()) {
        instanceResponse->set_port(portMappingsIter->second);
    }
    return response;
}

litebus::Future<messages::StartInstanceResponse> SupervisorExecutor::OnStartRuntime(
    const runtime::v1::StartResponse &response, const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &info = request->runtimeinstanceinfo();

    // On start runtime
    auto runtimeID = info.runtimeid();
    YRLOG_INFO("on start runtime({}) with sandbox({})", runtimeID, response.id());

    auto startInstanceResponse = GenSuccessStartInstanceResponse(request, response.id());
    return litebus::Async(GetAID(), &SupervisorExecutor::OnStartInstanceCompleted, runtimeID, startInstanceResponse);
}

litebus::Future<messages::UpdateCredResponse> SupervisorExecutor::UpdateCredForRuntime(
    const std::shared_ptr<messages::UpdateCredRequest> &request)
{
    // Update credentials for runtime
    auto runtimeID = request->runtimeid();
    YRLOG_INFO("update credentials for runtime({})", runtimeID);

    messages::UpdateCredResponse response;
    response.set_requestid(request->requestid());
    response.set_code(0);
    response.set_message("update credentials success");

    return response;
}

litebus::Future<Status> SupervisorExecutor::NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                                const int limit)
{
    // Notify instances disk usage exceed limit
    YRLOG_INFO("notify instances disk usage exceed limit: {}", description);

    return Status::OK();
}

bool SupervisorExecutor::IsRuntimeActive(const std::string &runtimeID)
{
    // Check if runtime is active
    auto it = runtime2sandboxID_.find(runtimeID);
    return it != runtime2sandboxID_.end();
}

litebus::Future<bool> SupervisorExecutor::StopAllSandboxes()
{
    std::list<litebus::Future<Status>> futures;
    YRLOG_INFO("{} containers need to stop", runtime2sandboxID_.size());
    for (auto [runtimeID, containerID] : runtime2sandboxID_) {
        futures.emplace_back(TerminateSandbox(runtimeID, containerID));
        YRLOG_INFO("stop runtime {} with container {}", runtimeID, containerID);
    }
    return CollectStatus(futures, "").Then([]() -> litebus::Future<bool> { return true; });
}

litebus::Future<::messages::StartInstanceResponse> SupervisorExecutorProxy::StartInstance(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs)
{
    return litebus::Async(executor_->GetAID(), &SupervisorExecutor::StartInstance, request, cardIDs);
}

litebus::Future<Status> SupervisorExecutorProxy::StopInstance(
    const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled)
{
    return litebus::Async(executor_->GetAID(), &SupervisorExecutor::StopInstance, request, oomKilled);
}

litebus::Future<messages::SnapshotRuntimeResponse> SupervisorExecutorProxy::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    return litebus::Async(executor_->GetAID(), &SupervisorExecutor::SnapshotRuntime, request);
}

litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> SupervisorExecutorProxy::GetRuntimeInstanceInfos()
{
    return litebus::Async(executor_->GetAID(), &SupervisorExecutor::GetRuntimeInstanceInfos);
}

}  // namespace functionsystem::runtime_manager
