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

#include "container_executor.h"

#include <google/protobuf/util/json_util.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cerrno>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_set>

#include "async/asyncafter.hpp"
#include "async/collect.hpp"
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/actor_worker.h"
#include "common/utils/collect_status.h"
#include "common/utils/exec_utils.h"
#include "common/utils/files.h"
#include "common/utils/generate_message.h"
#include "common/utils/path.h"
#include "common/utils/struct_transfer.h"
#include "config/build.h"
#include "port/port_manager.h"
#include "utils/os_utils.hpp"
#include "utils/utils.h"
#include "runtime_manager/ckpt/ckpt_file_manager_actor.h"
#include "common/constants/actor_name.h"

namespace functionsystem::runtime_manager {
using json = nlohmann::json;
const int64_t DEFAULT_GRACEFUL_SHUTDOWN = 5;
const int64_t RECONNECT_CONTAINERD_INTERVAL_MS = 5000;
const std::string PARAM_LANGUAGE = "language";
const std::string RUNTIME_LAYER_DIR_NAME = "layer";
const std::string RUNTIME_FUNC_DIR_NAME = "func";
const std::string PARAM_EXEC_PATH = "execPath";
const std::string PARAM_RUNTIME_ID = "runtimeID";
const std::string YR_ONLY_STDOUT = "YR_ONLY_STDOUT";
// used to chdir for runtime entrypoint
const std::string YR_RT_WORKING_DIR = "YR_RT_WORKING_DIR";

ContainerExecutor::ContainerExecutor(const std::string &name, const litebus::AID &functionAgentAID) : Executor(name)
{
    functionAgentAID_ = functionAgentAID;
    // Initialize checkpoint file manager
    auto ckptFileManagerActor = std::make_shared<CkptFileManagerActor>(
        name + "_CkptFileManager");
    litebus::Spawn(ckptFileManagerActor);
    ckptFileManager_ = std::make_shared<CkptFileManager>(ckptFileManagerActor);
}

void ContainerExecutor::InitConfig()
{
    cmdBuilder_.SetRuntimeConfig(config_);
    auto ep = litebus::os::GetEnv("CONTAINER_EP");
    if (ep.IsNone()) {
        YRLOG_INFO("container executor disabled, no containerd endpoint found");
        return;
    }
    std::string endpoint = ep.IsSome() ? ep.Get() : "127.0.0.1:8222";
    YRLOG_INFO("start container executor which bind containerd({})", endpoint);
    containerd_ = GrpcClient<runtime::v1::RuntimeLauncher>::CreateUdsGrpcClient(endpoint);
    synced_ = true;
    CheckConnectivity();
    YRLOG_INFO("success to start container executor which bind containerd({})", endpoint);
}

void ContainerExecutor::CheckConnectivity()
{
    litebus::AsyncAfter(RECONNECT_CONTAINERD_INTERVAL_MS, GetAID(), &ContainerExecutor::CheckConnectivity);
    if (containerd_->IsConnected()) {
        return;
    }
    if (reconnecting_) {
        return;
    }
    YRLOG_WARN("containerd is not connected, try to reconnect");
    ReconnectContainerd();
}

void ContainerExecutor::ReconnectContainerd()
{
    if (containerd_->IsConnected()) {
        reconnecting_ = false;
        return;
    }
    reconnecting_ = true;
    auto actor = std::make_shared<ActorWorker>();
    (void)actor
        ->AsyncWork([containerd(containerd_)]() {
            YRLOG_INFO("try to reconnect containerd...");
            containerd->CheckChannelAndWaitForReconnect(true);
        })
        .OnComplete([actor, aid(GetAID())](const litebus::Future<Status> &) {
            actor->Terminate();
            litebus::Async(aid, &ContainerExecutor::OnReconnectContainerd);
        });
}

void ContainerExecutor::OnReconnectContainerd()
{
    if (!containerd_->IsConnected()) {
        YRLOG_WARN("reconnect containerd failed, retry after {} ms", RECONNECT_CONTAINERD_INTERVAL_MS);
        litebus::AsyncAfter(RECONNECT_CONTAINERD_INTERVAL_MS, GetAID(), &ContainerExecutor::ReconnectContainerd);
        return;
    }
    reconnecting_ = false;
    YRLOG_INFO("reconnect containerd success");
}

void ContainerExecutor::Init()
{
    Sync();
}

void ContainerExecutor::Finalize()
{
    runtimeInstanceInfoMap_.clear();
    Executor::Finalize();
}

void ContainerExecutor::Sync()
{
    DoSyncRegistered().OnComplete([aid(GetAID())](const litebus::Future<Status> &result) {
        if (result.IsError() || result.Get().IsError()) {
            YRLOG_ERROR("sync registered failed, code {}. try to resync", result.GetErrorCode());
            litebus::AsyncAfter(RECONNECT_CONTAINERD_INTERVAL_MS, aid, &ContainerExecutor::Sync);
            return;
        }
        YRLOG_INFO("sync registered succeed.");
        litebus::Async(aid, &ContainerExecutor::OnSyncRegistered);
    });
}

void ContainerExecutor::OnSyncRegistered()
{
    synced_ = true;
}

litebus::Future<Status> ContainerExecutor::DoSyncRegistered()
{
    if (containerd_ == nullptr || !containerd_->IsConnected()) {
        YRLOG_ERROR("containerd client is not connected, can not sync registered runtimes");
        return Status(StatusCode::FAILED);
    }
    return GetRegisteredWarmUped().Then(
        [aid(GetAID())](const litebus::Future<runtime::v1::GetRegisteredResponse> &future) -> litebus::Future<Status> {
            if (future.IsError()) {
                YRLOG_ERROR("get registered warmuped runtimes failed, code {}", future.GetErrorCode());
                return Status(StatusCode::FAILED);
            }
            auto registeredRuntimes = future.Get();
            YRLOG_INFO("get {} registered warmuped runtimes, currently unregister all",
                       registeredRuntimes.funcruntimes_size());
            auto unReg = std::make_shared<runtime::v1::UnregisterRequest>();
            for (const auto &funcRuntime : registeredRuntimes.funcruntimes()) {
                *unReg->add_ids() = funcRuntime.id();
            }
            return litebus::Async(aid, &ContainerExecutor::DoUnregisterWarmUped, unReg)
                .Then(litebus::Defer(aid, &ContainerExecutor::OnUnregisteredWarmUped, unReg, std::placeholders::_1));
        });
}

litebus::Future<Status> ContainerExecutor::NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                               const int limit)
{
    return Status::OK();
}

