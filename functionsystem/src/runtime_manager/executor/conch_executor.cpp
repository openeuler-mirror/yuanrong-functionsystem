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

#include "conch_executor.h"

#include <sys/un.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <utility>

#include "async/asyncafter.hpp"
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/metadata/metadata_type.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/collect_status.h"
#include "common/utils/exec_utils.h"
#include "common/utils/files.h"
#include "common/utils/generate_message.h"
#include "common/utils/time_utils.h"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "nlohmann/json.hpp"
#include "runtime_manager/utils/utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem::runtime_manager {
constexpr int64_t RECONNECT_SUPERVISOR_INTERVAL_MS = 5000;
constexpr int64_t HEALTH_CHECK_INTERVAL_MS = 100;
constexpr int64_t HTTP_TIMEOUT_MS = 30000;
// conch reuses jiuwenbox's sandbox API paths and socket; the prefix name is
// independent only for readability. Values match SUPERVISOR_* in supervisor_executor.
const std::string CONCH_SANDBOX_PREFIX = "/api/v1/sandboxes";
const std::string CONCH_UDS_SOCKET = "/run/jiuwenbox/jiuwenbox.sock";
constexpr size_t HTTP_HEADER_SEPARATOR_LEN = 4;   // length of "\r\n\r\n"
constexpr size_t CONTENT_LENGTH_PREFIX_LEN = 15;   // length of "content-length:"
// HTTP status code boundaries: 0 means "not parsed", 2xx range is success.
constexpr int HTTP_STATUS_UNPARSED = 0;
constexpr int HTTP_STATUS_OK_MIN = 200;
constexpr int HTTP_STATUS_OK_MAX = 300;   // exclusive upper bound of 2xx
constexpr double CPU_MILLICORES_PER_CORE = 1000.0;   // CPU resource is in milli-cores (1000 = 1 core)

ConchExecutor::ConchExecutor(const std::string &name, const litebus::AID &functionAgentAID)
    : Executor(name), functionAgentAID_(functionAgentAID)
{
}

void ConchExecutor::Init()
{
    pkgType_ = GetInstallationType();
    YRLOG_INFO("Start init ConchExecutor");
}

void ConchExecutor::Finalize()
{
    YRLOG_INFO("Start finalize ConchExecutor");
    runtime2portMappings_.clear();
    runtime2sandboxIP_.clear();
    Executor::Finalize();
}

void ConchExecutor::InitConfig()
{
    cmdBuilder_.SetRuntimeConfig(config_);
}

void ConchExecutor::ParseResponse(litebus::Promise<nlohmann::json> promise, std::string response)
{
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        YRLOG_ERROR("invalid HTTP response (no header/body separator)");
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return;
    }
    std::string respBody = response.substr(headerEnd + HTTP_HEADER_SEPARATOR_LEN);

    // Parse HTTP status code from the first line (e.g. "HTTP/1.1 200 OK"); non-2xx is a failure.
    // Note: jiuwenbox's conch backend returns 201 + phase=error + error_message on soft failures
    // (e.g. conchd unreachable, SDK not installed). 201 is 2xx, so it passes this status gate and
    // is then handled below in CreateSandbox via the error_message field — do not treat 201 as a
    // transport failure here.
    int httpStatus = HTTP_STATUS_UNPARSED;
    size_t firstLineEnd = response.find("\r\n");
    if (firstLineEnd != std::string::npos && firstLineEnd < headerEnd) {
        std::string statusLine = response.substr(0, firstLineEnd);
        size_t sp1 = statusLine.find(' ');
        if (sp1 != std::string::npos) {
            size_t sp2 = statusLine.find(' ', sp1 + 1);
            std::string codeStr = statusLine.substr(sp1 + 1,
                sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
            try {
                httpStatus = std::stoi(codeStr);
            } catch (std::exception const &e) {
                YRLOG_WARN("failed to parse HTTP status code from '{}': {}", statusLine, e.what());
            }
        }
    }

    if (httpStatus != HTTP_STATUS_UNPARSED &&
        (httpStatus < HTTP_STATUS_OK_MIN || httpStatus >= HTTP_STATUS_OK_MAX)) {
        YRLOG_ERROR("conch returned non-2xx status: {}, body: {}", httpStatus, respBody);
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return;
    }
    if (respBody.empty()) {
        YRLOG_ERROR("HTTP response body is empty");
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return;
    }
    try {
        YRLOG_INFO("UDS request success, response: {}", respBody);
        auto jsonResp = nlohmann::json::parse(respBody);
        promise.SetValue(jsonResp);
    } catch (std::exception const &e) {
        YRLOG_ERROR("failed to parse response: {}", e.what());
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
    }
}

