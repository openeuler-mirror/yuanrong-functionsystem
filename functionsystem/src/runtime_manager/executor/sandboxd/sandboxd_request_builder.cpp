/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sandboxd_request_builder.h"

#include <algorithm>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/files.h"
#include "common/utils/path.h"
#include "runtime_manager/executor/executor.h"
#include "runtime_manager/executor/sandboxd/sandbox_command_utils.h"

namespace functionsystem::runtime_manager {

namespace {
// Log-redirect
const std::string YR_ONLY_STDOUT          = "YR_ONLY_STDOUT";
const std::string YR_RT_WORKING_DIR       = "YR_RT_WORKING_DIR";
// Deploy-option keys
const std::string CONTAINER_ROOTFS        = "rootfs";
const std::string CONTAINER_EXTRA_CONFIG  = "extra_config";
const std::string CONTAINER_MOUNTS        = "mounts";
const std::string CONTAINER_NETWORK       = "network";
// Resource defaults
constexpr double DEFAULT_CPU_MILLICORES   = 500.0;
constexpr double DEFAULT_MEMORY_MB        = 500.0;
// Mount
const std::string YR_FUNCTION_LIB_PATH   = "YR_FUNCTION_LIB_PATH";
const std::string FUNCTION_LIB_PATH      = "FUNCTION_LIB_PATH";
// Namespace alias for brevity
using namespace resource_view;  // NOLINT(google-build-using-namespace)

// Returns the directory portion of a path (like dirname(3))
std::string DirName(const std::string &path)
{
    if (path.empty()) {
        return "";
    }
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return "";
    }
    return pos == 0 ? "/" : path.substr(0, pos);
}

void ApplyS3RootfsConfig(runtime::v1::StartRequest &start, const nlohmann::json &rootfsConfig)
{
    if (!rootfsConfig.contains("storageInfo")) {
        return;
    }
    auto s3 = start.mutable_rootfs()->mutable_s3_config();
    const auto &storageInfo = rootfsConfig.at("storageInfo");
    if (storageInfo.contains("endpoint"))
        s3->set_endpoint(storageInfo.at("endpoint").get<std::string>());
    if (storageInfo.contains("bucket"))
        s3->set_bucket(storageInfo.at("bucket").get<std::string>());
    if (storageInfo.contains("object"))
        s3->set_object(storageInfo.at("object").get<std::string>());
    if (storageInfo.contains("accessKey"))
        s3->set_access_key_id(storageInfo.at("accessKey").get<std::string>());
    if (storageInfo.contains("secretKey"))
        s3->set_access_key_secret(storageInfo.at("secretKey").get<std::string>());
}

// Parse the rootfs deploy-option JSON onto the flat StartRequest. The flat
// request exposes rootfs + runtime as top-level fields (no FunctionRuntime).
Status RootfsJsonParse(runtime::v1::StartRequest &start, const std::string &rootfsJson)
{
    try {
        nlohmann::json j = nlohmann::json::parse(rootfsJson);
        if (j.contains("runtime")) {
            start.set_runtime(j.at("runtime").get<std::string>());
        }
        if (j.contains("readonly")) {
            bool ro = false;
            if (j.at("readonly").is_boolean()) {
                ro = j.at("readonly").get<bool>();
            } else if (j.at("readonly").is_string()) {
                const auto &s = j.at("readonly").get<std::string>();
                ro = (s == "true" || s == "1");
            }
            start.mutable_rootfs()->set_readonly(ro);
        }
        if (!j.contains("type")) {
            return Status::OK();
        }
        const std::string typeStr = j.at("type").get<std::string>();
        if (typeStr == "s3") {
            start.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::S3);
            ApplyS3RootfsConfig(start, j);
        } else if (typeStr == "image") {
            start.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::IMAGE);
            if (j.contains("imageurl")) {
                start.mutable_rootfs()->set_image_url(j.at("imageurl").get<std::string>());
            }
        } else if (typeStr == "local") {
            start.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::LOCAL);
            if (j.contains("path")) {
                start.mutable_rootfs()->set_path(j.at("path").get<std::string>());
            }
        }
    } catch (const std::exception &e) {
        auto msg = fmt::format("Failed to parse rootfs JSON: {}", e.what());
        YRLOG_ERROR("{}", msg);
        return Status(StatusCode::ERR_PARAM_INVALID, msg);
    }
    return Status::OK();
}

bool HasCustomRootfs(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    return opts.contains(CONTAINER_ROOTFS);
}

std::string ResolveRuntimeLanguage(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    std::string language = request->runtimeinstanceinfo().runtimeconfig().language();
    std::transform(language.begin(), language.end(), language.begin(), ::tolower);
    return language;
}