litebus::Future<bool> ContainerExecutor::StopAllContainers()
{
    std::list<litebus::Future<Status>> futures;
    YRLOG_INFO("{} containers need to stop", runtime2containerID_.size());
    for (auto [runtimeID, containerID] : runtime2containerID_) {
        futures.emplace_back(TerminateContainer(runtimeID, "", containerID, false));
        YRLOG_INFO("stop runtime {} with container {}", runtimeID, containerID);
    }
    return CollectStatus(futures, "").Then([]() -> litebus::Future<bool> { return true; });
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::StartInstance(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs)
{
    const auto &info = request->runtimeinstanceinfo();
    std::string language = info.runtimeconfig().language();
    (void)transform(language.begin(), language.end(), language.begin(), ::tolower);
    std::string runtimeID = info.runtimeid();
    std::string port;
    auto tlsConfig = info.runtimeconfig().tlsconfig();
    RuntimeFeatures features;
    if (tlsConfig.enableservermode()) {
        port = tlsConfig.posixport();
        features.serverMode = false;
    }
    YRLOG_DEBUG("enableservermode = {}, port = {}", tlsConfig.enableservermode(), port);
    // if (CheckRuntimeCredential(request) != StatusCode::SUCCESS) {
    //     YRLOG_ERROR("{}|{}|CheckRuntimeCredential failed, instanceID({}), runtimeID({})", info.traceid(),
    //                 info.requestid(), info.instanceid(), runtimeID);
    //     return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_PARAMS_INVALID);
    // }
    std::vector<std::string> args;
    if (auto status = cmdBuilder_.GetBuildArgs(language, port, request, args); status.IsError()) {
        YRLOG_ERROR("{}|{}|get build args failed, can not start instanceID({}), runtimeID({})", info.traceid(),
                    info.requestid(), info.instanceid(), runtimeID);
        return GenFailStartInstanceResponse(request, status.StatusCode(), status.GetMessage());
    }
    YRLOG_INFO("{}|{}|advance to start instanceID({}) runtimeID({})", info.traceid(), info.requestid(),
               info.instanceid(), runtimeID);
    // todo lwy runtime directly call
    features.cleanStreamProducerEnable = config_.cleanStreamProducerEnable;
    return StartRuntime(request, language, port, GenerateEnvs(config_, request, port, cardIDs, features), args);
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::OnStartRuntime(
    const runtime::v1::StartResponse &response, const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &info = request->runtimeinstanceinfo();
    if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("{}|{}|failed to start container, code({}) message({})", info.traceid(), info.requestid(),
                    response.code(), response.message());
        // Release any port-forward ports allocated in StartByRuntimeID before the container start failed
        const auto &runtimeID = info.runtimeid();
        auto portMappingsIter = runtime2portMappings_.find(runtimeID);
        if (portMappingsIter != runtime2portMappings_.end()) {
            PortManager::GetInstance().ReleasePorts(runtimeID);
            runtime2portMappings_.erase(portMappingsIter);
        }
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_CREATE_EXEC_FAILED, response.message());
    }
    auto containerID = response.id();
    litebus::Async(GetAID(), &ContainerExecutor::ReportInfo, info.instanceid(), info.runtimeid(), containerID,
                   functionsystem::metrics::MeterTitle{ "yr_app_instance_start_time", " start timestamp", "ms" });
    YRLOG_INFO("{}|{}|start instance success, instanceID({}), runtimeID({}), containerID({}))", info.traceid(),
               info.requestid(), info.instanceid(), info.runtimeid(), containerID);
    runtime2containerID_[info.runtimeid()] = containerID;
    // pids_.insert(execPtr->GetPid());
    runtimeInstanceInfoMap_[info.runtimeid()] = request->runtimeinstanceinfo();
    return GenSuccessStartInstanceResponse(request, containerID);
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::StartRuntime(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &language,
    const std::string &port, const Envs &envs, const std::vector<std::string> &args)
{
    const auto &info = request->runtimeinstanceinfo();
    std::string execPath;
    if (!litebus::strings::StartsWithPrefix(language, PYTHON_LANGUAGE)) {
        // todo(ant)： adapter for different runtime
        execPath = cmdBuilder_.GetExecPathFromRuntimeConfig(info.runtimeconfig());
    }
    if (request->runtimeinstanceinfo().warmuptype() != static_cast<int32_t>(WarmupType::NONE)) {
        return WarmUp(request, { { PARAM_EXEC_PATH, execPath }, { PARAM_LANGUAGE, language } }, args, envs);
    }
    if (!request->runtimeinstanceinfo().snapshotinfo().checkpointid().empty()) {
        return StartBySnapshot(request, { { PARAM_EXEC_PATH, execPath }, { PARAM_LANGUAGE, language } }, args, envs);
    }
    return StartByRuntimeID(request, { { PARAM_EXEC_PATH, execPath }, { PARAM_LANGUAGE, language } }, args, envs)
        .Then(litebus::Defer(GetAID(), &ContainerExecutor::OnStartRuntime, std::placeholders::_1, request));

    // todo lwy tlsconfig should be passed by uds
    // Status result;
    // if (config_.isProtoMsgToRuntime) {
    //     result =
    //         WriteProtoToRuntime(request->runtimeinstanceinfo().requestid(),
    //         request->runtimeinstanceinfo().runtimeid(),
    //                             request->runtimeinstanceinfo().runtimeconfig().tlsconfig(), execPtr);
    // } else {
    //     result =
    //         WriteJsonToRuntime(request->runtimeinstanceinfo().requestid(),
    //         request->runtimeinstanceinfo().runtimeid(),
    //                            request->runtimeinstanceinfo().runtimeconfig().tlsconfig(), execPtr);
    // }
    // if (result.IsError()) {
    //     return GenFailStartInstanceResponse(request, result.StatusCode());
    // }
}

void ContainerExecutor::ReportInfo(const std::string &instanceID, const std::string runtimeID,
                                   const std::string &containerID, const functionsystem::metrics::MeterTitle &title)
{
    auto timeStamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    functionsystem::metrics::MeterData data{
        static_cast<double>(timeStamp),
        {
            { "instance_id", instanceID },
            { "node_id", config_.nodeID },
            { "ip", config_.ip },
            { "runtime_id", runtimeID },
            { "container_id", containerID },
        },
    };
    functionsystem::metrics::MetricsAdapter::GetInstance().ReportGauge(title, data);
}

void ContainerExecutor::ConfigRuntimeRedirectLog(std::string &stdOut, std::string &stdErr, const std::string &runtimeID)
{
    // Keep logs directly under parentPath as {runtimeID}.out / {runtimeID}.err
    auto parentPath = litebus::os::Join(config_.runtimeLogPath, config_.runtimeStdLogDir);
    if (!litebus::os::ExistPath(parentPath)) {
        YRLOG_WARN("std log dir {} not found, try to make dir", parentPath);
        if (!litebus::os::Mkdir(parentPath).IsNone()) {
            YRLOG_WARN("failed to make dir {}, msg: {}", parentPath, litebus::os::Strerror(errno));
            return;
        }
    }

    // Build file paths
    stdOut = litebus::os::Join(parentPath, fmt::format("{}.out", runtimeID));
    stdErr = litebus::os::Join(parentPath, fmt::format("{}.err", runtimeID));

    // Ensure files exist
    if (!litebus::os::ExistPath(stdOut) && TouchFile(stdOut) != 0) {
        YRLOG_WARN("create std out log file {} failed: {}", stdOut, litebus::os::Strerror(errno));
        return;
    }
    if (!litebus::os::ExistPath(stdErr) && TouchFile(stdErr) != 0) {
        YRLOG_WARN("create std err log file {} failed: {}", stdErr, litebus::os::Strerror(errno));
        return;
    }
}

std::string trimDash(const std::string &s)
{
    if (s.empty())
        return s;

    size_t start = s.find_first_not_of('-');
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of('-');

    return s.substr(start, end - start + 1);
}

std::string DirName(const std::string &path)
{
    if (path.empty()) {
        return "";
    }
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return "";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

// Forward declarations for functions used in helpers
std::vector<std::string> BuildCommand(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                      const std::string &execPath);
std::string BuildBootstrapWorkingRoot(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                     runtime::v1::Mount &mount);

// Unified helper to build runtime commands - works with FunctionRuntime directly
void ContainerExecutor::BuildRuntimeCommands(runtime::v1::FunctionRuntime *funcRt,
                                           const std::shared_ptr<messages::StartInstanceRequest> &request,
                                           const std::string &execPath,
                                           const std::vector<std::string> &buildArgs)
{
    auto commands = BuildCommand(request, execPath);
    for (const auto &cmd : commands) {
        *funcRt->add_command() = cmd;
    }
    for (const auto &arg : buildArgs) {
        *funcRt->add_command() = arg;
    }
}

// Unified helper to set request resources - works with protobuf map directly
void ContainerExecutor::SetRequestResources(google::protobuf::Map<std::string, double> *resourcesMap,
                                          const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &resources = request->runtimeinstanceinfo().runtimeconfig().resources();
    if (resources.resources().find(CPU_RESOURCE_NAME) == resources.resources().end()
        || resources.resources().at(CPU_RESOURCE_NAME).type() != ValueType::Value_Type_SCALAR) {
        (*resourcesMap)[CPU_RESOURCE_NAME] = 500;
    } else {
        (*resourcesMap)[CPU_RESOURCE_NAME] =
            resources.resources().at(CPU_RESOURCE_NAME).scalar().value();
    }
    if (resources.resources().find(MEMORY_RESOURCE_NAME) == resources.resources().end()
        || resources.resources().at(MEMORY_RESOURCE_NAME).type() != ValueType::Value_Type_SCALAR) {
        (*resourcesMap)[MEMORY_RESOURCE_NAME] = 500;
    } else {
        (*resourcesMap)[MEMORY_RESOURCE_NAME] =
            resources.resources().at(MEMORY_RESOURCE_NAME).scalar().value();
    }
}

void ContainerExecutor::SetRequestEnvsAndLogsForStart(runtime::v1::StartRequest *req,
                                                     const std::shared_ptr<messages::StartInstanceRequest> &request,
                                                     const Envs &envs,
                                                     const std::string &runtimeID)
{
    const std::map<std::string, std::string> combineEnvs = cmdBuilder_.CombineEnvs(envs);
    req->mutable_userenvs()->insert(combineEnvs.begin(), combineEnvs.end());
    (*req->mutable_userenvs())[YR_ONLY_STDOUT] = "true";
    std::string stdOut, stdErr;
    ConfigRuntimeRedirectLog(stdOut, stdErr, runtimeID);
    req->set_stdout(stdOut);
    req->set_stderr(stdErr);
}

void ContainerExecutor::SetRequestEnvsAndLogsForRestore(runtime::v1::RestoreRequest *req,
                                                       const std::shared_ptr<messages::StartInstanceRequest> &request,
                                                       const Envs &envs,
                                                       const std::string &runtimeID)
{
    const std::map<std::string, std::string> combineEnvs = cmdBuilder_.CombineEnvs(envs);
    req->mutable_userenvs()->insert(combineEnvs.begin(), combineEnvs.end());
    (*req->mutable_userenvs())[YR_ONLY_STDOUT] = "true";
    std::string stdOut, stdErr;
    ConfigRuntimeRedirectLog(stdOut, stdErr, runtimeID);
    req->set_stdout(stdOut);
    req->set_stderr(stdErr);
}

void ContainerExecutor::SetRequestExtraConfigForStart(runtime::v1::StartRequest *req,
                                                     const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    if (opts.find(CONTAINER_EXTRA_CONFIG) != opts.end()) {
        req->set_extraconfig(opts.at(CONTAINER_EXTRA_CONFIG));
    }
}

void ContainerExecutor::SetRequestExtraConfigForRestore(runtime::v1::RestoreRequest *req,
                                                       const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    if (opts.find(CONTAINER_EXTRA_CONFIG) != opts.end()) {
        req->set_extraconfig(opts.at(CONTAINER_EXTRA_CONFIG));
    }
}

Envs BuildMountForCode(const std::shared_ptr<runtime::v1::StartRequest> &start,
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
    code->set_source(workingDirIter->second);
    if (workingDirIter->second.find(".img") != std::string::npos) {
        code->set_type("erofs");
        funcPath = DirName(workingDirIter->second);
    }
    std::string funcPathTarget = funcPath;
    std::replace(funcPathTarget.begin(), funcPathTarget.end(), '/', '-');
    code->set_target(request->runtimeinstanceinfo().container().mountpoint());

    updateEnv.posixEnvs[UNZIPPED_WORKING_DIR] = code->target();
    updateEnv.posixEnvs["YR_FUNCTION_LIB_PATH"] = code->target();
    updateEnv.posixEnvs["FUNCTION_LIB_PATH"] = code->target();

    for (auto &layer : GenerateLayerPath(request->runtimeinstanceinfo())) {
        auto code = start->add_mounts();
        code->set_type("bind");
        code->set_source(layer);
        std::string target = layer;
        std::replace(target.begin(), target.end(), '/', '-');
        code->set_target(litebus::os::Join("/opt", target));
    }
    return updateEnv;
}

Status RootfsJsonParse(runtime::v1::FunctionRuntime &funcRt, const std::string &rootfsJson)
{
    try {
        nlohmann::json j = nlohmann::json::parse(rootfsJson);

        // Set runtime (sandbox)
        if (j.find("runtime") != j.end()) {
            funcRt.set_sandbox(j.at("runtime").get<std::string>());
        }

        // Set readonly
        if (j.find("readonly") != j.end()) {
            bool readonly = false;
            if (j.at("readonly").is_boolean()) {
                readonly = j.at("readonly").get<bool>();
            } else if (j.at("readonly").is_string()) {
                std::string readonlyStr = j.at("readonly").get<std::string>();
                readonly = (readonlyStr == "true" || readonlyStr == "1");
            }
            funcRt.mutable_rootfs()->set_readonly(readonly);
        }

        // Guard: early return if type is not present
        if (j.find("type") == j.end()) {
            return Status::OK();
        }

        std::string typeStr = j.at("type").get<std::string>();

        if (typeStr == "s3") {
            funcRt.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::S3);
            if (j.find("storageInfo") == j.end()) {
                return Status::OK();
            }

            nlohmann::json storage = j.at("storageInfo");
            auto s3Config = funcRt.mutable_rootfs()->mutable_s3_config();
            if (storage.find("endpoint") != storage.end()) {
                s3Config->set_endpoint(storage.at("endpoint").get<std::string>());
            }
            if (storage.find("bucket") != storage.end()) {
                s3Config->set_bucket(storage.at("bucket").get<std::string>());
            }
            if (storage.find("object") != storage.end()) {
                s3Config->set_object(storage.at("object").get<std::string>());
            }
            if (storage.find("accessKey") != storage.end()) {
                s3Config->set_accesskeyid(storage.at("accessKey").get<std::string>());
            }
            if (storage.find("secretKey") != storage.end()) {
                s3Config->set_accesskeysecret(storage.at("secretKey").get<std::string>());
            }
        } else if (typeStr == "image") {
            funcRt.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::IMAGE);
            if (j.find("imageurl") != j.end()) {
                funcRt.mutable_rootfs()->set_image_url(j.at("imageurl").get<std::string>());
            }
        } else if (typeStr == "local") {
            funcRt.mutable_rootfs()->set_type(runtime::v1::RootfsSrcType::LOCAL);
            if (j.find("path") != j.end()) {
                funcRt.mutable_rootfs()->set_path(j.at("path").get<std::string>());
            }
        }

    } catch (std::exception &e) {
        auto err = fmt::format("Failed to parse rootfs JSON: {}", std::string(e.what()));
        YRLOG_ERROR("{}", err);
        return Status(StatusCode::ERR_PARAM_INVALID, err);
    }
    return Status::OK();
}

