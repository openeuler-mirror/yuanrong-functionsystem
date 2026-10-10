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
#include <cmath>
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
// Max length of the error detail extracted from a non-2xx HTTP body.
namespace {
constexpr size_t MAX_HTTP_ERROR_DETAIL_LEN = 512;

// Extract a human-readable root cause from a non-2xx HTTP body. Recognizes the
// error shapes jiuwenbox emits ({"error": ...} from its exception handlers,
// conchd's {"message": ...}, FastAPI/Pydantic's {"detail": str | [...]}) and
// falls back to the raw body when nothing matches. Single newlines are
// flattened so the result stays a one-line message upstream.
std::string ExtractHttpErrorDetail(int httpStatus, const std::string &body)
{
    std::string detail;
    try {
        auto json = nlohmann::json::parse(body);
        if (json.is_object()) {
            for (const char *key : { "error", "message", "detail" }) {
                if (!json.contains(key) || json[key].is_null()) {
                    continue;
                }
                detail = json[key].is_string() ? json[key].get<std::string>() : json[key].dump();
                if (!detail.empty()) {
                    break;
                }
            }
        }
    } catch (const std::exception &) {
        // Not JSON: fall through to the raw-body fallback below.
    }
    if (detail.empty()) {
        detail = body;
    }
    std::replace(detail.begin(), detail.end(), '\n', ' ');
    if (detail.size() > MAX_HTTP_ERROR_DETAIL_LEN) {
        detail.resize(MAX_HTTP_ERROR_DETAIL_LEN);
        detail += "...(truncated)";
    }
    if (detail.empty()) {
        detail = "HTTP " + std::to_string(httpStatus);
    }
    return detail;
}
}  // namespace

const std::string CONCH_SANDBOX_PREFIX = "/api/v1/sandboxes";
const std::string CONCH_UDS_SOCKET = "/run/jiuwenbox/jiuwenbox.sock";
constexpr size_t HTTP_HEADER_SEPARATOR_LEN = 4;   // length of "\r\n\r\n"
constexpr int HTTP_STATUS_UNPARSED = 0;
constexpr int HTTP_STATUS_OK_MIN = 200;
constexpr int HTTP_STATUS_OK_MAX = 300;   // exclusive upper bound of 2xx
constexpr int HTTP_STATUS_NO_CONTENT = 204;
constexpr double CPU_MILLICORES_PER_CORE = 1000.0;   // CPU resource is in milli-cores (1000 = 1 core)

ConchExecutor::ConchExecutor(const std::string &name, const litebus::AID &functionAgentAID)
    : Executor(name), functionAgentAID_(functionAgentAID)
{
}

void ConchExecutor::Init()
{
    pkgType_ = GetInstallationType();
    if (!AsyncUdsClient::InitEvLoop()) {
        YRLOG_ERROR("ConchExecutor: AsyncUdsClient EvLoop init failed; UDS requests will error out");
    }
    YRLOG_INFO("Start init ConchExecutor");
}

void ConchExecutor::Finalize()
{
    YRLOG_INFO("Start finalize ConchExecutor");
    runtime2portMappings_.clear();
    runtime2sandboxIP_.clear();
    AsyncUdsClient::FinishEvLoop();
    Executor::Finalize();
}

void ConchExecutor::InitConfig()
{
    cmdBuilder_.SetRuntimeConfig(config_);
}