void SetS3MountSource(runtime::v1::Mount *mount, const nlohmann::json &s3)
{
    auto *s3Config = mount->mutable_s3_config();
    if (s3.find("endpoint") != s3.end()) {
        s3Config->set_endpoint(s3.at("endpoint").get<std::string>());
    }
    if (s3.find("bucket") != s3.end()) {
        s3Config->set_bucket(s3.at("bucket").get<std::string>());
    }
    if (s3.find("object") != s3.end()) {
        s3Config->set_object(s3.at("object").get<std::string>());
    }
    if (s3.find("accessKey") != s3.end()) {
        s3Config->set_access_key_id(s3.at("accessKey").get<std::string>());
    }
    if (s3.find("secretKey") != s3.end()) {
        s3Config->set_access_key_secret(s3.at("secretKey").get<std::string>());
    }
}

void SetMountSource(runtime::v1::Mount *mount, const nlohmann::json &item)
{
    if (item.find("host_path") != item.end()) {
        mount->set_host_path(item.at("host_path").get<std::string>());
    } else if (item.find("s3_config") != item.end() && item.at("s3_config").is_object()) {
        SetS3MountSource(mount, item.at("s3_config"));
    } else if (item.find("image_url") != item.end()) {
        mount->set_image_url(item.at("image_url").get<std::string>());
    }
}

Status MountsJsonParse(runtime::v1::StartRequest &start, const std::string &mountsJson)
{
    try {
        nlohmann::json arr = nlohmann::json::parse(mountsJson);
        if (!arr.is_array()) {
            return Status(StatusCode::ERR_PARAM_INVALID, "mounts config must be a JSON array");
        }
        for (const auto &item : arr) {
            if (!item.is_object()) {
                continue;
            }
            auto *mount = start.add_mounts();

            if (item.find("type") != item.end()) {
                mount->set_type(item.at("type").get<std::string>());
            }
            if (item.find("target") != item.end()) {
                mount->set_target(item.at("target").get<std::string>());
            }
            if (item.find("options") != item.end() && item.at("options").is_array()) {
                for (const auto &opt : item.at("options")) {
                    if (opt.is_string()) {
                        mount->add_options(opt.get<std::string>());
                    }
                }
            }

            SetMountSource(mount, item);
        }
    } catch (std::exception &e) {
        auto err = fmt::format("Failed to parse mounts JSON: {}", std::string(e.what()));
        YRLOG_ERROR("{}", err);
        return Status(StatusCode::ERR_PARAM_INVALID, err);
    }
    return Status::OK();
}

// Build log paths and ensure the files exist. Fills stdOut and stdErr.
void ResolveLogPaths(const std::string &logDir, const std::string &runtimeID, std::string &stdOut, std::string &stdErr)
{
    if (!litebus::os::ExistPath(logDir)) {
        YRLOG_WARN("std log dir {} not found, attempting mkdir", logDir);
        if (!litebus::os::Mkdir(logDir).IsNone()) {
            YRLOG_WARN("failed to create {}: {}", logDir, litebus::os::Strerror(errno));
            return;
        }
    }
    stdOut = litebus::os::Join(logDir, fmt::format("{}.out", runtimeID));
    stdErr = litebus::os::Join(logDir, fmt::format("{}.err", runtimeID));
    if (!litebus::os::ExistPath(stdOut) && TouchFile(stdOut) != 0) {
        YRLOG_WARN("create stdout log {} failed: {}", stdOut, litebus::os::Strerror(errno));
    }
    if (!litebus::os::ExistPath(stdErr) && TouchFile(stdErr) != 0) {
        YRLOG_WARN("create stderr log {} failed: {}", stdErr, litebus::os::Strerror(errno));
    }
}

}  // namespace

// ── Construction ──────────────────────────────────────────────────────────────

SandboxdRequestBuilder::SandboxdRequestBuilder(const CommandBuilder &cmdBuilder) : cmdBuilder_(cmdBuilder)
{
}

// ── Public Build ──────────────────────────────────────────────────────────────

std::pair<Status, std::shared_ptr<runtime::v1::StartRequest>> SandboxdRequestBuilder::Build(
    const SandboxdStartParams &params) const
{
    return BuildStart(params);
}

// ── Start path ────────────────────────────────────────────────────────────────