bool HasCustomRootfs(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    return opts.find(CONTAINER_ROOTFS) != opts.end();
}

Status ContainerExecutor::BuildRootfs(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                      std::shared_ptr<runtime::v1::StartRequest> &start)
{
    // TODO(lwy): build rootfs if needed
    auto funcRt = start->mutable_funcruntime();
    const auto &opts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    if (opts.find(CONTAINER_ROOTFS) == opts.end()) {
        funcRt->set_id(request->runtimeinstanceinfo().container().id());
        funcRt->set_sandbox(request->runtimeinstanceinfo().container().runtime());
        *funcRt->mutable_rootfs() = request->runtimeinstanceinfo().container().rootfsconfig();
        return Status::OK();
    }
    // When rootfs is specified, do not use the function container ID to avoid using a registered pre-warmed seed with
    // inconsistent rootfs
    funcRt->set_id(request->runtimeinstanceinfo().runtimeid());
    return RootfsJsonParse(*funcRt, opts.at(CONTAINER_ROOTFS));
}


std::vector<std::string> BuildCommand(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                      const std::string &execPath)
{
    std::vector<std::string> commands;
    const auto &bootstrapConfig = request->runtimeinstanceinfo().bootstrapconfig();

    // // Add execPath
    // if (!execPath.empty()) {
    //     commands.push_back(execPath);
    // }

    // Add entrypoint from bootstrapConfig, split by spaces
    if (!bootstrapConfig.entrypoint().empty()) {
        std::istringstream iss(bootstrapConfig.entrypoint());
        std::string token;
        while (iss >> token) {
            commands.push_back(token);
        }
    }
    // Add cmd from bootstrapConfig, split by spaces
    if (!bootstrapConfig.cmd().empty()) {
        std::istringstream iss(bootstrapConfig.cmd());
        std::string token;
        while (iss >> token) {
            commands.push_back(token);
        }
    }

    return commands;
}