litebus::Future<nlohmann::json> ConchExecutor::SendRequestToConch(const std::string &method,
    const std::string &path, const nlohmann::json &body)
{
    litebus::Promise<nlohmann::json> promise;
    litebus::Future<nlohmann::json> result = promise.GetFuture();
    int fd = ConnectUdsSocket(CONCH_UDS_SOCKET);
    if (fd < 0) {
        YRLOG_ERROR("failed to connect to UDS socket: {}", CONCH_UDS_SOCKET);
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return result;
    }
    std::string httpRequest = BuildUdsHttpRequest(method, path, body.dump());
    if (!SendUdsRequest(fd, httpRequest)) {
        (void)close(fd);
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return result;
    }
    std::string response;
    if (!ReceiveUdsResponse(fd, response)) {
        (void)close(fd);
        promise.SetFailed(static_cast<int32_t>(ERR_INNER_COMMUNICATION));
        return result;
    }
    (void)close(fd);
    ParseResponse(promise, response);
    return result;
}

bool ConchExecutor::SendUdsRequest(int fd, const std::string &httpRequest)
{
    ssize_t sent = send(fd, httpRequest.c_str(), httpRequest.length(), 0);
    if (sent < 0 || static_cast<size_t>(sent) != httpRequest.length()) {
        YRLOG_ERROR("failed to send request to UDS socket: {}", std::strerror(errno));
        return false;
    }
    return true;
}

bool ConchExecutor::ReceiveUdsResponse(int fd, std::string &response)
{
    char buf[4096];
    size_t headerEnd = std::string::npos;
    size_t contentLength = 0;
    bool hasContentLength = false;
    ssize_t received;
    while ((received = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[received] = '\0';
        response += buf;
        if (headerEnd == std::string::npos) {
            headerEnd = response.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                continue;
            }
            std::string headers = response.substr(0, headerEnd);
            std::string lowerHeaders = headers;
            std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            size_t contentLengthPos = lowerHeaders.find("content-length:");
            if (contentLengthPos != std::string::npos) {
                // npos guard: malformed headers without trailing CRLF would underflow the subtraction.
                size_t crlfPos = headers.find("\r\n", contentLengthPos);
                size_t valueStart = contentLengthPos + CONTENT_LENGTH_PREFIX_LEN;
                size_t valueLen = (crlfPos == std::string::npos)
                                      ? (headerEnd > valueStart ? headerEnd - valueStart : 0)
                                      : crlfPos - valueStart;
                hasContentLength = true;
                contentLength = std::stoul(headers.substr(valueStart, valueLen));
            }
        }
        if (hasContentLength && response.length() - (headerEnd + HTTP_HEADER_SEPARATOR_LEN) >= contentLength) {
            break;
        }
    }
    if (received < 0) {
        YRLOG_ERROR("failed to receive response from UDS socket: {}", std::strerror(errno));
        return false;
    }
    return true;
}

int ConchExecutor::ConnectUdsSocket(const std::string &socketPath)
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

std::string ConchExecutor::BuildUdsHttpRequest(const std::string &method, const std::string &path,
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

litebus::Future<messages::StartInstanceResponse> ConchExecutor::StartInstance(
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
            static_cast<int32_t>(EXECUTOR_TYPE::CONCH));

        return response;
    }

    if (language.find(PYTHON_LANGUAGE) != std::string::npos) {
        auto execPath = cmdBuilder_.GetExecPathFromRuntimeConfig(info.runtimeconfig());
        std::string pythonServerPath = PYTHON_SERVER_PATH;
        if (pkgType_ == PKG_TYPE_WHEEL) {
            pythonServerPath = PYTHON_SERVER_PATH_IN_WHEEL;
        }
        args.insert(args.begin(), { execPath, "-u", config_.runtimePath + pythonServerPath });
    }

    // Conch sandboxes run on host network with no published host ports, so each user port
    // forward is a direct 1:1 mapping. Emit the JSON array form (matching docker/supervisor
    // executor) so the deploy response carries portmappings downstream as the portForward
    // instance extension.
    const auto &deployOpts = info.deploymentconfig().deployoptions();
    auto networkIter = deployOpts.find(CONTAINER_NETWORK);
    if (networkIter != deployOpts.end() && !networkIter->second.empty()) {
        auto forwardConfigs = ParseForwardPorts(networkIter->second);
        nlohmann::json portJson = nlohmann::json::array();
        for (const auto &fc : forwardConfigs) {
            portJson.push_back(fc.protocol + ":" + std::to_string(fc.containerPort) + ":" +
                               std::to_string(fc.containerPort));
        }
        if (!portJson.empty()) {
            runtime2portMappings_[info.runtimeid()] = portJson.dump();
        }
    }

    YRLOG_INFO("begin to start sandbox for runtime({}) instance({})", runtimeID, instanceID);
    return StartRuntime(request, language, GenerateEnvs(config_, request, port, cardIDs, features), args);
}