nlohmann::json ConchExecutor::ParseRawResponse(const std::string &response)
{
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        YRLOG_ERROR("invalid HTTP response (no header/body separator)");
        throw std::runtime_error("invalid HTTP response (no header/body separator)");
    }
    std::string respBody = response.substr(headerEnd + HTTP_HEADER_SEPARATOR_LEN);

    // jiuwenbox's conch backend returns 201 + phase=error + error_message on soft failures;
    // 201 is 2xx so it passes this gate and is handled in CreateSandbox via error_message.
    // A hard non-2xx is synthesized as {"error_message": ...} (the body's root cause) instead
    // of SetFailed; the IsError channel stays reserved for transport/protocol failures.
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
        // Surface the root cause from the body; the error_message branch in
        // OnCreateSandboxDone / OnExecInSandboxDone / OnDeleteSandboxDone propagates it upstream.
        return nlohmann::json{ { "error_message", ExtractHttpErrorDetail(httpStatus, respBody) } };
    }
    // 204 No Content (e.g. DELETE success) has an empty body; treat as success.
    // Other 2xx with an empty body is malformed (a body was expected) -> fail.
    if (respBody.empty()) {
        if (httpStatus == HTTP_STATUS_NO_CONTENT) {
            return nlohmann::json::object();
        }
        YRLOG_ERROR("conch returned {} with empty body, expected a JSON body", httpStatus);
        throw std::runtime_error("conch returned empty body");
    }
    YRLOG_INFO("UDS request success, response: {}", respBody);
    return nlohmann::json::parse(respBody);
}

litebus::Future<nlohmann::json> ConchExecutor::SendRequestToConch(const std::string &method,
                                                                  const std::string &path,
                                                                  const nlohmann::json &body)
{
    return AsyncUdsClient::RequestAsync({ CONCH_UDS_SOCKET, method, path, body, "Conch",
        [](const std::string &rawResponse) { return ParseRawResponse(rawResponse); } });
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
        // Resolve yr_runtime_main inside the guest (as DockerExecutor does): the sandbox image's
        // yr layout may differ from the host's, so the runtimePath-based path can miss. os.execv
        // keeps the PID; the single-quoted -c body locates yr_runtime_main.py next to the yr package
        // actually installed in the conch rootfs, then re-execs python on it with the inbound argv.
        auto execPath = cmdBuilder_.GetExecPathFromRuntimeConfig(info.runtimeconfig());
        std::string yrServerPath =
            "import os,sys;"
            "p=os.path.join(os.path.dirname(__import__('yr').__file__),'main','yr_runtime_main.py');"
            "os.execv(sys.executable,[sys.executable,'-u',p]+sys.argv[1:])";
        args.insert(args.begin(), { execPath, "-u", "-c", yrServerPath });
    }

    // Conch sandboxes run on host network with no published host ports, so each user port forward
    // is a direct 1:1 mapping. Emit the JSON array form (matching docker executor) so the deploy
    // response carries portmappings downstream as the portForward instance extension.
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