std::pair<Status, std::shared_ptr<runtime::v1::StartRequest>> SandboxdRequestBuilder::BuildStart(
    const SandboxdStartParams &params) const
{
    auto start = std::make_shared<runtime::v1::StartRequest>();

    // sandbox_id is intentionally left empty: per the sandboxd SandboxService
    // contract, sandboxd generates the sandbox ID and returns it in
    // StartResponse.id; the executor stores that via UpdateSandboxID(runtimeID).
    // Passing the client runtimeID here would conflate the two identities.

    // Only reference a template when sandboxd has already registered it.
    // Production sandboxd rejects unknown template_id values; custom rootfs
    // starts are never template-backed.
    const auto &templateID = params.request->runtimeinstanceinfo().container().id();
    if (!HasCustomRootfs(params.request) && params.registeredTemplateIDs.count(templateID) > 0) {
        start->set_template_id(templateID);
    }

    // Attach tenant ID as a metric label for sandboxd observability.
    // tenant_id is passed via runtimeconfig.posixenvs as YR_TENANT_ID.
    {
        const auto &posixEnvs = params.request->runtimeinstanceinfo().runtimeconfig().posixenvs();
        if (auto it = posixEnvs.find("YR_TENANT_ID"); it != posixEnvs.end() && !it->second.empty()) {
            (*start->mutable_metric_labels())["tenantid"] = it->second;
        }
    }

    // Resolve rootfs (and runtime handler) from deploy options or container config.
    if (auto s = BuildRootfs(params.request, *start); !s.IsOk()) {
        return {s, nullptr};
    }

    ApplyExtraConfig(params.request, start.get());
    ApplyPortMappings(params.portMappings, start->mutable_ports());

    std::string workingRoot;
    ApplyBootstrapMount(params.request, start->mutable_mounts(), workingRoot);
    ApplyCommands(params.request, params.cmdArgs, start.get());
    // Flat request has a single envs map; seed the working-root env there.
    (*start->mutable_envs())[YR_RT_WORKING_DIR] = workingRoot;

    Envs updatedEnvs = ApplyCodeMounts(params.request, start->mutable_mounts(), params.envs);

    // Build custom mounts from deployOptions
    {
        const auto &opts = params.request->runtimeinstanceinfo().deploymentconfig().deployoptions();
        if (auto it = opts.find(CONTAINER_MOUNTS); it != opts.end()) {
            if (auto status = MountsJsonParse(*start, it->second); !status.IsOk()) {
                return {status, nullptr};
            }
        }
        // Network mode (optional); empty => sandbox network on the sandboxd side.
        if (auto netIt = opts.find(CONTAINER_NETWORK); netIt != opts.end()) {
            start->set_network(netIt->second);
        }
    }

    ApplyResources(params.request, start->mutable_resources());
    ApplyEnvsAndLogs(updatedEnvs, params.runtimeID, start.get());

    // YR_LANGUAGE follows the service runtime field. The container runtime is
    // the sandbox backend (for example runc/runsc), not the user runtime.
    (*start->mutable_envs())["YR_LANGUAGE"] = ResolveRuntimeLanguage(params.request);

    // trace_id is the distributed trace ID propagated from the upstream request
    // (runtimeinstanceinfo().traceid()), not the local runtimeID.
    start->set_trace_id(params.request->runtimeinstanceinfo().traceid());

    return {Status::OK(), std::move(start)};
}

Status SandboxdRequestBuilder::BuildRootfs(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                           runtime::v1::StartRequest &start) const
{
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    if (!opts.contains(CONTAINER_ROOTFS)) {
        // No custom rootfs: take runtime handler + rootfs from the container config.
        // sandbox_id is left empty for sandboxd to generate.
        start.set_runtime(request->runtimeinstanceinfo().container().runtime());
        *start.mutable_rootfs() = request->runtimeinstanceinfo().container().rootfsconfig();
        return Status::OK();
    }
    // Custom rootfs: parse it from deploy options. sandbox_id left empty (sandboxd generates).
    return RootfsJsonParse(start, opts.at(CONTAINER_ROOTFS));
}

// ── Shared helpers ────────────────────────────────────────────────────────────

Envs SandboxdRequestBuilder::ApplyCodeMounts(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                             google::protobuf::RepeatedPtrField<runtime::v1::Mount> *mounts,
                                             const Envs &envs) const
{
    Envs updated = envs;
    auto workingDirIt = envs.posixEnvs.find(UNZIPPED_WORKING_DIR);
    if (workingDirIt == envs.posixEnvs.end() || workingDirIt->second.empty()) {
        return updated;
    }

    const auto &deploySpec = request->runtimeinstanceinfo().deploymentconfig();
    auto funcPath =
        litebus::os::Join(litebus::os::Join(deploySpec.deploydir(), RUNTIME_LAYER_DIR_NAME), RUNTIME_FUNC_DIR_NAME);

    if (auto libIt = envs.posixEnvs.find(YR_FUNCTION_LIB_PATH);
        libIt != envs.posixEnvs.end() && !libIt->second.empty()) {
        funcPath = libIt->second;
    }

    auto *code = mounts->Add();
    code->set_type("bind");
    if (workingDirIt->second.find(".img") != std::string::npos) {
        code->set_type("erofs");
        funcPath = DirName(workingDirIt->second);
    }
    code->set_host_path(workingDirIt->second);
    code->set_target(request->runtimeinstanceinfo().container().mountpoint());

    updated.posixEnvs[UNZIPPED_WORKING_DIR]  = code->target();
    updated.posixEnvs[YR_FUNCTION_LIB_PATH]  = code->target();
    updated.posixEnvs[FUNCTION_LIB_PATH]     = code->target();

    for (const auto &layer : GenerateLayerPath(request->runtimeinstanceinfo())) {
        auto *layerMount = mounts->Add();
        layerMount->set_type("bind");
        layerMount->set_host_path(layer);
        std::string target = layer;
        std::replace(target.begin(), target.end(), '/', '-');
        layerMount->set_target(litebus::os::Join("/opt", target));
    }
    return updated;
}