// TU-local helpers (internal linkage) mirroring supervisor_executor; kept static to avoid
// a multiple-definition link error since the same free functions exist in supervisor_executor.cpp.
static std::string GetPythonExecPath(const google::protobuf::Map<std::string, std::string> &options,
    const messages::RuntimeInstanceInfo &info, CommandBuilder &cmdBuilder)
{
    auto execPathIter = options.find(EXEC_PATH);
    if (execPathIter != options.end()) {
        return execPathIter->second;
    }

    return cmdBuilder.GetExecPathFromRuntimeConfig(info.runtimeconfig());
}

litebus::Future<messages::StartInstanceResponse> ConchExecutor::StartRuntime(
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
    }

    litebus::Promise<messages::StartInstanceResponse> promise;
    StartByRuntimeID(request, { { PARAM_EXEC_PATH, execPath }, { PARAM_LANGUAGE, language } }, args, envs)
        .OnComplete([this, request, promise, info](const litebus::Future<runtime::v1::StartResponse> &future) mutable {
            if (future.IsError()) {
                YRLOG_ERROR("{}|{}|start runtime failed in conch, error code: {}", info.traceid(),
                            info.requestid(), future.GetErrorCode());
                promise.SetFailed(future.GetErrorCode());
                return;
            }
            const auto &response = future.Get();
            if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
                YRLOG_ERROR("{}|{}|failed to start runtime in conch, code({}) message({})", info.traceid(),
                            info.requestid(), response.code(), response.message());
                auto startResponse =
                    GenFailStartInstanceResponse(request, RUNTIME_MANAGER_CREATE_EXEC_FAILED, response.message());
                startResponse.mutable_startruntimeinstanceresponse()->set_executortype(
                    static_cast<int32_t>(EXECUTOR_TYPE::CONCH));
                promise.SetValue(startResponse);
                return;
            }

            auto runtimeID = info.runtimeid();
            std::string sandboxIP;
            if (auto node = runtime2sandboxIP_.extract(runtimeID); !node.empty()) {
                sandboxIP = std::move(node.mapped());
            }
            auto startInstanceResponse = GenSuccessStartInstanceResponse(request, response.id(), sandboxIP);
            litebus::Async(GetAID(), &ConchExecutor::OnStartInstanceCompleted, runtimeID, startInstanceResponse)
                .OnComplete([promise](const litebus::Future<messages::StartInstanceResponse> &innerFuture) mutable {
                    if (innerFuture.IsError()) {
                        promise.SetFailed(innerFuture.GetErrorCode());
                        return;
                    }
                    promise.SetValue(innerFuture.Get());
                });
        });
    return promise.GetFuture();
}
bool ConchExecutor::IsReadonlyMount(const nlohmann::json &mount)
{
    const auto it = mount.find("readonly");
    if (it == mount.end() || (!it->is_boolean() && !it->is_string())) {
        return false;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    const auto &s = it->get_ref<const std::string &>();
    return s == "true" || s == "1";
}

nlohmann::json ConchExecutor::ParseBindMounts(const std::string &rootfsJson, const std::string &runtimeID)
{
    nlohmann::json bindMounts = nlohmann::json::array();
    BindHostLogDir(runtimeID, bindMounts);
    if (rootfsJson.empty()) {
        return bindMounts;
    }
    try {
        const auto rootfs = nlohmann::json::parse(rootfsJson);
        if (rootfs.contains("mounts") && rootfs["mounts"].is_array()) {
            for (const auto &m : rootfs["mounts"]) {
                if (!m.is_object() || !m.contains("source") || !m.contains("target")) {
                    continue;
                }
                bindMounts.push_back({
                    {"host_path", m["source"].get<std::string>()},
                    {"sandbox_path", m["target"].get<std::string>()},
                    {"mode", IsReadonlyMount(m) ? "ro" : "rw"},
                });
            }
        }
    } catch (const std::exception &e) {
        YRLOG_WARN("{}|Failed to parse rootfs mounts: {}, skip mounts", runtimeID, e.what());
    }
    return bindMounts;
}

void ConchExecutor::BindHostLogDir(const std::string &runtimeID, nlohmann::json &bindMounts)
{
    const std::string hostLogDir = litebus::os::Join(config_.runtimeLogPath, config_.runtimeStdLogDir);
    if (!hostLogDir.empty() && IsSafeBindSource(hostLogDir) && litebus::os::ExistPath(hostLogDir)) {
        bindMounts.push_back({
            { "host_path", hostLogDir },
            { "sandbox_path", hostLogDir },
            { "mode", "rw" },
        });
        return;
    }

    YRLOG_WARN("{}|skip log dir mount (path={}, safe={}, exists={}); yr logs may stay in-sandbox", runtimeID,
               hostLogDir, IsSafeBindSource(hostLogDir), litebus::os::ExistPath(hostLogDir));
}

// Build the jiuwenbox create-sandbox request body for the conch backend.
// Differences from supervisor_executor::CreateRequest (conch policy hard constraints):
//   1. Top-level "sandbox_runtime":"conch" routes jiuwenbox to ConchRuntime.
//   2. bind_mounts live under policy.conch.filesystem_policy.bind_mounts (NOT
//      top-level policy.filesystem_policy). ConchFilesystemPolicy is
//      extra="forbid" with only bind_mounts; emitting top-level filesystem_policy
//      would be silently ignored by the conch path and is omitted for clarity.
//   3. policy.conch.template_id carries the EROFS template id, sourced from the
//      rootfs JSON imageurl (ParseRootfsImageUrl). Empty is allowed: conchd falls
//      back to JIUWENBOX_CONCH_TEMPLATE_ID / sandbox.default_template_id.
//   4. REMOVED vs supervisor: policy.environment (bwrap-only), policy.process,
//      policy.namespace, policy.cgroup, and top-level filesystem_policy
//      directories/read_write. ConchPolicy is extra="forbid" — only the 7 keys
//      template_id/vcpu_num/vcpu_max/ram_mb/env/filesystem_policy/network are
//      accepted; any of the supervisor keys here would yield a 400.
//      Guest env (JIUWENSWARM_HOME + posixenvs) goes to policy.conch.env, which
//      merge_conch_create_env merges with the create-API env override.
nlohmann::json ConchExecutor::CreateRequest(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();
    const auto &deployOpts = info.deploymentconfig().deployoptions();
    auto getOpt = [&](const std::string &key) {
        auto it = deployOpts.find(key);
        return it != deployOpts.end() ? it->second : std::string{};
    };
    const std::string hostUser = getOpt(HOST_USER).empty() ? "agentos" : getOpt(HOST_USER);
    const std::string rootfsJson = getOpt(CONTAINER_ROOTFS);
    const std::string homeDir = "/home/" + hostUser;

    nlohmann::json conch = nlohmann::json::object();

    // template_id from rootfs imageurl; empty -> conchd default_template_id fallback.
    conch["template_id"] = ParseRootfsImageUrl(rootfsJson);

    // bind_mounts under conch.filesystem_policy.bind_mounts. BindMount element shape
    // {host_path, sandbox_path, mode} is shared with the process backend. Note
    // (validate_conch_policy): each host_path must exist and be a directory
    // on the jiuwenbox host, else 400 — stricter than supervisor's bwrap.
    if (auto bindMounts = ParseBindMounts(rootfsJson, runtimeID); !bindMounts.empty()) {
        conch["filesystem_policy"] = { { "bind_mounts", std::move(bindMounts) } };
    }

    // Guest env lives in policy.conch.env (NOT top-level environment, which is bwrap-only).
    conch["env"] = nlohmann::json::object({ { "JIUWENSWARM_HOME", homeDir } });
    for (const auto &kv : info.runtimeconfig().posixenvs()) {
        conch["env"][kv.first] = kv.second;
    }

    nlohmann::json policy = nlohmann::json::object();
    policy["conch"] = std::move(conch);

    YRLOG_INFO("{}|Create conch sandbox for {}", runtimeID, hostUser);

    return nlohmann::json{
        { "sandbox_runtime", "conch" },
        { "policy", std::move(policy) },
        { "policy_mode", "append" },
    };
}

litebus::Future<runtime::v1::StartResponse> ConchExecutor::CreateSandbox(
    const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &runtimeID = request->runtimeinstanceinfo().runtimeid();
    litebus::Promise<runtime::v1::StartResponse> promise;
    nlohmann::json createRequest = CreateRequest(request);

    SendRequestToConch("POST", CONCH_SANDBOX_PREFIX, createRequest)
        .OnComplete([this, runtimeID, promise](const litebus::Future<nlohmann::json> &future) mutable {
            // Resolve a failure StartResponse (carrying the conch error_message when
            // present) so the caller can read code/message directly; CreateSandbox never uses
            // SetFailed, mirroring ExecInSandbox. Isolated orphan sandboxes are cleaned up first.
            auto failWith = [&](const std::string &message) {
                runtime::v1::StartResponse failRsp{};
                failRsp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                failRsp.set_message(message);
                promise.SetValue(failRsp);
            };

            if (future.IsError()) {
                YRLOG_ERROR("{}|Create sandbox request failed with error code: {}", runtimeID, future.GetErrorCode());
                failWith("Failed to create sandbox");   // transport failure: no error_message
                return;
            }

            const nlohmann::json &createResp = future.Get();

            // error_message set means create failed; if a valid id is also returned (orphan sandbox),
            // delete it first then fail. Covers the conch 201+phase=error soft-failure path (SDK not installed / conchd
            // unreachable / template missing): jiuwenbox returns 201 with error_message set,
            // which ParseResponse admits as 2xx, so we reach here and treat it as a failure.
            if (createResp.contains("error_message") && !createResp["error_message"].is_null()) {
                std::string errorMsg = createResp["error_message"].get<std::string>();
                YRLOG_ERROR("{}|Create sandbox failed with error_message: {}", runtimeID, errorMsg);
                failWith("Failed to create sandbox" + (errorMsg.empty() ? "" : ": " + errorMsg));
                if (createResp.contains("id") && createResp["id"].is_string()
                    && !createResp["id"].get<std::string>().empty()) {
                    CleanupSandboxAfterFailure(runtimeID, createResp["id"].get<std::string>());
                }
                return;
            }

            // check id field
            if (!createResp.contains("id") || !createResp["id"].is_string()) {
                YRLOG_ERROR("{}|Create sandbox failed: response does not contain valid id", runtimeID);
                failWith("Create sandbox response does not contain valid id");
                return;
            }

            std::string sandboxId = createResp["id"];
            YRLOG_INFO("{}|Create sandbox success: {}", runtimeID, sandboxId);
            runtime2sandboxID_.emplace(runtimeID, sandboxId);

            // ip_address absent/non-string is non-fatal: sandbox_ip stays empty downstream.
            // (The backend response field was renamed runtime->sandbox_runtime, but the
            // executor never reads it; conch's pid is fixed null and is also never read.)
            if (createResp.contains("ip_address") && createResp["ip_address"].is_string()) {
                runtime2sandboxIP_.emplace(runtimeID, createResp["ip_address"].get<std::string>());
            } else {
                YRLOG_WARN("{}|Create sandbox response has no ip_address, sandbox IP left empty", runtimeID);
            }

            runtime::v1::StartResponse rsp{};
            rsp.set_code(0);
            rsp.set_message("success");
            rsp.set_id(sandboxId);
            promise.SetValue(rsp);
        });

    return promise.GetFuture();
}

nlohmann::json ConchExecutor::BuildCommand(const std::shared_ptr<runtime::v1::StartRequest> &start)
{
    auto command = nlohmann::json::array();

    std::string quotedArgv;
    for (int i = 0; i < start->command().size(); ++i) {
        if (i > 0) {
            quotedArgv += " ";
        }
        quotedArgv += ShellQuote(start->command()[i]);
    }

    auto stdOut = start->stdout();
    auto stdErr = start->stderr();

    // Empty paths or unwritable target: fall back to inherited stdout/stderr (aligned with DockerExecutor).
    std::string cmdLine;
    if (stdOut.empty() || stdErr.empty()) {
        cmdLine = "exec " + quotedArgv;
    } else {
        cmdLine = "if ( >" + ShellQuote(stdOut) + " ) 2>/dev/null; then exec " + quotedArgv
            + " >" + ShellQuote(stdOut) + " 2>" + ShellQuote(stdErr) + "; else exec "
            + quotedArgv + "; fi";
    }

    command.push_back("sh");
    command.push_back("-c");
    command.push_back(cmdLine);
    return command;
}

nlohmann::json ConchExecutor::BuildExecRequest(const std::string &runtimeID,
    const std::shared_ptr<runtime::v1::StartRequest> &start, const std::string &sandboxId)
{
    nlohmann::json execRequest = nlohmann::json::object();

    auto command = BuildCommand(start);
    execRequest["command"] = command;

    auto envs = nlohmann::json::object();
    for (const auto &env : start->envs()) {
        envs[env.first] = env.second;
    }
    if (!envs.empty()) {
        execRequest["env"] = envs;
    }

    YRLOG_INFO("{}|Executing command: {} in sandbox: {}", runtimeID, command.dump(), sandboxId);
    return execRequest;
}

litebus::Future<runtime::v1::StartResponse> ConchExecutor::ExecInSandbox(
    const std::string &runtimeID, const std::shared_ptr<runtime::v1::StartRequest> &start, const std::string &sandboxId)
{
    nlohmann::json execRequest = BuildExecRequest(runtimeID, start, sandboxId);
    std::string execPath = CONCH_SANDBOX_PREFIX + "/" + sandboxId + "/exec_background";

    litebus::Promise<runtime::v1::StartResponse> promise;
    SendRequestToConch("POST", execPath, execRequest)
        .OnComplete([this, sandboxId, runtimeID, promise](const litebus::Future<nlohmann::json> &future) mutable {
            auto failWith = [&](const std::string &message) {
                runtime::v1::StartResponse failRsp{};
                failRsp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                failRsp.set_message(message);
                promise.SetValue(failRsp);
                CleanupSandboxAfterFailure(runtimeID, sandboxId, true);
            };

            if (future.IsError()) {
                YRLOG_ERROR("{}|Failed to exec command in sandbox {}: {}", runtimeID, sandboxId,
                            static_cast<int>(future.GetErrorCode()));
                failWith("Failed to execute command in sandbox");   // transport failure: no error_message
                return;
            }

            // check error_message field; present and non-null indicates an error
            const nlohmann::json &execResp = future.Get();
            if (execResp.contains("error_message") && !execResp["error_message"].is_null()) {
                std::string errorMsg = execResp["error_message"].get<std::string>();
                YRLOG_ERROR("{}|Failed to exec command in sandbox {} with error_message: {}", runtimeID, sandboxId,
                            errorMsg);
                failWith("Failed to execute command in sandbox" + (errorMsg.empty() ? "" : ": " + errorMsg));
                return;
            }

            runtime::v1::StartResponse rsp{};
            rsp.set_code(0);
            rsp.set_message("success");
            rsp.set_id(sandboxId);
            promise.SetValue(rsp);
        });
    return promise.GetFuture();
}

void ConchExecutor::CleanupRuntimeMappings(const std::string &runtimeID)
{
    runtime2sandboxID_.erase(runtimeID);
    runtime2sandboxIP_.erase(runtimeID);
    runtime2portMappings_.erase(runtimeID);
}

void ConchExecutor::CleanupSandboxAfterFailure(const std::string &runtimeID, const std::string &sandboxId,
    bool cleanMappings)
{
    TerminateSandbox(runtimeID, sandboxId)
        .OnComplete([this, runtimeID, sandboxId, cleanMappings](const litebus::Future<Status> &future) mutable {
            if (future.IsError()) {
                YRLOG_WARN("{}|Failed to cleanup sandbox {} after exec failure", runtimeID, sandboxId);
            }

            if (cleanMappings) {
                CleanupRuntimeMappings(runtimeID);
            }
        });
}

litebus::Future<messages::StartInstanceResponse> ConchExecutor::OnStartInstanceCompleted(
    const std::string &runtimeID, const messages::StartInstanceResponse &response)
{
    (void)inProgressStarts_.erase(runtimeID);
    if (pendingDeletes_.erase(runtimeID) > 0) {
        YRLOG_INFO("runtime({}) finish start, but has pending delete, start cleaning up", runtimeID);
        if (response.code() == static_cast<int32_t>(StatusCode::SUCCESS)) {
            auto stopReq = std::make_shared<messages::StopInstanceRequest>();
            stopReq->set_runtimeid(runtimeID);
            stopReq->set_requestid("cleanup-" + runtimeID);
            litebus::Async(GetAID(), &ConchExecutor::StopInstance, stopReq, false);
        }
    }
    return response;
}

litebus::Future<Status> ConchExecutor::StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
    bool oomKilled)
{
    auto runtimeID = request->runtimeid();
    auto sandboxIDIter = runtime2sandboxID_.find(runtimeID);
    if (sandboxIDIter == runtime2sandboxID_.end()) {
        YRLOG_ERROR("sandbox ID not found for runtime({})", runtimeID);
        return Status::OK();
    }

    std::string sandboxID = sandboxIDIter->second;
    auto deleteReq = std::make_shared<runtime::v1::DeleteRequest>();
    deleteReq->set_id(sandboxID);
    YRLOG_INFO("{}|Delete sandbox: {} for runtime: {}", request->requestid(), sandboxID, runtimeID);
    litebus::Promise<Status> promise;
    // conch has no stop (jiuwenbox returns 409 on the stop endpoint); cold recreate is
    // delete+create. StopInstance therefore goes straight to delete, same as supervisor.
    DoDeleteSandbox(deleteReq).OnComplete([this, runtimeID, promise](
        const litebus::Future<runtime::v1::DeleteResponse> &future) mutable {
            CleanupRuntimeMappings(runtimeID);
            runtimeInstanceInfoMap_.erase(runtimeID);

            if (future.IsError()) {
                YRLOG_ERROR("Failed to delete sandbox for runtime({}), error code: {}", runtimeID,
                            future.GetErrorCode());
                promise.SetValue(Status(static_cast<StatusCode>(future.GetErrorCode()),
                                        "Failed to delete sandbox for runtime " + runtimeID));
                return;
            }

            YRLOG_INFO("Successfully delete sandbox for runtime({})", runtimeID);
            promise.SetValue(Status::OK());
        });
    return promise.GetFuture();
}