std::string BuildBootstrapWorkingRoot(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    runtime::v1::Mount &mount)
{
    std::string root = "/";
    const auto &bootstrapConfig = request->runtimeinstanceinfo().bootstrapconfig();

    // Only create mount and return cd commands if bootstrapConfig has valid type and root
    if (bootstrapConfig.type().empty() || bootstrapConfig.root().empty()) {
        return root;
    }
    // todo lwy which should be removed after warmup supports mount
    if (!HasCustomRootfs(request)) {
        YRLOG_WARN("custom rootfs is not specified, using default language working root config");
        return root;
    }
    const std::string mountDst = "/__yuanrong/";
    mount.set_source(bootstrapConfig.root());
    mount.set_target(mountDst);

    if (bootstrapConfig.type() == "erofs") {
        mount.set_type("erofs");
    } else if (bootstrapConfig.type() == "path") {
        mount.set_type("bind");
    } else {
        // For unknown types, default to bind
        mount.set_type("bind");
    }
    return mountDst;
}

std::vector<ContainerExecutor::PortForwardConfig> ContainerExecutor::ParseForwardPorts(const std::string &networkJson)
{
    std::vector<PortForwardConfig> configs;
    if (networkJson.empty()) {
        return configs;
    }
    try {
        auto j = json::parse(networkJson);
        if (!j.contains("portForwardings") || !j["portForwardings"].is_array()) {
            return configs;
        }
        for (const auto &item : j["portForwardings"]) {
            if (!item.is_object() || !item.contains("port") || !item["port"].is_number_integer()) {
                continue;
            }
            int p = item["port"].get<int>();
            if (p > 0 && p <= 65535) {
                PortForwardConfig config;
                config.containerPort = static_cast<uint32_t>(p);
                // Protocol is stored as lowercase (tcp/udp)
                if (item.contains("protocol") && item["protocol"].is_string()) {
                    std::string proto = item["protocol"].get<std::string>();
                    std::transform(proto.begin(), proto.end(), proto.begin(), ::tolower);
                    config.protocol = proto;
                } else {
                    config.protocol = "tcp";  // Default to TCP
                }
                configs.push_back(config);
            }
        }
    } catch (const std::exception &e) {
        YRLOG_WARN("ParseForwardPorts: failed to parse network json: {}, error: {}", networkJson, e.what());
    }
    return configs;
}

litebus::Future<runtime::v1::StartResponse> ContainerExecutor::StartByRuntimeID(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
    const Envs &envs)
{
    const auto &execPath = startRuntimeParams.at(PARAM_EXEC_PATH);
    auto language = startRuntimeParams.at(PARAM_LANGUAGE);
    const auto &runtimeID = request->runtimeinstanceinfo().runtimeid();
    std::string stdOut;
    std::string stdErr;
    ConfigRuntimeRedirectLog(stdOut, stdErr, runtimeID);
    std::string cmd = execPath;
    runtime::v1::StartResponse rsp{};
    // java has jvm args check so ignore here
    if (language.find(JAVA_LANGUAGE_PREFIX) == std::string::npos && !CheckIllegalChars(cmd)) {
        rsp.set_code(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
        rsp.set_message(fmt::format("invalid java cmd: {}", cmd));
        return rsp;
    }
    YRLOG_INFO("start {} runtime({}), execute final cmd: {}", language, runtimeID, cmd);
    auto start = std::make_shared<runtime::v1::StartRequest>();
    if (auto status = BuildRootfs(request, start); !status.IsOk()) {
        runtime::v1::StartResponse rsp{};
        rsp.set_code(static_cast<int32_t>(status.StatusCode()));
        rsp.set_message(status.RawMessage());
        return rsp;
    }

    SetRequestExtraConfigForStart(start.get(), request);
    // Port forwarding: allocate host ports for requested container ports
    const auto &deployOpts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    auto networkIter = deployOpts.find(CONTAINER_NETWORK);
    if (networkIter != deployOpts.end() && !networkIter->second.empty()) {
        const auto forwardConfigs = ParseForwardPorts(networkIter->second);
        if (!forwardConfigs.empty()) {
            auto hostPorts = PortManager::GetInstance().RequestPorts(runtimeID, static_cast<int>(forwardConfigs.size()));
            if (hostPorts.size() == forwardConfigs.size()) {
                // Format for runtime_launcher: "protocol:hostPort:containerPort" (e.g., "tcp:40001:8080")
                for (size_t i = 0; i < forwardConfigs.size(); ++i) {
                    std::string portMapping = forwardConfigs[i].protocol + ":" +
                                             std::to_string(hostPorts[i]) + ":" +
                                             std::to_string(forwardConfigs[i].containerPort);
                    start->add_ports(portMapping);
                }
                // Format for response: "protocol:hostPort:containerPort" JSON array
                json portJson = json::array();
                for (size_t i = 0; i < forwardConfigs.size(); ++i) {
                    portJson.push_back(forwardConfigs[i].protocol + ":" +
                                       std::to_string(hostPorts[i]) + ":" +
                                       std::to_string(forwardConfigs[i].containerPort));
                }
                runtime2portMappings_[runtimeID] = portJson.dump();
                YRLOG_INFO("{}|port forward: allocated mappings for runtime({}): {}", runtimeID, runtimeID,
                           runtime2portMappings_[runtimeID]);
            } else {
                YRLOG_WARN("{}|port forward: insufficient ports for runtime({}), requested {}", runtimeID, runtimeID,
                           forwardConfigs.size());
            }
        }
    }
     runtime::v1::Mount mount;
    auto workingRoot = BuildBootstrapWorkingRoot(request, mount);
    if (mount.source().length() > 0 && mount.target().length() > 0) {
        *start->add_mounts() = mount;
    }
    BuildRuntimeCommands(start->mutable_funcruntime(), request, execPath, buildArgs);
    (*start->mutable_funcruntime()->mutable_runtimeenvs())[YR_RT_WORKING_DIR] = workingRoot;

    auto updateEnv = BuildMountForCode(start, request, envs);
    SetRequestResources(start->mutable_resources(), request);
    SetRequestEnvsAndLogsForStart(start.get(), request, updateEnv, runtimeID);

    return DoStartContainer(request, start);
}

litebus::Future<Status> ContainerExecutor::StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                                        bool oomKilled)
{
    std::string runtimeID = request->runtimeid();
    std::string requestID = request->requestid();
    if (registeredWarmUp_.find(runtimeID) != registeredWarmUp_.end()) {
        return UnRegisteredWarmUped(runtimeID, requestID);
    }
    return StopInstanceByRuntimeID(runtimeID, requestID, oomKilled);
}

litebus::Future<Status> ContainerExecutor::UnRegisteredWarmUped(const std::string &runtimeID,
                                                                const std::string &requestID)
{
    auto unReg = std::make_shared<runtime::v1::UnregisterRequest>();
    *unReg->add_ids() = runtimeID;
    YRLOG_INFO("start to unregister Pre-warmed runtime({})", runtimeID);
    return DoUnregisterWarmUped(unReg).Then(
        litebus::Defer(GetAID(), &ContainerExecutor::OnUnregisteredWarmUped, unReg, std::placeholders::_1));
}

