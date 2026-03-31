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

#include "sandbox_request_builder.h"

#include <algorithm>
#include <sstream>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/files.h"
#include "common/utils/path.h"
#include "runtime_manager/executor/executor.h"

namespace functionsystem::runtime_manager {

namespace {
// Log-redirect
const std::string YR_ONLY_STDOUT          = "YR_ONLY_STDOUT";
const std::string YR_RT_WORKING_DIR       = "YR_RT_WORKING_DIR";
// Deploy-option keys
const std::string CONTAINER_ROOTFS        = "CONTAINER_ROOTFS";
const std::string CONTAINER_EXTRA_CONFIG  = "CONTAINER_EXTRA_CONFIG";
const std::string CONTAINER_NETWORK       = "CONTAINER_NETWORK";
// Resource defaults
constexpr double DEFAULT_CPU_MILLICORES   = 500.0;
constexpr double DEFAULT_MEMORY_MB        = 500.0;
// Mount
const std::string YR_FUNCTION_LIB_PATH   = "YR_FUNCTION_LIB_PATH";
const std::string FUNCTION_LIB_PATH      = "FUNCTION_LIB_PATH";
// Layer directory names (mirrors function_agent/common/constants.h)
const std::string RUNTIME_LAYER_DIR_NAME  = "layer";
const std::string RUNTIME_FUNC_DIR_NAME   = "func";
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

// Build the entrypoint + cmd vector from bootstrapConfig
std::vector<std::string> BuildCommands(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    std::vector<std::string> cmds;
    const auto &bc = request->runtimeinstanceinfo().bootstrapconfig();
    for (const std::string *src : {&bc.entrypoint(), &bc.cmd()}) {
        if (src->empty()) {
            continue;
        }
        std::istringstream iss(*src);
        std::string tok;
        while (iss >> tok) {
            cmds.push_back(tok);
        }
    }
    return cmds;
}

Status RootfsJsonParse(runtime::v1::FunctionRuntime &funcRt, const std::string &rootfsJson)
{
    try {
        nlohmann::json j = nlohmann::json::parse(rootfsJson);
        if (j.contains("runtime")) {
            funcRt.set_sandbox(j.at("runtime").get<std::string>());
        }
        if (j.contains("readonly")) {
            bool ro = false;
            if (j.at("readonly").is_boolean()) {
                ro = j.at("readonly").get<bool>();
            } else if (j.at("readonly").is_string()) {
                const auto &s = j.at("readonly").get<std::string>();
                ro = (s == "true" || s == "1");
            }
            funcRt.mutable_rootfs()->set_readonly(ro);
        }
        if (!j.contains("type")) {
            return Status::OK();
        }
        const std::string typeStr = j.at("type").get<std::string>();
        if (typeStr == "s3") {
            funcRt.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::S3);
            if (!j.contains("storageInfo")) {
                return Status::OK();
            }
            auto s3 = funcRt.mutable_rootfs()->mutable_s3_config();
            const auto &si = j.at("storageInfo");
            if (si.contains("endpoint"))  s3->set_endpoint(si.at("endpoint").get<std::string>());
            if (si.contains("bucket"))    s3->set_bucket(si.at("bucket").get<std::string>());
            if (si.contains("object"))    s3->set_object(si.at("object").get<std::string>());
            if (si.contains("accessKey")) s3->set_accesskeyid(si.at("accessKey").get<std::string>());
            if (si.contains("secretKey")) s3->set_accesskeysecret(si.at("secretKey").get<std::string>());
        } else if (typeStr == "image") {
            funcRt.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::IMAGE);
            if (j.contains("imageurl")) {
                funcRt.mutable_rootfs()->set_image_url(j.at("imageurl").get<std::string>());
            }
        } else if (typeStr == "local") {
            funcRt.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::LOCAL);
            if (j.contains("path")) {
                funcRt.mutable_rootfs()->set_path(j.at("path").get<std::string>());
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

// Build log paths and ensure the files exist. Fills stdOut and stdErr.
void ResolveLogPaths(const std::string &logDir, const std::string &runtimeID,
                     std::string &stdOut, std::string &stdErr)
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

SandboxRequestBuilder::SandboxRequestBuilder(const CommandBuilder &cmdBuilder)
    : cmdBuilder_(cmdBuilder)
{
}

// ── Public Build ──────────────────────────────────────────────────────────────

std::pair<Status, SandboxRequestBuilder::SandboxProtoRequest> SandboxRequestBuilder::Build(
    const SandboxStartParams &params) const
{
    if (params.checkpointID.empty()) {
        auto [status, req] = BuildStart(params);
        if (!status.IsOk()) {
            return {status, {}};
        }
        return {Status::OK(), std::move(req)};
    }
    auto [status, req] = BuildRestore(params);
    if (!status.IsOk()) {
        return {status, {}};
    }
    return {Status::OK(), std::move(req)};
}

std::shared_ptr<runtime::v1::StartRequest> SandboxRequestBuilder::AsStart(
    const SandboxProtoRequest &req)
{
    return std::get<std::shared_ptr<runtime::v1::StartRequest>>(req);
}

std::shared_ptr<runtime::v1::RestoreRequest> SandboxRequestBuilder::AsRestore(
    const SandboxProtoRequest &req)
{
    return std::get<std::shared_ptr<runtime::v1::RestoreRequest>>(req);
}

// ── Start path ────────────────────────────────────────────────────────────────

std::pair<Status, std::shared_ptr<runtime::v1::StartRequest>> SandboxRequestBuilder::BuildStart(
    const SandboxStartParams &params) const
{
    auto start = std::make_shared<runtime::v1::StartRequest>();

    if (auto s = BuildRootfs(params.request, *start); !s.IsOk()) {
        return {s, nullptr};
    }

    ApplyExtraConfig(params.request, start.get());
    ApplyPortMappings(params.portMappings, start->mutable_ports());

    std::string workingRoot;
    ApplyBootstrapMount(params.request, start->mutable_mounts(), workingRoot);
    ApplyCommands(params.request, params.cmdArgs, start->mutable_funcruntime());
    (*start->mutable_funcruntime()->mutable_runtimeenvs())[YR_RT_WORKING_DIR] = workingRoot;

    Envs updatedEnvs = ApplyCodeMounts(params.request, start->mutable_mounts(), params.envs);
    ApplyResources(params.request, start->mutable_resources());
    ApplyEnvsAndLogs(updatedEnvs, params.runtimeID, start.get());

    return {Status::OK(), std::move(start)};
}

Status SandboxRequestBuilder::BuildRootfs(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                           runtime::v1::StartRequest &start) const
{
    auto *funcRt = start.mutable_funcruntime();
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    if (!opts.contains(CONTAINER_ROOTFS)) {
        funcRt->set_id(request->runtimeinstanceinfo().container().id());
        funcRt->set_sandbox(request->runtimeinstanceinfo().container().runtime());
        *funcRt->mutable_rootfs() = request->runtimeinstanceinfo().container().rootfsconfig();
        return Status::OK();
    }
    // Custom rootfs: use runtimeID to avoid re-using a pre-warmed seed with inconsistent rootfs
    funcRt->set_id(request->runtimeinstanceinfo().runtimeid());
    return RootfsJsonParse(*funcRt, opts.at(CONTAINER_ROOTFS));
}

// ── Restore path ──────────────────────────────────────────────────────────────

std::pair<Status, std::shared_ptr<runtime::v1::RestoreRequest>> SandboxRequestBuilder::BuildRestore(
    const SandboxStartParams &params) const
{
    auto restore = std::make_shared<runtime::v1::RestoreRequest>();
    restore->set_ckpt_dir(params.checkpointID);
    restore->set_trace_id(params.runtimeID);

    // Identity goes on funcruntime (RestoreRequest has no top-level set_id)
    auto *funcRt = restore->mutable_funcruntime();
    funcRt->set_id(params.request->runtimeinstanceinfo().container().id());
    funcRt->set_sandbox(params.request->runtimeinstanceinfo().container().runtime());
    *funcRt->mutable_rootfs() = params.request->runtimeinstanceinfo().container().rootfsconfig();

    ApplyExtraConfig(params.request, restore.get());

    std::string workingRoot;
    ApplyBootstrapMount(params.request, restore->mutable_mounts(), workingRoot);
    ApplyCommands(params.request, params.cmdArgs, restore->mutable_funcruntime());
    (*restore->mutable_funcruntime()->mutable_runtimeenvs())[YR_RT_WORKING_DIR] = workingRoot;

    Envs updatedEnvs = ApplyCodeMounts(params.request, restore->mutable_mounts(), params.envs);
    ApplyResources(params.request, restore->mutable_resources());
    ApplyEnvsAndLogs(updatedEnvs, params.runtimeID, restore.get());

    return {Status::OK(), std::move(restore)};
}

// ── Shared helpers ────────────────────────────────────────────────────────────

Envs SandboxRequestBuilder::ApplyCodeMounts(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                             google::protobuf::RepeatedPtrField<runtime::v1::Mount> *mounts,
                                             const Envs &envs) const
{
    Envs updated = envs;
    auto workingDirIt = envs.posixEnvs.find(UNZIPPED_WORKING_DIR);
    if (workingDirIt == envs.posixEnvs.end() || workingDirIt->second.empty()) {
        return updated;
    }

    const auto &deploySpec = request->runtimeinstanceinfo().deploymentconfig();
    auto funcPath = litebus::os::Join(litebus::os::Join(deploySpec.deploydir(), RUNTIME_LAYER_DIR_NAME),
                                      RUNTIME_FUNC_DIR_NAME);

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
    code->set_source(workingDirIt->second);
    code->set_target(request->runtimeinstanceinfo().container().mountpoint());

    updated.posixEnvs[UNZIPPED_WORKING_DIR]  = code->target();
    updated.posixEnvs[YR_FUNCTION_LIB_PATH]  = code->target();
    updated.posixEnvs[FUNCTION_LIB_PATH]     = code->target();

    for (const auto &layer : GenerateLayerPath(request->runtimeinstanceinfo())) {
        auto *layerMount = mounts->Add();
        layerMount->set_type("bind");
        layerMount->set_source(layer);
        std::string target = layer;
        std::replace(target.begin(), target.end(), '/', '-');
        layerMount->set_target(litebus::os::Join("/opt", target));
    }
    return updated;
}

void SandboxRequestBuilder::ApplyBootstrapMount(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
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
    const std::string mountDst = "/__yuanrong/";
    mount->set_source(bc.root());
    mount->set_target(mountDst);
    mount->set_type((bc.type() == "erofs") ? "erofs" : "bind");
    workingRoot = mountDst;
}

void SandboxRequestBuilder::ApplyCommands(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                           const CommandArgs &cmdArgs,
                                           runtime::v1::FunctionRuntime *funcRt) const
{
    for (const auto &cmd : BuildCommands(request)) {
        *funcRt->add_command() = cmd;
    }
    for (const auto &arg : cmdArgs.args) {
        *funcRt->add_command() = arg;
    }
}

void SandboxRequestBuilder::ApplyResources(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                            google::protobuf::Map<std::string, double> *resources) const
{
    const auto &res = request->runtimeinstanceinfo().runtimeconfig().resources().resources();
    auto cpuIt = res.find(CPU_RESOURCE_NAME);
    (*resources)[CPU_RESOURCE_NAME] =
        (cpuIt != res.end() && cpuIt->second.type() == ValueType::Value_Type_SCALAR)
            ? cpuIt->second.scalar().value()
            : DEFAULT_CPU_MILLICORES;

    auto memIt = res.find(MEMORY_RESOURCE_NAME);
    (*resources)[MEMORY_RESOURCE_NAME] =
        (memIt != res.end() && memIt->second.type() == ValueType::Value_Type_SCALAR)
            ? memIt->second.scalar().value()
            : DEFAULT_MEMORY_MB;
}

template <typename ProtoReq>
void SandboxRequestBuilder::ApplyEnvsAndLogs(const Envs &envs, const std::string &runtimeID,
                                              ProtoReq *req) const
{
    const auto &config = cmdBuilder_.GetConfig();
    const std::string logDir = litebus::os::Join(config.runtimeLogPath, config.runtimeStdLogDir);

    const auto combined = cmdBuilder_.CombineEnvs(envs);
    req->mutable_userenvs()->insert(combined.begin(), combined.end());
    (*req->mutable_userenvs())[YR_ONLY_STDOUT] = "true";

    std::string stdOut, stdErr;
    ResolveLogPaths(logDir, runtimeID, stdOut, stdErr);
    req->set_stdout(stdOut);
    req->set_stderr(stdErr);
}

template <typename ProtoReq>
void SandboxRequestBuilder::ApplyExtraConfig(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                              ProtoReq *req) const
{
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    if (auto it = opts.find(CONTAINER_EXTRA_CONFIG); it != opts.end()) {
        req->set_extraconfig(it->second);
    }
}

void SandboxRequestBuilder::ApplyPortMappings(const std::vector<std::string> &portMappings,
                                               google::protobuf::RepeatedPtrField<std::string> *ports) const
{
    for (const auto &mapping : portMappings) {
        *ports->Add() = mapping;
    }
}

// Explicit template instantiations
template void SandboxRequestBuilder::ApplyEnvsAndLogs<runtime::v1::StartRequest>(
    const Envs &, const std::string &, runtime::v1::StartRequest *) const;
template void SandboxRequestBuilder::ApplyEnvsAndLogs<runtime::v1::RestoreRequest>(
    const Envs &, const std::string &, runtime::v1::RestoreRequest *) const;
template void SandboxRequestBuilder::ApplyExtraConfig<runtime::v1::StartRequest>(
    const std::shared_ptr<messages::StartInstanceRequest> &, runtime::v1::StartRequest *) const;
template void SandboxRequestBuilder::ApplyExtraConfig<runtime::v1::RestoreRequest>(
    const std::shared_ptr<messages::StartInstanceRequest> &, runtime::v1::RestoreRequest *) const;

}  // namespace functionsystem::runtime_manager