litebus::Future<runtime::v1::DeleteResponse> ConchExecutor::DoDeleteSandbox(
    const std::shared_ptr<runtime::v1::DeleteRequest> &req)
{
    std::string path = CONCH_SANDBOX_PREFIX + "/" + req->id();
    litebus::Promise<runtime::v1::DeleteResponse> promise;
    SendRequestToConch("DELETE", path)
        .OnComplete([promise](const litebus::Future<nlohmann::json> &future) mutable {
            if (future.IsError()) {
                promise.SetFailed(future.GetErrorCode());
                return;
            }
            promise.SetValue(runtime::v1::DeleteResponse{});
        });
    return promise.GetFuture();
}

litebus::Future<messages::SnapshotRuntimeResponse> ConchExecutor::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(request->requestid());
    response.set_code(static_cast<int32_t>(StatusCode::GRPC_UNIMPLEMENTED));
    response.set_message("Snapshot is not supported for conch-based runtime");
    YRLOG_WARN("{}|SnapshotRuntime is not supported for ConchExecutor", request->requestid());
    return response;
}

std::map<std::string, messages::RuntimeInstanceInfo> ConchExecutor::GetRuntimeInstanceInfos()
{
    return runtimeInstanceInfoMap_;
}

void ConchExecutor::BuildRuntimeCommands(runtime::v1::StartRequest *request,
    const std::vector<std::string> &buildArgs)
{
    // Build commands for runtime
    for (const auto &arg : buildArgs) {
        request->add_command(arg);
    }
}