inline std::string GetPythonExecPath(const google::protobuf::Map<std::string, std::string> &options,
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
    auto result = promise.GetFuture();
    StartByRuntimeID(request, { { PARAM_EXEC_PATH, execPath }, { PARAM_LANGUAGE, language } }, args, envs)
        .OnComplete(litebus::Defer(GetAID(), &ConchExecutor::OnStartRuntimeDone, std::placeholders::_1,
                                   request, std::move(promise)));
    return result;
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

// Build the jiuwenbox sandbox create body for the conch backend. ConchPolicy is extra="forbid"
// (only template_name/vcpu_num/vcpu_max/ram_mb/env/filesystem_policy/network accepted), so none
// of supervisor's bwrap-only keys — policy.environment/process/namespace/cgroup, top-level
// filesystem_policy — may be emitted; any of them yields a 400.
nlohmann::json ConchExecutor::CreateRequest(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();
    const auto &deployOpts = info.deploymentconfig().deployoptions();
    auto getOpt = [&deployOpts](const std::string &key) {
        auto it = deployOpts.find(key);
        return it != deployOpts.end() ? it->second : std::string{};
    };
    const std::string hostUser = getOpt(HOST_USER).empty() ? "agentos" : getOpt(HOST_USER);
    const std::string rootfsJson = getOpt(CONTAINER_ROOTFS);
    const std::string homeDir = "/home/" + hostUser;

    nlohmann::json conch = nlohmann::json::object();

    // template_name from rootfs imageurl; empty -> jiuwenbox env / conchd default fallback.
    conch["template_name"] = ParseRootfsImageUrl(rootfsJson);

    // bind_mounts under conch.filesystem_policy (NOT the bwrap top-level filesystem_policy);
    // each host_path must exist and be a directory on the jiuwenbox host (validate_conch_policy).
    if (auto bindMounts = ParseBindMounts(rootfsJson, runtimeID); !bindMounts.empty()) {
        conch["filesystem_policy"] = { { "bind_mounts", std::move(bindMounts) } };
    }

    // Guest env lives in policy.conch.env (NOT top-level environment, bwrap-only);
    // merge_conch_create_env merges it with the create-API env override.
    conch["env"] = nlohmann::json::object({ { "JIUWENSWARM_HOME", homeDir } });
    for (const auto &kv : info.runtimeconfig().posixenvs()) {
        conch["env"][kv.first] = kv.second;
    }

    // Resources: scheduling CPU is milli-cores / Memory MB (same source BuildCgroup /
    // BuildResources read); mapped to vcpu_num / ram_mb so conch doesn't fall back to its
    // 2-vCPU/2048MB default, rounding up (a sub-core request still gets 1). vcpu_max follows
    // vcpu_num: conchd defaults an omitted one to 2 and rejects vcpu_max < vcpu_num.
    const auto &runtimeResources = info.runtimeconfig().resources().resources();
    if (auto cpuIt = runtimeResources.find(resource_view::CPU_RESOURCE_NAME); cpuIt != runtimeResources.end()) {
        const double cpuVal = Executor::GetEffectiveScalarLimit(cpuIt->second, 0);
        if (cpuVal > 0) {
            const int vcpuNum = std::max(1, static_cast<int>(std::ceil(cpuVal / CPU_MILLICORES_PER_CORE)));
            conch["vcpu_num"] = vcpuNum;
            conch["vcpu_max"] = vcpuNum;
        }
    }
    if (auto memIt = runtimeResources.find(resource_view::MEMORY_RESOURCE_NAME); memIt != runtimeResources.end()) {
        const double memVal = Executor::GetEffectiveScalarLimit(memIt->second, 0);
        if (memVal > 0) {
            conch["ram_mb"] = static_cast<int64_t>(std::ceil(memVal));
        }
    }

    nlohmann::json policy = nlohmann::json::object();
    policy["conch"] = std::move(conch);

    YRLOG_INFO("{}|Create conch sandbox for {}, instance({})", runtimeID, hostUser, info.instanceid());

    nlohmann::json body = nlohmann::json{
        { "sandbox_runtime", "conch" },     // routes jiuwenbox to the conch backend
        { "policy", std::move(policy) },
        { "policy_mode", "append" }
    };
    body["trace_id"] = info.traceid();
    body["instance_id"] = info.instanceid();
    body["runtime_id"] = runtimeID;
    // sandbox_id carries the user-facing instance_id so "jiuwenbox sandbox ls" shows the
    // instance directly. jiuwenbox echoes it back as the sandbox ID, so exec/delete and the
    // containerid stored downstream follow automatically; empty keeps the old
    // server-generated ID.
    if (!info.instanceid().empty()) {
        body["sandbox_id"] = info.instanceid();
    }
    return body;
}

litebus::Future<runtime::v1::StartResponse> ConchExecutor::CreateSandbox(
    const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &runtimeID = request->runtimeinstanceinfo().runtimeid();
    litebus::Promise<runtime::v1::StartResponse> promise;
    auto result = promise.GetFuture();
    nlohmann::json createRequest = CreateRequest(request);

    SendRequestToConch("POST", CONCH_SANDBOX_PREFIX, createRequest)
        .OnComplete(litebus::Defer(GetAID(), &ConchExecutor::OnCreateSandboxDone,
                                   std::placeholders::_1, runtimeID, std::move(promise)));
    return result;
}

void ConchExecutor::OnCreateSandboxDone(const litebus::Future<nlohmann::json> &future,
                                        const std::string &runtimeID,
                                        litebus::Promise<runtime::v1::StartResponse> promise)
{
    auto failWith = [&promise](const std::string &message) {
        runtime::v1::StartResponse failRsp{};
        failRsp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        failRsp.set_message(message);
        promise.SetValue(failRsp);
    };

    if (future.IsError()) {
        YRLOG_ERROR("{}|Create sandbox request failed with error code: {}", runtimeID, future.GetErrorCode());
        // Transport/protocol failure only: no error_message exists.
        failWith("Failed to create sandbox");
        return;
    }

    const nlohmann::json &createResp = future.Get();

    // error_message set means create failed; if a valid id is also returned (orphan sandbox),
    // delete it first then fail. Covers the conch 201+phase=error soft-failure path (SDK not
    // installed / conchd unreachable / template missing) and non-2xx HTTP errors, which
    // ParseRawResponse synthesizes as {"error_message": ...} carrying the body's root cause.
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

    if (!createResp.contains("id") || !createResp["id"].is_string()) {
        YRLOG_ERROR("{}|Create sandbox failed: response does not contain valid id", runtimeID);
        failWith("Create sandbox response does not contain valid id");
        return;
    }

    std::string sandboxId = createResp["id"];
    YRLOG_INFO("{}|Create sandbox success: {}", runtimeID, sandboxId);
    runtime2sandboxID_.emplace(runtimeID, sandboxId);

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
}

void ConchExecutor::OnStartRuntimeDone(const litebus::Future<runtime::v1::StartResponse> &future,
                                       const std::shared_ptr<messages::StartInstanceRequest> &request,
                                       litebus::Promise<messages::StartInstanceResponse> promise)
{
    const auto &info = request->runtimeinstanceinfo();
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
        .OnComplete([promise = std::move(promise)](
                        const litebus::Future<messages::StartInstanceResponse> &innerFuture) mutable {
            if (innerFuture.IsError()) {
                promise.SetFailed(innerFuture.GetErrorCode());
                return;
            }
            promise.SetValue(innerFuture.Get());
        });
}

void ConchExecutor::OnDeleteSandboxDone(const litebus::Future<nlohmann::json> &future,
                                        const std::string &sandboxId,
                                        litebus::Promise<runtime::v1::DeleteResponse> promise)
{
    if (future.IsError()) {
        YRLOG_ERROR("{}|Delete sandbox request failed with error code: {}", sandboxId, future.GetErrorCode());
        // Normalize: StopInstance casts this back to StatusCode; the per-stage codes are for
        // docker's OnDockerReply decoder, which conch has no equivalent of.
        promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        return;
    }
    // Non-2xx arrives as a value carrying error_message (ParseRawResponse synthesizes it
    // instead of throwing); a failed delete must not be reported as success.
    const auto &resp = future.Get();
    if (resp.contains("error_message") && !resp["error_message"].is_null()) {
        YRLOG_ERROR("Failed to delete sandbox {}: {}", sandboxId, resp["error_message"].get<std::string>());
        promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        return;
    }
    promise.SetValue(runtime::v1::DeleteResponse{});
}

void ConchExecutor::OnExecInSandboxDone(const litebus::Future<nlohmann::json> &future,
                                        const std::string &runtimeID,
                                        const std::string &sandboxId,
                                        litebus::Promise<runtime::v1::StartResponse> promise)
{
    // Synchronously clear mappings before resolving the promise so the caller observes an
    // empty runtime->sandbox map when the future completes; the async TerminateSandbox below
    // is fire-and-forget, so cleanMappings=false avoids re-clearing an already-cleared map.
    auto failWith = [this, &promise, &runtimeID, &sandboxId](const std::string &message) {
        runtime::v1::StartResponse failRsp{};
        failRsp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        failRsp.set_message(message);
        CleanupRuntimeMappings(runtimeID);
        promise.SetValue(failRsp);
        CleanupSandboxAfterFailure(runtimeID, sandboxId, false);
    };

    if (future.IsError()) {
        YRLOG_ERROR("{}|Failed to exec command in sandbox {}: {}", runtimeID, sandboxId,
                    static_cast<int>(future.GetErrorCode()));
        failWith("Failed to execute command in sandbox");
        return;
    }

    const nlohmann::json &execResp = future.Get();
    if (execResp.contains("error_message") && !execResp["error_message"].is_null()) {
        std::string errorMsg = execResp["error_message"].get<std::string>();
        YRLOG_ERROR("{}|Failed to exec command in sandbox {} with error_message: {}", runtimeID, sandboxId, errorMsg);
        failWith("Failed to execute command in sandbox" + (errorMsg.empty() ? "" : ": " + errorMsg));
        return;
    }

    runtime::v1::StartResponse rsp{};
    rsp.set_code(0);
    rsp.set_message("success");
    rsp.set_id(sandboxId);
    promise.SetValue(rsp);
}

nlohmann::json ConchExecutor::BuildCommand(const std::shared_ptr<runtime::v1::StartRequest> &start)
{
    auto command = nlohmann::json::array();

    std::string quotedArgv;
    for (int i = 0; i < start->command().size(); ++i) {
        if (i > 0) {
            quotedArgv += " ";
        }
        // Each argv token must be shell-quoted: command() args often contain arbitrary user
        // content (e.g. python -c "print('hello')") and must not be interpreted by sh.
        quotedArgv += ShellQuote(start->command()[i]);
    }

    auto stdOut = start->stdout();
    // Tolerant redirect (aligned with DockerExecutor): when the log path is empty (log dir
    // provisioning failed) or unwritable inside the conch rootfs (log bind-mount skipped),
    // bare-exec the command with inherited stdout/stderr so the runtime still starts instead
    // of sh dying on `>''` / open(ENOENT). Non-empty: probe writability in a subshell; on
    // success append (>> + 2>&1, in step with LogManagerActor's copytruncate rotation), on
    // failure fall back to inherited fds. stdout and stderr share one path.
    std::string cmdLine;
    if (stdOut.empty()) {
        cmdLine = "exec " + quotedArgv;
    } else {
        cmdLine = "if ( >>" + ShellQuote(stdOut) + " ) 2>/dev/null; then exec " + quotedArgv
            + " >>" + ShellQuote(stdOut) + " 2>&1; else exec " + quotedArgv + "; fi";
    }

    command.push_back("sh");
    command.push_back("-c");
    command.push_back(cmdLine);
    return command;
}

nlohmann::json ConchExecutor::BuildExecRequest(const std::string &runtimeID,
                                               const std::shared_ptr<runtime::v1::StartRequest> &start,
                                               const std::string &sandboxId)
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
    auto result = promise.GetFuture();
    SendRequestToConch("POST", execPath, execRequest)
        .OnComplete(litebus::Defer(GetAID(), &ConchExecutor::OnExecInSandboxDone, std::placeholders::_1,
                                   runtimeID, sandboxId, std::move(promise)));
    return result;
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
    std::string sandboxId = req->id();
    litebus::Promise<runtime::v1::DeleteResponse> promise;
    auto result = promise.GetFuture();
    SendRequestToConch("DELETE", path)
        .OnComplete(litebus::Defer(GetAID(), &ConchExecutor::OnDeleteSandboxDone, std::placeholders::_1,
                                   std::move(sandboxId), std::move(promise)));
    return result;
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

// TU-local copy of the same-named helper in supervisor_executor.cpp (identical body);
// anonymous namespace avoids the multiple-definition clash in runtime_manager_lib.
namespace {
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
}  // namespace

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

    // set to be zero
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
    // Check if runtime is active
    auto it = runtime2sandboxID_.find(runtimeID);
    return it != runtime2sandboxID_.end();
}

litebus::Future<bool> ConchExecutor::StopAllSandboxes()
{
    std::list<litebus::Future<Status>> futures;
    YRLOG_INFO("{} sandboxes need to stop", runtime2sandboxID_.size());
    for (auto [runtimeID, containerID] : runtime2sandboxID_) {
        futures.emplace_back(TerminateSandbox(runtimeID, containerID));
        YRLOG_INFO("stop runtime {} with sandbox {}", runtimeID, containerID);
    }
    return CollectStatus(futures, "").Then([]() -> litebus::Future<bool> { return true; });
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