litebus::Future<messages::SnapshotRuntimeResponse> ContainerExecutor::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(request->requestid());

    const std::string &runtimeID = request->runtimeid();
    const std::string &containerID = request->containerid();
    const std::string &instanceID = request->instanceid();
    int32_t ttl = request->ttl();  // Extract TTL from request

    YRLOG_INFO("{}|snapshot runtime({}) container({}) with ttl: {}", request->requestid(), runtimeID, containerID, ttl);

    // Check if container exists
    auto container = runtime2containerID_.find(runtimeID);
    if (container == runtime2containerID_.end()) {
        YRLOG_ERROR("{}|runtime({}) not found in runtime2containerID map", request->requestid(), runtimeID);
        response.set_code(static_cast<int32_t>(StatusCode::RUNTIME_MANAGER_RUNTIME_NOT_FOUND));
        response.set_message(fmt::format("runtime {} not found", runtimeID));
        return response;
    }

    // Generate unique checkpoint ID
    std::string checkpointID = fmt::format("ckpt-{}-{}", instanceID,
        std::chrono::system_clock::now().time_since_epoch().count());

    std::string checkpointPath = fmt::format("/home/yuanrong/checkpoints/{}", checkpointID);

    // Call containerd to create checkpoint
    auto checkpointReq = std::make_shared<runtime::v1::CheckpointRequest>();
    checkpointReq->set_id(containerID);
    checkpointReq->set_ckpt_dir(checkpointPath);
    checkpointReq->set_timeout(30);  // Use default timeout of 30 seconds
    checkpointReq->set_compress(false);
    checkpointReq->set_trace_id(request->requestid());

    return DoCheckpoint(checkpointReq).Then(
        litebus::Defer(GetAID(), &ContainerExecutor::OnCheckpointCompleted, std::placeholders::_1,
                       request->requestid(), checkpointID, checkpointPath, runtimeID, ttl));
}

litebus::Future<messages::SnapshotRuntimeResponse> ContainerExecutor::OnCheckpointCompleted(
    const runtime::v1::CheckpointResponse &ckptResponse,
    const std::string &requestID,
    const std::string &checkpointID,
    const std::string &checkpointPath,
    const std::string &runtimeID,
    int32_t ttl)
{
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(requestID);

    if (!ckptResponse.success()) {
        YRLOG_ERROR("{}|checkpoint failed: {}", requestID, ckptResponse.message());
        response.set_code(static_cast<int32_t>(StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED));
        response.set_message(ckptResponse.message());
        return response;
    }

    YRLOG_INFO("{}|checkpoint created successfully, uploading to storage: {} with ttl: {}", requestID, checkpointID, ttl);

    // Register checkpoint (upload and register in ckptFileManager)
    ASSERT_IF_NULL(ckptFileManager_);
    return ckptFileManager_->RegisterCheckpoint(checkpointID, checkpointPath, checkpointID, ttl)
        .Then(litebus::Defer(GetAID(), &ContainerExecutor::OnRegisterCheckpoint,
                                std::placeholders::_1, response, requestID, checkpointID, runtimeID, ttl));
}

litebus::Future<messages::SnapshotRuntimeResponse> ContainerExecutor::OnRegisterCheckpoint(
    const std::string &storageUrl,
    messages::SnapshotRuntimeResponse response,
    const std::string &requestID,
    const std::string &checkpointID,
    const std::string &runtimeID,
    int32_t ttl)
{
    auto *snapshotInfo = response.mutable_snapshotinfo();
    snapshotInfo->set_checkpointid(checkpointID);
    snapshotInfo->set_storage(storageUrl);
    snapshotInfo->set_ttlseconds(ttl);  // Set TTL in response
    // Track checkpoint for this runtime
    runtime2checkpointID_[runtimeID] = checkpointID;

    YRLOG_INFO("{}|snapshot runtime {} completed successfully with checkpoint {}, storageUrl: {}, ttl: {}",
               requestID, runtimeID, checkpointID, storageUrl, ttl);
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("snapshot created successfully");
    return response;
}

litebus::Future<runtime::v1::CheckpointResponse> ContainerExecutor::DoCheckpoint(
    const std::shared_ptr<runtime::v1::CheckpointRequest> &req)
{
    YRLOG_INFO("{}|DoCheckpoint for container {} to {}", req->trace_id(), req->id(), req->ckpt_dir());
    ASSERT_IF_NULL(containerd_);

    auto response = std::make_shared<runtime::v1::CheckpointResponse>();
    return containerd_
        ->CallAsyncX("Checkpoint", *req.get(), response.get(),
                     &runtime::v1::RuntimeLauncher::Stub::AsyncCheckpoint)
        .Then([req, response](const Status &status) -> litebus::Future<runtime::v1::CheckpointResponse> {
            if (status.IsOk()) {
                return *response;
            }
            auto msg = fmt::format("failed to checkpoint container {}, grpc err: {}",
                                   req->id(), status.RawMessage());
            YRLOG_ERROR("{}|{}", req->trace_id(), msg);
            runtime::v1::CheckpointResponse checkpointRsp;
            checkpointRsp.set_success(false);
            checkpointRsp.set_message(msg);
            return checkpointRsp;
        });
}

litebus::Future<runtime::v1::RestoreResponse> ContainerExecutor::DoRestore(
    const std::shared_ptr<runtime::v1::RestoreRequest> &req)
{
    YRLOG_INFO("{}|DoRestore from checkpoint {} for runtime {}", req->trace_id(),
               req->ckpt_dir(), req->funcruntime().id());
    ASSERT_IF_NULL(containerd_);

    auto response = std::make_shared<runtime::v1::RestoreResponse>();
    return containerd_
        ->CallAsyncX("Restore", *req.get(), response.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncRestore)
        .Then([req, response](const Status &status) -> litebus::Future<runtime::v1::RestoreResponse> {
            if (status.IsOk()) {
                return *response;
            }
            auto msg = fmt::format("failed to restore from checkpoint {} for runtime {}, grpc err: {}",
                                   req->ckpt_dir(), req->funcruntime().id(), status.RawMessage());
            YRLOG_ERROR("{}|{}", req->trace_id(), msg);
            runtime::v1::RestoreResponse restoreRsp;
            restoreRsp.set_code(static_cast<int32_t>(status.StatusCode()));
            restoreRsp.set_message(msg);
            return restoreRsp;
        });
}

litebus::Future<Status> ContainerExecutor::OnUnregisteredWarmUped(
    const std::shared_ptr<runtime::v1::UnregisterRequest> &unReg, const runtime::v1::NormalResponse &response)
{
    if (!response.success()) {
        YRLOG_ERROR("failed to unRegister Pre-warmed runtime({})",
                    fmt::join(unReg->ids().begin(), unReg->ids().end(), ","));
        return Status(StatusCode::RUNTIME_MANAGER_WARMUP_FAILURE);
    }
    for (const auto &id : unReg->ids()) {
        registeredWarmUp_.erase(id);
    }
    YRLOG_INFO("success to unregister Pre-warmed runtime({})",
               fmt::join(unReg->ids().begin(), unReg->ids().end(), ","));
    return Status::OK();
}

litebus::Future<Status> ContainerExecutor::StopInstanceByRuntimeID(const std::string &runtimeID,
                                                                   const std::string &requestID, bool oomKilled)
{
    auto container = runtime2containerID_.find(runtimeID);
    if (container == runtime2containerID_.end()) {
        if (innerOomKilledruntimes_.find(runtimeID) != innerOomKilledruntimes_.end()) {
            YRLOG_DEBUG("{}|runtime({}) already deleted by oomMonitor.", requestID, runtimeID);
            innerOomKilledruntimes_.erase(runtimeID);
            return Status::OK();  // for adapting instance exit clean logic in function_proxy
        }
        YRLOG_WARN("{}|can not find pid to stop runtime({}).", requestID, runtimeID);
        return Status::OK();
    }
    return TerminateContainer(runtimeID, requestID, container->second, oomKilled);
}