void ConchExecutor::SetRequestEnvsAndLogsForStart(runtime::v1::StartRequest *req, const Envs &envs,
    const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &runtimeID = request->runtimeinstanceinfo().runtimeid();
    const std::map<std::string, std::string> combineEnvs = cmdBuilder_.CombineEnvs(envs);
    req->mutable_envs()->insert(combineEnvs.begin(), combineEnvs.end());
    (*req->mutable_envs())[YR_ONLY_STDOUT] = "true";

    std::string stdOut;
    std::string stdErr;
    ConfigRuntimeRedirectLog(stdOut, stdErr, runtimeID);  // base Executor impl (shared)
    req->set_stdout(stdOut);
    req->set_stderr(stdErr);
}

static Envs BuildMountForCodes(const std::shared_ptr<runtime::v1::StartRequest> &start,
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

litebus::Future<runtime::v1::StartResponse> ConchExecutor::StartByRuntimeID(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
    const Envs &envs)
{
    const auto &execPath = startRuntimeParams.at(PARAM_EXEC_PATH);
    auto language = startRuntimeParams.at(PARAM_LANGUAGE);
    const auto &runtimeID = request->runtimeinstanceinfo().runtimeid();

    std::string cmd = execPath;
    // java has jvm args check so ignore here
    if (language.find(JAVA_LANGUAGE_PREFIX) == std::string::npos && !CheckIllegalChars(cmd)) {
        runtime::v1::StartResponse rsp{};
        rsp.set_code(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
        rsp.set_message(fmt::format("invalid cmd: {}", cmd));
        return rsp;
    }
    YRLOG_INFO("start {} runtime({}), execute final cmd: {}", language, runtimeID, cmd);
    auto start = std::make_shared<runtime::v1::StartRequest>();

    BuildRuntimeCommands(start.get(), buildArgs);

    auto updateEnv = BuildMountForCodes(start, request, envs);
    SetRequestEnvsAndLogsForStart(start.get(), updateEnv, request);

    litebus::Promise<runtime::v1::StartResponse> promise;
    CreateSandbox(request)
        .OnComplete([this, start, runtimeID, promise](
            const litebus::Future<runtime::v1::StartResponse> &future) mutable {
            const auto &createResp = future.Get();
            if (createResp.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
                YRLOG_ERROR("{}|Failed to create sandbox: {}", runtimeID, createResp.message());
                runtime::v1::StartResponse rsp{};
                rsp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                rsp.set_message(createResp.message());
                promise.SetValue(rsp);
                return;
            }
            const std::string &sandboxId = createResp.id();
            ExecInSandbox(runtimeID, start, sandboxId)
                .OnComplete(
                [promise, runtimeID](const litebus::Future<runtime::v1::StartResponse> &execFuture) mutable {
                    const auto &execResp = execFuture.Get();
                    if (execResp.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
                        YRLOG_ERROR("{}|Failed to exec in sandbox: {}", runtimeID, execResp.message());
                        runtime::v1::StartResponse rsp{};
                        rsp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                        // ExecInSandbox always sets a non-empty message on the failure paths, so propagate it as-is.
                        rsp.set_message(execResp.message());
                        promise.SetValue(rsp);
                        return;
                    }
                    promise.SetValue(execResp);
                });
        });
    return promise.GetFuture();
}

litebus::Future<Status> ConchExecutor::TerminateSandbox(const std::string &runtimeID, const std::string &sandboxID)
{
    // Terminate sandbox
    YRLOG_INFO("terminate sandbox({}) for runtime({})", sandboxID, runtimeID);

    auto deleteReq = std::make_shared<runtime::v1::DeleteRequest>();
    deleteReq->set_id(sandboxID);

    litebus::Promise<Status> promise;
    DoDeleteSandbox(deleteReq).OnComplete(
        [promise, sandboxID, runtimeID](const litebus::Future<runtime::v1::DeleteResponse> &future) mutable {
            if (future.IsError()) {
                YRLOG_ERROR("Failed to terminate sandbox({}) for runtime({}), error code: {}", sandboxID, runtimeID,
                            future.GetErrorCode());
                promise.SetValue(
                    Status(static_cast<StatusCode>(future.GetErrorCode()), "Failed to terminate sandbox " + sandboxID));
                return;
            }
            promise.SetValue(Status::OK());
        });
    return promise.GetFuture();
}

messages::StartInstanceResponse ConchExecutor::GenSuccessStartInstanceResponse(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &sandboxID,
    const std::string &sandboxIP)
{
    messages::StartInstanceResponse response;
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("start instance success");
    auto info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();

    response.set_requestid(info.requestid());
    response.mutable_startruntimeinstanceresponse()->set_executortype(static_cast<int32_t>(EXECUTOR_TYPE::CONCH));

    auto instanceResponse = response.mutable_startruntimeinstanceresponse();
    instanceResponse->set_runtimeid(runtimeID);
    instanceResponse->set_containerid(sandboxID);
    instanceResponse->set_containerip(sandboxIP);
    YRLOG_DEBUG("{}|{}|instance({}) runtime({}) with container({}), sandboxIP({})", info.traceid(), info.requestid(),
                info.instanceid(), runtimeID, sandboxID, sandboxIP);

    // set to be zero; conch backend pid is fixed null and never consumed here.
    instanceResponse->set_pid(0);
    auto portMappingsIter = runtime2portMappings_.find(runtimeID);
    if (portMappingsIter != runtime2portMappings_.end()) {
        instanceResponse->set_port(portMappingsIter->second);
    }
    return response;
}

litebus::Future<messages::UpdateCredResponse> ConchExecutor::UpdateCredForRuntime(
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

litebus::Future<Status> ConchExecutor::NotifyInstancesDiskUsageExceedLimit(const std::string &description,
    const int limit)
{
    // Notify instances disk usage exceed limit
    YRLOG_INFO("notify instances disk usage exceed limit: {}", description);

    return Status::OK();
}

bool ConchExecutor::IsRuntimeActive(const std::string &runtimeID)
{
    auto it = runtime2sandboxID_.find(runtimeID);
    return it != runtime2sandboxID_.end();
}

litebus::Future<bool> ConchExecutor::StopAllSandboxes()
{
    std::list<litebus::Future<Status>> futures;
    YRLOG_INFO("{} containers need to stop", runtime2sandboxID_.size());
    // Snapshot keys: TerminateSandbox mutates the map via async callbacks during iteration.
    std::vector<std::string> runtimeIDs;
    runtimeIDs.reserve(runtime2sandboxID_.size());
    for (const auto &kv : runtime2sandboxID_) {
        runtimeIDs.push_back(kv.first);
    }
    for (const auto &runtimeID : runtimeIDs) {
        const auto it = runtime2sandboxID_.find(runtimeID);
        if (it == runtime2sandboxID_.end()) {
            continue;
        }
        futures.emplace_back(TerminateSandbox(runtimeID, it->second));
        YRLOG_INFO("stop runtime {} with container {}", runtimeID, it->second);
    }
    return CollectStatus(futures, "").Then([this]() -> litebus::Future<bool> {
        runtime2sandboxID_.clear();
        runtimeInstanceInfoMap_.clear();
        return true;
    });
}

litebus::Future<::messages::StartInstanceResponse> ConchExecutorProxy::StartInstance(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs)
{
    return litebus::Async(executor_->GetAID(), &ConchExecutor::StartInstance, request, cardIDs);
}

litebus::Future<Status> ConchExecutorProxy::StopInstance(
    const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled)
{
    return litebus::Async(executor_->GetAID(), &ConchExecutor::StopInstance, request, oomKilled);
}

litebus::Future<messages::SnapshotRuntimeResponse> ConchExecutorProxy::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    return litebus::Async(executor_->GetAID(), &ConchExecutor::SnapshotRuntime, request);
}

litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> ConchExecutorProxy::GetRuntimeInstanceInfos()
{
    return litebus::Async(executor_->GetAID(), &ConchExecutor::GetRuntimeInstanceInfos);
}

}  // namespace functionsystem::runtime_manager
