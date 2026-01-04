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

namespace functionsystem::runtime_manager {
using json = nlohmann::json;
const int64_t DEFAULT_GRACEFUL_SHUTDOWN = 5;
const int64_t RECONNECT_CONTAINERD_INTERVAL_MS = 5000;
const std::string PARAM_LANGUAGE = "language";
const std::string RUNTIME_LAYER_DIR_NAME = "layer";
const std::string RUNTIME_FUNC_DIR_NAME = "func";
const std::string PARAM_EXEC_PATH = "execPath";
const std::string PARAM_RUNTIME_ID = "runtimeID";

ContainerExecutor::ContainerExecutor(const std::string &name, const litebus::AID &functionAgentAID) : Executor(name)
{
    functionAgentAID_ = functionAgentAID;
}

void ContainerExecutor::InitConfig()
{
    cmdBuilder_.SetRuntimeConfig(config_);
    auto ep = litebus::os::GetEnv("CONTAINER_EP");
    std::string endpoint = ep.IsSome() ? ep.Get() : "127.0.0.1:8222";
    YRLOG_INFO("start container executor which bind containerd({})", endpoint);
    containerd_ = GrpcClient<runtime::v1::RuntimeLauncher>::CreateUdsGrpcClient(endpoint);
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
}

void ContainerExecutor::Finalize()
{
    runtimeInstanceInfoMap_.clear();
    Executor::Finalize();
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
    if (litebus::strings::StartsWithPrefix(language, PYTHON_LANGUAGE)) {
        execPath = language;
    } else {
        execPath = cmdBuilder_.GetExecPathFromRuntimeConfig(info.runtimeconfig());
    }
    YRLOG_DEBUG("{}|{}|language({}) executor path: {}", info.traceid(), info.requestid(), language, execPath);
    if (execPath.empty()) {
        YRLOG_ERROR("{}|{}|execPath is not found, start instanceID({}) failed, runtimeID({})", info.traceid(),
                    info.requestid(), info.instanceid(), info.runtimeid());
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_EXEC_PATH_NOT_FOUND,
                                            "Executable path of " + language + " is not found");
    }
    if (request->runtimeinstanceinfo().warmuptype() != static_cast<int32_t>(WarmupType::NONE)) {
        return WarmUp(request, { { PARAM_EXEC_PATH, execPath }, { PARAM_LANGUAGE, language } }, args, envs);
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
    auto parentPath = litebus::os::Join(config_.runtimeLogPath, config_.runtimeStdLogDir);
    auto path = litebus::os::Join(parentPath, runtimeID);
    if (!litebus::os::ExistPath(path)) {
        YRLOG_WARN("std log path {} not found, try to make dir", path);
        if (!litebus::os::Mkdir(path).IsNone()) {
            YRLOG_WARN("failed to make dir {}, msg: {}", path, litebus::os::Strerror(errno));
            return;
        }
    }
    char realPath[PATH_MAX] = { 0 };
    if (realpath(path.c_str(), realPath) == nullptr) {
        YRLOG_WARN("real path std log file {} failed, errno: {}, {}", path, errno, litebus::os::Strerror(errno));
        return;
    }

    stdOut = litebus::os::Join(std::string(realPath), "stdout");
    if (!litebus::os::ExistPath(stdOut) && TouchFile(stdOut) != 0) {
        YRLOG_WARN("create std out log file {} failed.", stdOut, litebus::os::Strerror(errno));
        return;
    }
    stdErr = litebus::os::Join(std::string(realPath), "stderr");
    if (!litebus::os::ExistPath(stdErr) && TouchFile(stdErr) != 0) {
        YRLOG_WARN("create std err log file {} failed.", stdErr, litebus::os::Strerror(errno));
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

Envs BuildMountForCode(const std::shared_ptr<runtime::v1::StartRequest> &start,
                       const std::shared_ptr<messages::StartInstanceRequest> &request, const Envs &envs)
{
    Envs updateEnv = envs;
    auto deploySpec = request->runtimeinstanceinfo().deploymentconfig();
    auto layerPath = litebus::os::Join(deploySpec.deploydir(), RUNTIME_LAYER_DIR_NAME);
    auto funcPath = litebus::os::Join(layerPath, RUNTIME_FUNC_DIR_NAME);
    auto code = start->add_mounts();
    code->set_type("bind");

    auto libPathIter = envs.posixEnvs.find("YR_FUNCTION_LIB_PATH");
    if (libPathIter != envs.posixEnvs.end() && !libPathIter->second.empty()) {
        funcPath = libPathIter->second;
    }

    auto workingDirIter = envs.posixEnvs.find(UNZIPPED_WORKING_DIR);
    if (workingDirIter == envs.posixEnvs.end() || workingDirIter->second.empty()) {
        code->set_source(funcPath);
    } else {
        code->set_source(workingDirIter->second);
        if (workingDirIter->second.find(".img") != std::string::npos) {
            code->set_type("erofs");
            funcPath = DirName(workingDirIter->second);
        }
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
    auto funcRt = start->mutable_funcruntime();
    funcRt->set_id(request->runtimeinstanceinfo().container().id());
    funcRt->set_sandbox(request->runtimeinstanceinfo().container().runtime());
    *funcRt->mutable_rootfs() = request->runtimeinstanceinfo().container().rootfsconfig();
    for (const auto &arg : buildArgs) {
        *funcRt->add_command() = arg;
    }
    auto dssocket = start->add_mounts();
    dssocket->set_type("bind");
    dssocket->set_source("/opt/ds/socket/");
    dssocket->set_target("/opt/ds/socket/");
    auto updateEnv = BuildMountForCode(start, request, envs);

    // todo: should be more elegant
    const auto &resources = request->runtimeinstanceinfo().runtimeconfig().resources();
    if (resources.resources().find(CPU_RESOURCE_NAME) == resources.resources().end()
        || resources.resources().at(CPU_RESOURCE_NAME).type() != ValueType::Value_Type_SCALAR) {
        (*start->mutable_resources())[CPU_RESOURCE_NAME] = 500;
    } else {
        (*start->mutable_resources())[CPU_RESOURCE_NAME] = resources.resources().at(CPU_RESOURCE_NAME).scalar().value();
    }

    if (resources.resources().find(MEMORY_RESOURCE_NAME) == resources.resources().end()
        || resources.resources().at(MEMORY_RESOURCE_NAME).type() != ValueType::Value_Type_SCALAR) {
        (*start->mutable_resources())[MEMORY_RESOURCE_NAME] = 500;
    } else {
        (*start->mutable_resources())[MEMORY_RESOURCE_NAME] =
            resources.resources().at(MEMORY_RESOURCE_NAME).scalar().value();
    }

    // currently all treated as runtimeEnv
    // todo lwy for fork friendly, the immutable env should be mv to runtimeEnv
    const std::map<std::string, std::string> combineEnvs = cmdBuilder_.CombineEnvs(updateEnv);
    start->mutable_userenvs()->insert(combineEnvs.begin(), combineEnvs.end());
    start->set_stdout(stdOut);
    start->set_stderr(stdErr);

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
    functionsystem::metrics::MeterTitle title{ "yr_instance_stop_time", "stop timestamp", "num" };
    litebus::Async(GetAID(), &ContainerExecutor::ReportInfo, instanceID, runtimeID, containerID, title);
    (void)runtime2containerID_.erase(runtimeID);
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
    YRLOG_DEBUG("{}|{}|instance({}) runtime({}) with container({})", request->runtimeinstanceinfo().traceid(),
                request->runtimeinstanceinfo().requestid(), request->runtimeinstanceinfo().instanceid(),
                request->runtimeinstanceinfo().runtimeid(), containerID);
    // set to be zero
    instanceResponse->set_pid(0);
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
    for (const auto &arg : buildArgs) {
        *warmup->add_command() = arg;
    }
    // BuildMountForCode(start, request);
    // currently all treated as runtimeEnv
    // todo lwy for fork friendly, the immutable env should be mv to runtimeEnv
    warmup->mutable_runtimeenvs()->insert(combineEnvs.begin(), combineEnvs.end());
    if (auto env = litebus::os::GetEnv("YR_ENV_FILE"); env.IsSome()) {
        (*warmup->mutable_runtimeenvs())["YR_ENV_FILE"] = env.Get();
    }
    if (auto ready = litebus::os::GetEnv("YR_SEED_FILE"); ready.IsSome()) {
        (*warmup->mutable_runtimeenvs())["YR_SEED_FILE"] = ready.Get();
    }
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

litebus::Future<runtime::v1::NormalResponse> ContainerExecutor::DoRegisterToWarmUp(
    const std::shared_ptr<runtime::v1::RegisterRequest> &reg)
{
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

litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> ContainerExecutorProxy::GetRuntimeInstanceInfos()
{
    return litebus::Async(executor_->GetAID(), &ContainerExecutor::GetRuntimeInstanceInfos);
}

}  // namespace functionsystem::runtime_manager