litebus::Future<Status> ContainerExecutor::OnDeleteContainer(const std::string &instanceID,
                                                             const std::string &runtimeID, const std::string &requestID,
                                                             const std::string &containerID)
{
    YRLOG_INFO("{}|finished delete container({}) for instance({}) runtime({})", requestID, containerID, instanceID,
               runtimeID);

    auto infoIter = runtimeInstanceInfoMap_.find(runtimeID);
    if (infoIter != runtimeInstanceInfoMap_.end()) {
        runtimeInstanceInfoMap_.erase(runtimeID);
    }

    // Remove checkpoint reference when container is deleted
    auto ckptIter = runtime2checkpointID_.find(runtimeID);
    if (ckptIter != runtime2checkpointID_.end()) {
        std::string checkpointID = ckptIter->second;
        if (ckptFileManager_ != nullptr) {
            ckptFileManager_->RemoveReference(checkpointID).Then(
                [checkpointID, requestID, runtimeID](const litebus::Future<Status> &result) -> Status {
                    if (result.IsError() || result.Get().IsError()) {
                        YRLOG_WARN("{}|failed to remove reference for checkpoint {} (runtime: {})",
                                   requestID, checkpointID, runtimeID);
                    } else {
                        YRLOG_INFO("{}|removed reference for checkpoint {} (runtime: {})",
                                   requestID, checkpointID, runtimeID);
                    }
                    return Status::OK();
                });
        }
        runtime2checkpointID_.erase(ckptIter);
    }

    functionsystem::metrics::MeterTitle title{ "yr_instance_stop_time", "stop timestamp", "num" };
    litebus::Async(GetAID(), &ContainerExecutor::ReportInfo, instanceID, runtimeID, containerID, title);
    (void)runtime2containerID_.erase(runtimeID);
    // Release forwarded ports if any were allocated for this runtime
    auto portMappingsIter = runtime2portMappings_.find(runtimeID);
    if (portMappingsIter != runtime2portMappings_.end()) {
        PortManager::GetInstance().ReleasePorts(runtimeID);
        (void)runtime2portMappings_.erase(portMappingsIter);
    }
    // if (oomKilled) {
    //     innerOomKilledruntimes_.insert(runtimeID);
    // }
    return Status::OK();
}

litebus::Future<Status> ContainerExecutor::TerminateContainer(const std::string &runtimeID,
                                                              const std::string &requestID,
                                                              const std::string &containerID, bool force)
{
    auto infoIter = runtimeInstanceInfoMap_.find(runtimeID);
    std::string instanceID = "";
    int64_t timeout = DEFAULT_GRACEFUL_SHUTDOWN;
    if (infoIter != runtimeInstanceInfoMap_.end()) {
        instanceID = infoIter->second.instanceid();
        timeout = infoIter->second.gracefulshutdowntime();
    }
    auto del = std::make_shared<runtime::v1::DeleteRequest>();
    del->set_id(containerID);
    del->set_timeout(force ? 0 : timeout);
    YRLOG_INFO("{}|terminate container({}) of instance({}) runtime({}).", requestID, containerID, instanceID,
               runtimeID);
    return DoDeleteContainer(instanceID, runtimeID, requestID, del)
        .Then(litebus::Defer(GetAID(), &ContainerExecutor::OnDeleteContainer, instanceID, runtimeID, requestID,
                             containerID));
}

std::map<std::string, messages::RuntimeInstanceInfo> ContainerExecutor::GetRuntimeInstanceInfos()
{
    return runtimeInstanceInfoMap_;
}

messages::StartInstanceResponse ContainerExecutor::GenSuccessStartInstanceResponse(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &containerID)
{
    messages::StartInstanceResponse response;
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("start instance success");
    response.set_requestid(request->runtimeinstanceinfo().requestid());

    auto instanceResponse = response.mutable_startruntimeinstanceresponse();
    instanceResponse->set_runtimeid(request->runtimeinstanceinfo().runtimeid());
    instanceResponse->set_containerid(containerID);
    YRLOG_DEBUG("{}|{}|instance({}) runtime({}) with container({})", request->runtimeinstanceinfo().traceid(),
                request->runtimeinstanceinfo().requestid(), request->runtimeinstanceinfo().instanceid(),
                request->runtimeinstanceinfo().runtimeid(), containerID);
    // set to be zero
    instanceResponse->set_pid(0);
    // Set port forward mappings if allocated (JSON string e.g. {"8888":"40001","443":"40002"})
    const auto &runtimeID = request->runtimeinstanceinfo().runtimeid();
    auto portMappingsIter = runtime2portMappings_.find(runtimeID);
    if (portMappingsIter != runtime2portMappings_.end()) {
        instanceResponse->set_port(portMappingsIter->second);
    }
    return response;
}

litebus::Future<messages::UpdateCredResponse> ContainerExecutor::UpdateCredForRuntime(
    const std::shared_ptr<messages::UpdateCredRequest> &request)
{
    auto requestID = request->requestid();
    auto runtimeID = request->runtimeid();
    auto token = request->token();
    auto salt = request->salt();

    messages::UpdateCredResponse response;
    response.set_requestid(requestID);
    if (runtime2containerID_.find(runtimeID) == runtime2containerID_.end()) {
        YRLOG_WARN("{}|{}|runtime has already been killed.", requestID, runtimeID);
        response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
        return response;
    }
    std::string containerID;
    ::messages::TLSConfig tlsConfig;
    auto infoIter = runtimeInstanceInfoMap_.find(runtimeID);
    if (infoIter != runtimeInstanceInfoMap_.end()) {
        tlsConfig = infoIter->second.runtimeconfig().tlsconfig();
    }
    tlsConfig.set_salt(request->salt());
    tlsConfig.set_token(request->token());
    tlsConfig.mutable_tenantcredentials()->CopyFrom(request->tenantcredentials());
    // todo: lwy update by uds
    // Status result = config_.isProtoMsgToRuntime ? WriteProtoToRuntime(requestID, runtimeID, tlsConfig, execPtr)
    //                                             : WriteJsonToRuntime(requestID, runtimeID, tlsConfig, execPtr);
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    return response;
}

 bool ContainerExecutor::IsRuntimeActive(const std::string &runtimeID)
{
    return runtime2containerID_.find(runtimeID) != runtime2containerID_.end();
}