void SandboxdRequestBuilder::ApplyBootstrapMount(const std::shared_ptr<messages::StartInstanceRequest> &request,
    google::protobuf::RepeatedPtrField<runtime::v1::Mount> *mounts,
    std::string &workingRoot) const
{
    workingRoot = "/";
    const auto &bc = request->runtimeinstanceinfo().bootstrapconfig();
    if (bc.type().empty() || bc.root().empty()) {
        return;
    }
    if (!HasCustomRootfs(request)) {
        YRLOG_WARN("custom rootfs not specified; skipping bootstrap working root mount");
        return;
    }
    auto *mount = mounts->Add();
    const std::string mountDst = "/__yuanrong";
    mount->set_host_path(bc.root());
    mount->set_target(mountDst);
    mount->set_type((bc.type() == "erofs") ? "erofs" : "bind");
    workingRoot = mountDst;
}

void SandboxdRequestBuilder::ApplyCommands(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                           const CommandArgs &cmdArgs, runtime::v1::StartRequest *start) const
{
    for (const auto &cmd : BuildBootstrapCommands(request)) {
        *start->add_command() = cmd;
    }
    bool skipNext = false;
    for (const auto &arg : cmdArgs.args) {
        if (skipNext) {
            skipNext = false;
            continue;
        }
        if (arg == "--job_id" || arg.rfind("--job_id=", 0) == 0 || arg == "--runtime_id"
            || arg.rfind("--runtime_id=", 0) == 0) {
            if (arg == "--job_id" || arg == "--runtime_id") {
                skipNext = true;
            }
            continue;
        }
        *start->add_command() = arg;
    }
}

void SandboxdRequestBuilder::ApplyResources(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                            google::protobuf::Map<std::string, double> *resources) const
{
    const auto &res = request->runtimeinstanceinfo().runtimeconfig().resources().resources();
    auto getEffectiveValue = [](const resource_view::Resource &res, double defaultVal) -> double {
        if (res.type() != ValueType::Value_Type_SCALAR) {
            return defaultVal;
        }
        double limit = res.scalar().limit();
        if (limit > 0) {
            return limit;
        }
        return res.scalar().value();
    };

    auto cpuIt = res.find(CPU_RESOURCE_NAME);
    (*resources)[CPU_RESOURCE_NAME] =
        (cpuIt != res.end()) ? getEffectiveValue(cpuIt->second, DEFAULT_CPU_MILLICORES) : DEFAULT_CPU_MILLICORES;

    auto memIt = res.find(MEMORY_RESOURCE_NAME);
    (*resources)[MEMORY_RESOURCE_NAME] =
        (memIt != res.end()) ? getEffectiveValue(memIt->second, DEFAULT_MEMORY_MB) : DEFAULT_MEMORY_MB;
}

void SandboxdRequestBuilder::ApplyEnvsAndLogs(const Envs &envs, const std::string &runtimeID,
                                              runtime::v1::StartRequest *start) const
{
    const auto &config = cmdBuilder_.GetConfig();
    const std::string logDir = litebus::os::Join(config.runtimeLogPath, config.runtimeStdLogDir);

    // Flat request carries a single envs map (no separate runtime/user envs).
    const auto combined = cmdBuilder_.CombineEnvs(envs);
    start->mutable_envs()->insert(combined.begin(), combined.end());
    (*start->mutable_envs())[YR_ONLY_STDOUT] = "true";

    std::string stdOut;
    std::string stdErr;
    ResolveLogPaths(logDir, runtimeID, stdOut, stdErr);
    start->set_stdout(stdOut);
    start->set_stderr(stdErr);
}

void SandboxdRequestBuilder::ApplyExtraConfig(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                              runtime::v1::StartRequest *start) const
{
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    if (auto it = opts.find(CONTAINER_EXTRA_CONFIG); it != opts.end()) {
        start->set_extra_config(it->second);
    }
}

void SandboxdRequestBuilder::ApplyPortMappings(const std::vector<std::string> &portMappings,
                                               google::protobuf::RepeatedPtrField<std::string> *ports) const
{
    for (const auto &mapping : portMappings) {
        *ports->Add() = mapping;
    }
}

}  // namespace functionsystem::runtime_manager