litebus::Future<runtime::v1::StartResponse> ContainerExecutor::DoStartContainer(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::shared_ptr<runtime::v1::StartRequest> &start)
{
    YRLOG_INFO("debug:: {}|{}|{} {} DoStartContainer meg: {}", request->runtimeinstanceinfo().traceid(),
               request->runtimeinstanceinfo().requestid(), request->runtimeinstanceinfo().runtimeid(),
               request->runtimeinstanceinfo().instanceid(), start->ShortDebugString());
    ASSERT_IF_NULL(containerd_);
    auto response = std::make_shared<runtime::v1::StartResponse>();
    return containerd_
        ->CallAsyncX("Start", *start.get(), response.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncStart)
        .Then([aid(GetAID()), response, start,
               request](const Status &status) -> litebus::Future<runtime::v1::StartResponse> {
            if (status.IsOk()) {
                return *response;
            }
            auto msg = fmt::format("failed to start container {} for runtime({}) instance({}), grpc err: {}",
                                   start->funcruntime().sandbox(), request->runtimeinstanceinfo().runtimeid(),
                                   request->runtimeinstanceinfo().instanceid(), status.RawMessage());
            YRLOG_ERROR("{}|{}|{}", request->runtimeinstanceinfo().traceid(),
                        request->runtimeinstanceinfo().requestid(), msg);
            runtime::v1::StartResponse startRsp{};
            startRsp.set_code(static_cast<int32_t>(status.StatusCode()));
            startRsp.set_message(msg);
            return startRsp;
        });
}
litebus::Future<runtime::v1::DeleteResponse> ContainerExecutor::DoDeleteContainer(
    const std::string &instanceID, const std::string &runtimeID, const std::string &requestID,
    const std::shared_ptr<runtime::v1::DeleteRequest> &req)
{
    YRLOG_INFO("{}|{}|{} DoDeleteContainer meg: {}", requestID, instanceID, runtimeID, req->ShortDebugString());
    ASSERT_IF_NULL(containerd_);
    return containerd_
        ->CallAsync("Delete", *req.get(), static_cast<runtime::v1::DeleteResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncDelete)
        .Then([aid(GetAID()), req, runtimeID, requestID](
                  litebus::Try<runtime::v1::DeleteResponse> rsp) -> litebus::Future<runtime::v1::DeleteResponse> {
            if (rsp.IsOK()) {
                return rsp.Get();
            }
            auto msg = fmt::format("failed to delete container {} for runtime({}), grpc err: {}", req->id(), runtimeID,
                                   rsp.GetErrorCode());
            YRLOG_ERROR("{}|{}", requestID, msg);
            return runtime::v1::DeleteResponse{};
        });
}
litebus::Future<runtime::v1::WaitResponse> ContainerExecutor::DoWaitContainer(
    const std::shared_ptr<runtime::v1::WaitRequest> &req)
{
    ASSERT_IF_NULL(containerd_);
    return containerd_
        ->CallAsync("Wait", *req.get(), static_cast<runtime::v1::WaitResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncWait)
        .Then([req](litebus::Try<runtime::v1::WaitResponse> rsp) -> litebus::Future<runtime::v1::WaitResponse> {
            if (rsp.IsOK()) {
                return rsp.Get();
            }
            auto msg = fmt::format("failed to wait container {}, grpc err: {}", req->id(), rsp.GetErrorCode());
            YRLOG_ERROR("{}", msg);
            runtime::v1::WaitResponse wait{};
            wait.set_status(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
            wait.set_message(msg);
            return wait;
        });
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::WarmUp(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
    const Envs &envs)
{
    const auto &execPath = startRuntimeParams.at(PARAM_EXEC_PATH);
    auto language = startRuntimeParams.at(PARAM_LANGUAGE);
    const auto &runtimeID = request->runtimeinstanceinfo().runtimeid();
    const std::map<std::string, std::string> combineEnvs = cmdBuilder_.CombineEnvs(envs);
    std::string cmd = execPath;
    // java has jvm args check so ignore here
    if (language.find(JAVA_LANGUAGE_PREFIX) == std::string::npos && !CheckIllegalChars(cmd)) {
        return GenFailStartInstanceResponse(request, ERR_PARAM_INVALID, fmt::format("invalid java cmd: {}", cmd));
    }
    YRLOG_INFO("warm up {} ({}), execute final cmd: {}", language, request->runtimeinstanceinfo().instanceid(), cmd);
    auto registerReq = std::make_shared<runtime::v1::RegisterRequest>();
    // currently only one register langruntime
    auto warmup = registerReq->add_funcruntimes();
    warmup->set_id(runtimeID);
    warmup->set_sandbox(request->runtimeinstanceinfo().container().runtime());
    *warmup->mutable_rootfs() = request->runtimeinstanceinfo().container().rootfsconfig();
    warmup->set_makeseed(request->runtimeinstanceinfo().warmuptype() == static_cast<int32_t>(WarmupType::SEED));

    // Build language mount and get working root
    runtime::v1::Mount mount;
    auto workingRoot = BuildBootstrapWorkingRoot(request, mount);
    (*warmup->mutable_runtimeenvs())[YR_RT_WORKING_DIR] = workingRoot;

    // Build command from bootstrapconfig and execPath
    auto commands = BuildCommand(request, execPath);
    for (const auto &cmd : commands) {
        *warmup->add_command() = cmd;
    }
    // Also add buildArgs if provided
    for (const auto &arg : buildArgs) {
        *warmup->add_command() = arg;
    }

    // currently all treated as runtimeEnv
    // todo lwy for fork friendly, the immutable env should be mv to runtimeEnv
    warmup->mutable_runtimeenvs()->insert(combineEnvs.begin(), combineEnvs.end());
    if (auto env = litebus::os::GetEnv("YR_ENV_FILE"); env.IsSome()) {
        (*warmup->mutable_runtimeenvs())["YR_ENV_FILE"] = env.Get();
    }
    if (auto ready = litebus::os::GetEnv("YR_SEED_FILE"); ready.IsSome()) {
        (*warmup->mutable_runtimeenvs())["YR_SEED_FILE"] = ready.Get();
    }
    (*warmup->mutable_runtimeenvs())[YR_ONLY_STDOUT] = "true";
    return DoRegisterToWarmUp(registerReq)
        .Then(litebus::Defer(GetAID(), &ContainerExecutor::OnRegisterToWarmUp, std::placeholders::_1, request,
                             registerReq));
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::OnRegisterToWarmUp(
    const runtime::v1::NormalResponse &response, const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::shared_ptr<runtime::v1::RegisterRequest> &reg)
{
    if (!response.success()) {
        return GenFailStartInstanceResponse(
            request, RUNTIME_MANAGER_WARMUP_FAILURE,
            fmt::format("failed to register warmup runtime ({}), message:{}",
                        request->runtimeinstanceinfo().instanceid(), response.message()));
    }
    const auto &runtimes = reg->funcruntimes();
    for (const auto &warmuped : runtimes) {
        registeredWarmUp_[warmuped.id()] = warmuped;
    }
    messages::StartInstanceResponse rsp;
    rsp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    rsp.set_requestid(request->runtimeinstanceinfo().requestid());
    auto instanceResponse = rsp.mutable_startruntimeinstanceresponse();
    instanceResponse->set_runtimeid(request->runtimeinstanceinfo().runtimeid());
    YRLOG_DEBUG("{}|{}|success to warmup({}) runtime({})", request->runtimeinstanceinfo().traceid(),
                request->runtimeinstanceinfo().requestid(), request->runtimeinstanceinfo().instanceid(),
                request->runtimeinstanceinfo().runtimeid());
    return rsp;
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::StartBySnapshot(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
    const Envs &envs)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &snapshotInfo = info.snapshotinfo();
    const auto &runtimeID = info.runtimeid();

    const std::string checkpointID = snapshotInfo.checkpointid();
    const std::string storageUrl = snapshotInfo.storage();

    YRLOG_INFO("{}|{}|start instance from snapshot, instanceID({}), runtimeID({}), checkpointID({})",
               info.traceid(), info.requestid(), info.instanceid(), runtimeID, checkpointID);

    // Download checkpoint and increase reference count
    ASSERT_IF_NULL(ckptFileManager_);
    return ckptFileManager_->DownloadCheckpoint(checkpointID, storageUrl)
        .Then(litebus::Defer(GetAID(), &ContainerExecutor::OnDownloadCheckpointForRestore,
                             std::placeholders::_1, request, startRuntimeParams, buildArgs, envs));
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::OnDownloadCheckpointForRestore(
    const std::string &checkpointPath,
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
    const Envs &envs)
{
    const auto &info = request->runtimeinstanceinfo();
    const std::string checkpointID = info.snapshotinfo().checkpointid();

    YRLOG_INFO("{}|{}|checkpoint downloaded to {}, adding reference and starting restore...",
               info.traceid(), info.requestid(), checkpointPath);

    // Add reference count for this checkpoint
    return ckptFileManager_->AddReference(checkpointID)
        .Then(litebus::Defer(GetAID(), &ContainerExecutor::OnAddReferenceForRestore,
                             std::placeholders::_1, checkpointPath, checkpointID, request,
                             startRuntimeParams, buildArgs, envs));
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::OnAddReferenceForRestore(
    const Status &refStatus,
    const std::string &checkpointPath,
    const std::string &checkpointID,
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::map<std::string, std::string> startRuntimeParams,
    const std::vector<std::string> &buildArgs,
    const Envs &envs)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();
    const auto &execPath = startRuntimeParams.at(PARAM_EXEC_PATH);
    auto language = startRuntimeParams.at(PARAM_LANGUAGE);

    if (refStatus.IsError()) {
        YRLOG_ERROR("{}|{}|failed to add reference for checkpoint {}: {}",
                    info.traceid(), info.requestid(), checkpointID, refStatus.RawMessage());
        return GenFailStartInstanceResponse(request, StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED,
                                           "Failed to add checkpoint reference: " + refStatus.RawMessage());
    }

    // Track checkpoint reference for this runtime
    runtime2checkpointID_[runtimeID] = checkpointID;

    YRLOG_INFO("{}|{}|building restore request for runtime({})",
               info.traceid(), info.requestid(), runtimeID);

    // Build restore request
    auto restoreReq = std::make_shared<runtime::v1::RestoreRequest>();
    restoreReq->set_ckpt_dir(checkpointPath);
    restoreReq->set_trace_id(info.requestid());

    // Configure function runtime
    auto funcRt = restoreReq->mutable_funcruntime();
    funcRt->set_id(runtimeID);
    funcRt->set_sandbox(request->runtimeinstanceinfo().container().runtime());
    *funcRt->mutable_rootfs() = request->runtimeinstanceinfo().container().rootfsconfig();

    SetRequestExtraConfigForRestore(restoreReq.get(), request);
    runtime::v1::Mount mount;
    auto workingRoot = BuildBootstrapWorkingRoot(request, mount);
    if (mount.source().length() > 0 && mount.target().length() > 0) {
        *restoreReq->add_mounts() = mount;
    }
    BuildRuntimeCommands(restoreReq->mutable_funcruntime(), request, execPath, buildArgs);
    (*restoreReq->mutable_funcruntime()->mutable_runtimeenvs())[YR_RT_WORKING_DIR] = workingRoot;

    // Build mounts for code - need to create temporary StartRequest for this
    auto tempStart = std::make_shared<runtime::v1::StartRequest>();
    auto updateEnv = BuildMountForCode(tempStart, request, envs);
    // Copy mounts from tempStart to restoreReq
    for (const auto &mount : tempStart->mounts()) {
        *restoreReq->add_mounts() = mount;
    }

    SetRequestResources(restoreReq->mutable_resources(), request);
    SetRequestEnvsAndLogsForRestore(restoreReq.get(), request, updateEnv, runtimeID);

    YRLOG_INFO("{}|{}|calling DoRestore for runtime({}), checkpoint({})",
               info.traceid(), info.requestid(), runtimeID, checkpointPath);

    // Call restore
    return DoRestore(restoreReq).Then(
        litebus::Defer(GetAID(), &ContainerExecutor::OnRestoreCompleted,
                       std::placeholders::_1, request));
}

litebus::Future<messages::StartInstanceResponse> ContainerExecutor::OnRestoreCompleted(
    const runtime::v1::RestoreResponse &response,
    const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    const auto &info = request->runtimeinstanceinfo();

    if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("{}|{}|failed to restore from snapshot, code({}) message({})",
                    info.traceid(), info.requestid(), response.code(), response.message());
        return GenFailStartInstanceResponse(request, StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED, response.message());
    }

    auto containerID = response.id();
    litebus::Async(GetAID(), &ContainerExecutor::ReportInfo, info.instanceid(), info.runtimeid(), containerID,
                   functionsystem::metrics::MeterTitle{ "yr_app_instance_start_time", " start timestamp", "ms" });

    YRLOG_INFO("{}|{}|restore instance success, instanceID({}), runtimeID({}), containerID({})",
               info.traceid(), info.requestid(), info.instanceid(), info.runtimeid(), containerID);

    runtime2containerID_[info.runtimeid()] = containerID;
    runtimeInstanceInfoMap_[info.runtimeid()] = request->runtimeinstanceinfo();

    return GenSuccessStartInstanceResponse(request, containerID);
}

litebus::Future<runtime::v1::NormalResponse> ContainerExecutor::DoRegisterToWarmUp(
    const std::shared_ptr<runtime::v1::RegisterRequest> &reg)
{
    if (!synced_) {
        std::string msg = "sync not completed yet, can not warm up container now";
        YRLOG_WARN("{}", msg);
        runtime::v1::NormalResponse normal{};
        normal.set_success(false);
        normal.set_message(msg);
        return normal;
    }
    YRLOG_DEBUG("debug:: {}", reg->ShortDebugString());
    ASSERT_IF_NULL(containerd_);
    return containerd_
        ->CallAsync("Register", *reg.get(), static_cast<runtime::v1::NormalResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncRegister)
        .Then([reg](litebus::Try<runtime::v1::NormalResponse> rsp) -> litebus::Future<runtime::v1::NormalResponse> {
            if (rsp.IsOK()) {
                return rsp.Get();
            }
            runtime::v1::NormalResponse normal{};
            if (reg->funcruntimes_size() == 0) {
                return normal;
            }
            auto msg = fmt::format("failed to warm up container {}, grpc err: {}", reg->funcruntimes(0).id(),
                                   rsp.GetErrorCode());
            YRLOG_ERROR("{}", msg);
            normal.set_success(false);
            normal.set_message(msg);
            return normal;
        });
}

litebus::Future<runtime::v1::NormalResponse> ContainerExecutor::DoUnregisterWarmUped(
    const std::shared_ptr<runtime::v1::UnregisterRequest> &unReg)
{
    ASSERT_IF_NULL(containerd_);
    return containerd_
        ->CallAsync("Unregister", *unReg.get(), static_cast<runtime::v1::NormalResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncUnregister)
        .Then([unReg](litebus::Try<runtime::v1::NormalResponse> rsp) -> litebus::Future<runtime::v1::NormalResponse> {
            if (rsp.IsOK()) {
                return rsp.Get();
            }
            runtime::v1::NormalResponse normal{};
            if (unReg->ids_size() != 0) {
                return normal;
            }
            auto msg = fmt::format("failed to unregister container ({}), grpc err: {}",
                                   fmt::join(unReg->ids().begin(), unReg->ids().end(), ","), rsp.GetErrorCode());
            YRLOG_ERROR("{}", msg);
            normal.set_success(false);
            normal.set_message(msg);
            return normal;
        });
}

litebus::Future<runtime::v1::GetRegisteredResponse> ContainerExecutor::GetRegisteredWarmUped()
{
    ASSERT_IF_NULL(containerd_);
    return containerd_
        ->CallAsync("GetRegistered", runtime::v1::GetRegisteredRequest{},
                    static_cast<runtime::v1::GetRegisteredResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncGetRegistered)
        .Then([](litebus::Try<runtime::v1::GetRegisteredResponse> rsp)
                  -> litebus::Future<runtime::v1::GetRegisteredResponse> {
            if (rsp.IsOK()) {
                return rsp.Get();
            }
            litebus::Promise<runtime::v1::GetRegisteredResponse> promise;
            promise.SetFailed(rsp.GetErrorCode());
            return promise.GetFuture();
        });
}

litebus::Future<::messages::StartInstanceResponse> ContainerExecutorProxy::StartInstance(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs)
{
    return litebus::Async(executor_->GetAID(), &ContainerExecutor::StartInstance, request, cardIDs);
}

litebus::Future<Status> ContainerExecutorProxy::StopInstance(
    const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled)
{
    return litebus::Async(executor_->GetAID(), &ContainerExecutor::StopInstance, request, oomKilled);
}

litebus::Future<messages::SnapshotRuntimeResponse> ContainerExecutorProxy::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    return litebus::Async(executor_->GetAID(), &ContainerExecutor::SnapshotRuntime, request);
}

litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> ContainerExecutorProxy::GetRuntimeInstanceInfos()
{
    return litebus::Async(executor_->GetAID(), &ContainerExecutor::GetRuntimeInstanceInfos);
}

}  // namespace functionsystem::runtime_manager