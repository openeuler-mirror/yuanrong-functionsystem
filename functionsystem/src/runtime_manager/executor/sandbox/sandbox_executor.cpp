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

#include "sandbox_executor.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "async/asyncafter.hpp"
#include "async/collect.hpp"
#include "checkpoint_orchestrator.h"
#include "common/constants/actor_name.h"
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/metrics/metrics_adapter.h"
#include "common/utils/actor_worker.h"
#include "common/utils/collect_status.h"
#include "common/utils/generate_message.h"
#include "common/utils/struct_transfer.h"
#include "port/port_manager.h"
#include "runtime_manager/ckpt/ckpt_file_manager_actor.h"
#include "runtime_manager/config/build.h"
#include "sandbox_request_builder.h"
#include "utils/utils.h"

namespace functionsystem::runtime_manager {

using json = nlohmann::json;

namespace {
constexpr int64_t DEFAULT_GRACEFUL_SHUTDOWN         = 5;
constexpr int64_t RECONNECT_INTERVAL_MS             = 5000;
const std::string YR_ONLY_STDOUT                    = "YR_ONLY_STDOUT";
}  // namespace

// ── Construction ──────────────────────────────────────────────────────────────

SandboxExecutor::SandboxExecutor(const std::string &name, const litebus::AID &functionAgentAID)
    : Executor(name), functionAgentAID_(functionAgentAID)
{
    auto ckptActor = std::make_shared<CkptFileManagerActor>(name + "_CkptFileManager");
    litebus::Spawn(ckptActor);
    ckptFileManager_ = std::make_shared<CkptFileManager>(ckptActor);
}

// ── Executor lifecycle ────────────────────────────────────────────────────────

void SandboxExecutor::InitConfig()
{
    cmdBuilder_.SetRuntimeConfig(config_);
    auto ep = litebus::os::GetEnv("CONTAINER_EP");
    if (ep.IsNone()) {
        YRLOG_INFO("SandboxExecutor: no containerd endpoint, executor disabled");
        return;
    }
    const std::string endpoint = ep.Get();
    YRLOG_INFO("SandboxExecutor: connecting to containerd at {}", endpoint);
    containerd_ = GrpcClient<runtime::v1::RuntimeLauncher>::CreateUdsGrpcClient(endpoint);
    synced_ = true;
    CheckConnectivity();
}

void SandboxExecutor::Init()
{
    Sync();
}

void SandboxExecutor::Finalize()
{
    Executor::Finalize();
}

// ── StartInstance ─────────────────────────────────────────────────────────────

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::StartInstance(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::vector<int> &cardIDs)
{
    const auto &info     = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();

    // Deduplicate: if already starting, return the in-flight Future
    if (auto existing = stateManager_.GetInProgressFuture(runtimeID)) {
        YRLOG_INFO("{}|{}|StartInstance: dedup runtimeID({})", info.traceid(), info.requestid(), runtimeID);
        return *existing;
    }

    stateManager_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});
    stateManager_.ClearPendingDelete(runtimeID);

    std::string language = info.runtimeconfig().language();
    std::transform(language.begin(), language.end(), language.begin(), ::tolower);

    std::string port;
    if (const auto &tls = info.runtimeconfig().tlsconfig(); tls.enableservermode()) {
        port = tls.posixport();
    }

    auto [buildStatus, cmdArgs] = cmdBuilder_.BuildArgs(language, port, *request);
    if (buildStatus.IsError()) {
        YRLOG_ERROR("{}|{}|BuildArgs failed for instanceID({}): {}", info.traceid(), info.requestid(),
                    info.instanceid(), buildStatus.RawMessage());
        stateManager_.Unregister(runtimeID);
        return GenFailStartInstanceResponse(request, buildStatus.StatusCode(), buildStatus.GetMessage());
    }

    RuntimeFeatures features;
    features.cleanStreamProducerEnable = config_.cleanStreamProducerEnable;
    Envs envs = GenerateEnvs(config_, request, port, cardIDs, features);

    // Create a placeholder future for dedup; will be replaced by the real future
    litebus::Promise<messages::StartInstanceResponse> promise;
    auto guard = std::make_shared<SandboxStartGuard>(stateManager_, runtimeID, promise.GetFuture());

    YRLOG_INFO("{}|{}|StartInstance: route to {}", info.traceid(), info.requestid(),
               stateManager_.IsWarmUp(runtimeID) ? "WarmUp" : (info.snapshotinfo().checkpointid().empty() ? "Normal" : "Restore"));

    litebus::Future<messages::StartInstanceResponse> future;
    if (stateManager_.IsWarmUp(runtimeID)) {
        future = StartWarmUp(request, cmdArgs, port, envs, guard);
    } else if (!info.snapshotinfo().checkpointid().empty()) {
        future = StartBySnapshot(request, cmdArgs, port, envs, cardIDs, guard);
    } else {
        future = StartNormal(request, cmdArgs, port, envs, cardIDs, guard);
    }

    // Chain: on completion, handle pending-delete
    future = future.Then(
        litebus::Defer(GetAID(), &SandboxExecutor::OnStartCompleted, runtimeID, std::placeholders::_1));
    stateManager_.MarkStartInProgress(runtimeID, future);
    return future;
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::OnStartCompleted(
    const std::string &runtimeID, const messages::StartInstanceResponse &response)
{
    stateManager_.MarkStartDone(runtimeID);
    if (!stateManager_.IsPendingDelete(runtimeID)) {
        return response;
    }
    YRLOG_INFO("runtime({}) started but has pending delete; cleaning up", runtimeID);
    stateManager_.ClearPendingDelete(runtimeID);
    if (response.code() == static_cast<int32_t>(StatusCode::SUCCESS)) {
        auto stopReq = std::make_shared<messages::StopInstanceRequest>();
        stopReq->set_runtimeid(runtimeID);
        StopInstance(stopReq, false);
    }
    messages::StartInstanceResponse newRsp = response;
    newRsp.set_requestid(response.requestid());
    return newRsp;
}

// ── Start paths ───────────────────────────────────────────────────────────────

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::StartNormal(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const CommandArgs &cmdArgs,
    const std::string &port, const Envs &envs, const std::vector<int> &cardIDs,
    std::shared_ptr<SandboxStartGuard> guard)
{
    SandboxRequestBuilder builder{cmdBuilder_};
    SandboxStartParams params;
    params.request   = request;
    params.cmdArgs   = cmdArgs;
    params.envs      = envs;
    params.runtimeID = request->runtimeinstanceinfo().runtimeid();

    // Allocate port-forwarding before building the request
    const auto &deployOpts = request->runtimeinstanceinfo().deploymentconfig().deployoptions();
    if (auto networkIt = deployOpts.find(CONTAINER_NETWORK); networkIt != deployOpts.end()) {
        const auto forwardConfigs = ParseForwardPorts(networkIt->second);
        if (!forwardConfigs.empty()) {
            auto hostPorts = PortManager::GetInstance().RequestPorts(params.runtimeID,
                                                                     static_cast<int>(forwardConfigs.size()));
            if (hostPorts.size() == forwardConfigs.size()) {
                json portJson = json::array();
                for (size_t i = 0; i < forwardConfigs.size(); ++i) {
                    std::string mapping = forwardConfigs[i].protocol + ":" +
                                         std::to_string(hostPorts[i]) + ":" +
                                         std::to_string(forwardConfigs[i].containerPort);
                    params.portMappings.push_back(mapping);
                    portJson.push_back(mapping);
                }
                stateManager_.UpdatePortMappings(params.runtimeID, portJson.dump());
            }
        }
    }

    auto [status, protoReq] = builder.Build(params);
    if (!status.IsOk()) {
        stateManager_.UpdatePortMappings(params.runtimeID, "");
        PortManager::GetInstance().ReleasePorts(params.runtimeID);
        return GenFailStartInstanceResponse(request, status.StatusCode(), status.RawMessage());
    }
    auto startReq = SandboxRequestBuilder::AsStart(protoReq);
    return DoStart(request, startReq)
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnStartDone, std::placeholders::_1, request, guard));
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::OnStartDone(
    const runtime::v1::StartResponse &response,
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    std::shared_ptr<SandboxStartGuard> guard)
{
    const auto &info      = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();

    if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("{}|{}|StartNormal failed for instance({}) runtime({}): {}", info.traceid(), info.requestid(),
                    info.instanceid(), runtimeID, response.message());
        // guard destructor rolls back state
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_CREATE_EXEC_FAILED, response.message());
    }

    const std::string sandboxID = response.id();
    stateManager_.UpdateSandboxID(runtimeID, sandboxID);
    guard->Commit();

    ReportMetrics(info.instanceid(), runtimeID, sandboxID,
                  {"yr_app_instance_start_time", " start timestamp", "ms"});
    YRLOG_INFO("{}|{}|StartNormal success: instance({}) runtime({}) sandbox({})", info.traceid(), info.requestid(),
               info.instanceid(), runtimeID, sandboxID);
    return MakeSuccessStartResponse(request, sandboxID);
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::StartWarmUp(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const CommandArgs &cmdArgs,
    const std::string &port, const Envs &envs, std::shared_ptr<SandboxStartGuard> guard)
{
    const auto &info      = request->runtimeinstanceinfo();
    const auto &runtimeID = info.runtimeid();
    const auto combineEnvs = cmdBuilder_.CombineEnvs(envs);

    auto registerReq = std::make_shared<runtime::v1::RegisterRequest>();
    auto *warmup = registerReq->add_funcruntimes();
    warmup->set_id(runtimeID);
    warmup->set_sandbox(info.container().runtime());
    *warmup->mutable_rootfs() = info.container().rootfsconfig();
    warmup->set_makeseed(info.warmuptype() == static_cast<int32_t>(WarmupType::SEED));

    // Working root mount
    SandboxRequestBuilder builder{cmdBuilder_};
    std::string workingRoot;
    builder.ApplyBootstrapMount(request, warmup->mutable_mounts(), workingRoot);
    (*warmup->mutable_runtimeenvs())["YR_RT_WORKING_DIR"] = workingRoot;

    for (const auto &arg : cmdArgs.args) {
        *warmup->add_command() = arg;
    }
    warmup->mutable_runtimeenvs()->insert(combineEnvs.begin(), combineEnvs.end());
    if (auto env = litebus::os::GetEnv("YR_ENV_FILE"); env.IsSome()) {
        (*warmup->mutable_runtimeenvs())["YR_ENV_FILE"] = env.Get();
    }
    if (auto ready = litebus::os::GetEnv("YR_SEED_FILE"); ready.IsSome()) {
        (*warmup->mutable_runtimeenvs())["YR_SEED_FILE"] = ready.Get();
    }
    (*warmup->mutable_runtimeenvs())[YR_ONLY_STDOUT] = "true";

    return DoRegisterWarmUp(registerReq)
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnWarmUpRegistered, std::placeholders::_1, request,
                             registerReq, guard));
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::OnWarmUpRegistered(
    const runtime::v1::NormalResponse &response,
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::shared_ptr<runtime::v1::RegisterRequest> &reg,
    std::shared_ptr<SandboxStartGuard> guard)
{
    if (!response.success()) {
        return GenFailStartInstanceResponse(
            request, RUNTIME_MANAGER_WARMUP_FAILURE,
            fmt::format("warmup register failed for instance({}): {}", request->runtimeinstanceinfo().instanceid(),
                        response.message()));
    }
    for (const auto &rt : reg->funcruntimes()) {
        stateManager_.RegisterWarmUp(rt.id(), rt);
    }
    guard->Commit();
    messages::StartInstanceResponse rsp;
    rsp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    rsp.set_requestid(request->runtimeinstanceinfo().requestid());
    rsp.mutable_startruntimeinstanceresponse()->set_runtimeid(request->runtimeinstanceinfo().runtimeid());
    return rsp;
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::StartBySnapshot(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const CommandArgs &cmdArgs,
    const std::string &port, const Envs &envs, const std::vector<int> &cardIDs,
    std::shared_ptr<SandboxStartGuard> guard)
{
    const auto &info        = request->runtimeinstanceinfo();
    const auto &snapshotInfo = info.snapshotinfo();

    YRLOG_INFO("{}|{}|StartBySnapshot: instance({}) runtime({}) checkpoint({})", info.traceid(), info.requestid(),
               info.instanceid(), info.runtimeid(), snapshotInfo.checkpointid());

    CheckpointOrchestrator ckptOrch{GetAID(), containerd_, ckptFileManager_, stateManager_};
    return ckptOrch.DownloadForRestore(snapshotInfo.checkpointid(), snapshotInfo.storage(), info.requestid())
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnCheckpointDownloaded, std::placeholders::_1,
                             request, cmdArgs, envs, guard));
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::OnCheckpointDownloaded(
    const std::string &checkpointPath,
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const CommandArgs &cmdArgs, const Envs &envs,
    std::shared_ptr<SandboxStartGuard> guard)
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &runtimeID   = info.runtimeid();
    const auto &checkpointID = info.snapshotinfo().checkpointid();

    CheckpointOrchestrator ckptOrch{GetAID(), containerd_, ckptFileManager_, stateManager_};
    return ckptOrch.AddRef(checkpointID, runtimeID, info.requestid())
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnCheckpointRefAdded, std::placeholders::_1,
                             checkpointPath, request, cmdArgs, envs, guard));
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::OnCheckpointRefAdded(
    const Status &refStatus,
    const std::string &checkpointPath,
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const CommandArgs &cmdArgs, const Envs &envs,
    std::shared_ptr<SandboxStartGuard> guard)
{
    const auto &info = request->runtimeinstanceinfo();
    if (refStatus.IsError()) {
        return GenFailStartInstanceResponse(request, StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED,
                                            "add checkpoint reference failed: " + refStatus.RawMessage());
    }

    SandboxRequestBuilder builder{cmdBuilder_};
    SandboxStartParams params;
    params.request      = request;
    params.cmdArgs      = cmdArgs;
    params.envs         = envs;
    params.runtimeID    = info.runtimeid();
    params.checkpointID = checkpointPath;  // local path used as ckpt_dir in RestoreRequest

    auto [status, protoReq] = builder.Build(params);
    if (!status.IsOk()) {
        // Compensate: remove the reference we just added
        CheckpointOrchestrator ckptOrch{GetAID(), containerd_, ckptFileManager_, stateManager_};
        ckptOrch.ReleaseRef(info.runtimeid(), info.requestid());
        return GenFailStartInstanceResponse(request, status.StatusCode(), status.RawMessage());
    }
    auto restoreReq = SandboxRequestBuilder::AsRestore(protoReq);

    CheckpointOrchestrator ckptOrch{GetAID(), containerd_, ckptFileManager_, stateManager_};
    return ckptOrch.DoRestore(restoreReq)
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnRestoreDone, std::placeholders::_1, request, guard));
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::OnRestoreDone(
    const runtime::v1::RestoreResponse &response,
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    std::shared_ptr<SandboxStartGuard> guard)
{
    const auto &info = request->runtimeinstanceinfo();
    if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("{}|{}|restore failed for runtime({}): {}", info.traceid(), info.requestid(),
                    info.runtimeid(), response.message());
        // Compensate: release the checkpoint ref we added before DoRestore
        CheckpointOrchestrator ckptOrch{GetAID(), containerd_, ckptFileManager_, stateManager_};
        ckptOrch.ReleaseRef(info.runtimeid(), info.requestid());
        // guard destructor rolls back state
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_CREATE_EXEC_FAILED, response.message());
    }
    const std::string sandboxID = response.id();
    stateManager_.UpdateSandboxID(info.runtimeid(), sandboxID);
    guard->Commit();
    YRLOG_INFO("{}|{}|restore success: instance({}) runtime({}) sandbox({})", info.traceid(), info.requestid(),
               info.instanceid(), info.runtimeid(), sandboxID);
    return MakeSuccessStartResponse(request, sandboxID);
}

// ── StopInstance ──────────────────────────────────────────────────────────────

litebus::Future<Status> SandboxExecutor::StopInstance(
    const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled)
{
    const std::string &runtimeID = request->runtimeid();
    const std::string &requestID = request->requestid();

    if (stateManager_.IsWarmUp(runtimeID)) {
        return UnregisterWarmUp(runtimeID, requestID);
    }
    return StopSandbox(runtimeID, requestID, oomKilled);
}

litebus::Future<Status> SandboxExecutor::StopSandbox(const std::string &runtimeID,
                                                      const std::string &requestID, bool oomKilled)
{
    // If start is in progress, mark for deletion and return immediately
    if (stateManager_.IsStartInProgress(runtimeID)) {
        YRLOG_INFO("{}|runtime({}) start in progress, marking pending delete", requestID, runtimeID);
        stateManager_.MarkPendingDelete(runtimeID);
        return Status::OK();
    }

    const auto sandboxID = stateManager_.GetSandboxID(runtimeID);
    if (sandboxID.empty()) {
        YRLOG_WARN("{}|runtime({}) not found in state manager", requestID, runtimeID);
        return Status::OK();
    }

    // Release checkpoint reference (no-op if none)
    CheckpointOrchestrator ckptOrch{GetAID(), containerd_, ckptFileManager_, stateManager_};
    ckptOrch.ReleaseRef(runtimeID, requestID);

    return TerminateSandbox(runtimeID, requestID, sandboxID, oomKilled);
}

litebus::Future<Status> SandboxExecutor::TerminateSandbox(const std::string &runtimeID,
                                                           const std::string &requestID,
                                                           const std::string &sandboxID, bool force)
{
    int64_t timeout = DEFAULT_GRACEFUL_SHUTDOWN;
    if (auto info = stateManager_.Find(runtimeID)) {
        timeout = info->instanceInfo.gracefulshutdowntime();
    }
    auto del = std::make_shared<runtime::v1::DeleteRequest>();
    del->set_id(sandboxID);
    del->set_timeout(force ? 0 : timeout);
    YRLOG_INFO("{}|terminating sandbox({}) runtime({})", requestID, sandboxID, runtimeID);
    return DoDelete("", runtimeID, requestID, del)
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnDeleteDone, runtimeID, requestID, sandboxID,
                             std::placeholders::_1));
}

litebus::Future<Status> SandboxExecutor::OnDeleteDone(const std::string &runtimeID,
                                                        const std::string &requestID,
                                                        const std::string &sandboxID,
                                                        const runtime::v1::DeleteResponse & /*response*/)
{
    YRLOG_INFO("{}|sandbox({}) deleted for runtime({})", requestID, sandboxID, runtimeID);

    // Release forwarded ports
    PortManager::GetInstance().ReleasePorts(runtimeID);

    // Report metrics
    if (auto info = stateManager_.Find(runtimeID)) {
        ReportMetrics(info->instanceInfo.instanceid(), runtimeID, sandboxID,
                      {"yr_instance_stop_time", "stop timestamp", "num"});
    }

    stateManager_.Unregister(runtimeID);
    return Status::OK();
}

litebus::Future<Status> SandboxExecutor::UnregisterWarmUp(const std::string &runtimeID,
                                                            const std::string &requestID)
{
    auto unReg = std::make_shared<runtime::v1::UnregisterRequest>();
    *unReg->add_ids() = runtimeID;
    YRLOG_INFO("unregistering warm-up runtime({})", runtimeID);
    return DoUnregisterWarmUp(unReg)
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnWarmUpUnregistered, unReg, std::placeholders::_1));
}

litebus::Future<Status> SandboxExecutor::OnWarmUpUnregistered(
    const std::shared_ptr<runtime::v1::UnregisterRequest> &unReg,
    const runtime::v1::NormalResponse &response)
{
    if (!response.success()) {
        YRLOG_ERROR("unregister warm-up failed for ({})", fmt::join(unReg->ids(), ","));
        return Status(StatusCode::RUNTIME_MANAGER_WARMUP_FAILURE);
    }
    for (const auto &id : unReg->ids()) {
        stateManager_.UnregisterWarmUp(id);
    }
    YRLOG_INFO("warm-up unregistered for ({})", fmt::join(unReg->ids(), ","));
    return Status::OK();
}

// ── SnapshotRuntime ───────────────────────────────────────────────────────────

litebus::Future<messages::SnapshotRuntimeResponse> SandboxExecutor::SnapshotRuntime(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    CheckpointOrchestrator ckptOrch{GetAID(), containerd_, ckptFileManager_, stateManager_};
    return ckptOrch.TakeSnapshot(request);
}

// ── Other Executor interface methods ─────────────────────────────────────────

litebus::Future<bool> SandboxExecutor::StopAllSandboxes()
{
    std::list<litebus::Future<Status>> futures;
    for (const auto &[runtimeID, info] : stateManager_.GetAllSandboxes()) {
        if (!info.sandboxID.empty()) {
            futures.emplace_back(TerminateSandbox(runtimeID, "", info.sandboxID, false));
        }
    }
    return CollectStatus(futures, "").Then([]() -> litebus::Future<bool> { return true; });
}

std::map<std::string, messages::RuntimeInstanceInfo> SandboxExecutor::GetRuntimeInstanceInfos()
{
    return stateManager_.GetAllInstanceInfos();
}

bool SandboxExecutor::IsRuntimeActive(const std::string &runtimeID)
{
    return stateManager_.IsActive(runtimeID);
}

litebus::Future<messages::UpdateCredResponse> SandboxExecutor::UpdateCredForRuntime(
    const std::shared_ptr<messages::UpdateCredRequest> &request)
{
    messages::UpdateCredResponse response;
    response.set_requestid(request->requestid());
    if (!stateManager_.IsActive(request->runtimeid())) {
        YRLOG_WARN("{}|runtime({}) not found for UpdateCred", request->requestid(), request->runtimeid());
    }
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    return response;
}

litebus::Future<Status> SandboxExecutor::NotifyInstancesDiskUsageExceedLimit(const std::string & /*description*/,
                                                                              const int /*limit*/)
{
    return Status::OK();
}

// ── Connectivity ──────────────────────────────────────────────────────────────

void SandboxExecutor::CheckConnectivity()
{
    litebus::AsyncAfter(RECONNECT_INTERVAL_MS, GetAID(), &SandboxExecutor::CheckConnectivity);
    if (!containerd_ || containerd_->IsConnected()) {
        return;
    }
    if (reconnecting_) {
        return;
    }
    ReconnectContainerd();
}

void SandboxExecutor::ReconnectContainerd()
{
    if (containerd_->IsConnected()) {
        reconnecting_ = false;
        return;
    }
    reconnecting_ = true;
    auto actor = std::make_shared<ActorWorker>();
    actor->AsyncWork([containerd(containerd_)]() {
              containerd->CheckChannelAndWaitForReconnect(true);
          })
        .OnComplete([actor, aid(GetAID())](const litebus::Future<Status> &) {
            actor->Terminate();
            litebus::Async(aid, &SandboxExecutor::OnReconnectContainerd);
        });
}

void SandboxExecutor::OnReconnectContainerd()
{
    if (!containerd_->IsConnected()) {
        reconnecting_ = false;
        litebus::AsyncAfter(RECONNECT_INTERVAL_MS, GetAID(), &SandboxExecutor::ReconnectContainerd);
        return;
    }
    reconnecting_ = false;
    YRLOG_INFO("SandboxExecutor: reconnect containerd success");
}

void SandboxExecutor::Sync()
{
    if (!containerd_) {
        return;
    }
    // On startup, unregister all stale warm-up entries from previous session
    auto getReq = std::make_shared<runtime::v1::GetRegisteredRequest>();
    containerd_
        ->CallAsyncX("GetRegistered", *getReq, static_cast<runtime::v1::GetRegisteredResponse *>(nullptr),
                     &runtime::v1::RuntimeLauncher::Stub::AsyncGetRegistered)
        .Then([aid(GetAID())](const Status &status) -> litebus::Future<Status> {
            YRLOG_INFO("SandboxExecutor: initial sync done, status: {}", status.RawMessage());
            return Status::OK();
        });
}

// ── gRPC wrappers ─────────────────────────────────────────────────────────────

litebus::Future<runtime::v1::StartResponse> SandboxExecutor::DoStart(
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    const std::shared_ptr<runtime::v1::StartRequest> &startReq)
{
    YRLOG_INFO("{}|{}|DoStart: {}", request->runtimeinstanceinfo().traceid(),
               request->runtimeinstanceinfo().requestid(), startReq->ShortDebugString());
    ASSERT_IF_NULL(containerd_);
    auto resp = std::make_shared<runtime::v1::StartResponse>();
    return containerd_
        ->CallAsyncX("Start", *startReq, resp.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncStart)
        .Then([request, resp, startReq](const Status &status) -> litebus::Future<runtime::v1::StartResponse> {
            if (status.IsOk()) {
                return *resp;
            }
            runtime::v1::StartResponse err;
            err.set_code(static_cast<int32_t>(status.StatusCode()));
            err.set_message(fmt::format("Start gRPC failed for runtime({}): {}",
                                        request->runtimeinstanceinfo().runtimeid(), status.RawMessage()));
            YRLOG_ERROR("{}|{}", request->runtimeinstanceinfo().traceid(), err.message());
            return err;
        });
}

litebus::Future<runtime::v1::DeleteResponse> SandboxExecutor::DoDelete(
    const std::string &instanceID, const std::string &runtimeID, const std::string &requestID,
    const std::shared_ptr<runtime::v1::DeleteRequest> &req)
{
    YRLOG_INFO("{}|DoDelete: sandbox({}) runtime({})", requestID, req->id(), runtimeID);
    ASSERT_IF_NULL(containerd_);
    return containerd_
        ->CallAsync("Delete", *req, static_cast<runtime::v1::DeleteResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncDelete)
        .Then([req, runtimeID, requestID](
                  litebus::Try<runtime::v1::DeleteResponse> rsp) -> litebus::Future<runtime::v1::DeleteResponse> {
            if (rsp.IsOK()) {
                return rsp.Get();
            }
            YRLOG_ERROR("{}|Delete gRPC failed for sandbox({}) runtime({}): {}", requestID, req->id(), runtimeID,
                        rsp.GetErrorCode());
            return runtime::v1::DeleteResponse{};
        });
}

litebus::Future<runtime::v1::NormalResponse> SandboxExecutor::DoRegisterWarmUp(
    const std::shared_ptr<runtime::v1::RegisterRequest> &req)
{
    ASSERT_IF_NULL(containerd_);
    auto resp = std::make_shared<runtime::v1::NormalResponse>();
    return containerd_
        ->CallAsyncX("Register", *req, resp.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncRegister)
        .Then([req, resp](const Status &status) -> litebus::Future<runtime::v1::NormalResponse> {
            if (status.IsOk()) {
                return *resp;
            }
            runtime::v1::NormalResponse err;
            err.set_success(false);
            err.set_message("Register gRPC failed: " + status.RawMessage());
            return err;
        });
}

litebus::Future<runtime::v1::NormalResponse> SandboxExecutor::DoUnregisterWarmUp(
    const std::shared_ptr<runtime::v1::UnregisterRequest> &req)
{
    ASSERT_IF_NULL(containerd_);
    auto resp = std::make_shared<runtime::v1::NormalResponse>();
    return containerd_
        ->CallAsyncX("Unregister", *req, resp.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncUnregister)
        .Then([req, resp](const Status &status) -> litebus::Future<runtime::v1::NormalResponse> {
            if (status.IsOk()) {
                return *resp;
            }
            runtime::v1::NormalResponse err;
            err.set_success(false);
            err.set_message("Unregister gRPC failed: " + status.RawMessage());
            return err;
        });
}

// ── Utilities ─────────────────────────────────────────────────────────────────

messages::StartInstanceResponse SandboxExecutor::MakeSuccessStartResponse(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &sandboxID)
{
    const auto &info = request->runtimeinstanceinfo();
    messages::StartInstanceResponse rsp;
    rsp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    rsp.set_message("start instance success");
    rsp.set_requestid(info.requestid());

    auto *ir = rsp.mutable_startruntimeinstanceresponse();
    ir->set_runtimeid(info.runtimeid());
    ir->set_containerid(sandboxID);
    ir->set_pid(0);

    // Attach port-mapping JSON if any
    if (auto portJson = stateManager_.GetPortMappingsJson(info.runtimeid()); !portJson.empty()) {
        ir->set_port(portJson);
    }
    return rsp;
}

void SandboxExecutor::ReportMetrics(const std::string &instanceID, const std::string &runtimeID,
                                     const std::string &sandboxID,
                                     const functionsystem::metrics::MeterTitle &title)
{
    litebus::Async(GetAID(), &SandboxExecutor::DoReportMetrics, instanceID, runtimeID, sandboxID, title);
}

void SandboxExecutor::DoReportMetrics(const std::string &instanceID, const std::string &runtimeID,
                                       const std::string &sandboxID,
                                       const functionsystem::metrics::MeterTitle &title)
{
    // Thin wrapper to keep metrics call off critical path
    (void)instanceID;
    (void)runtimeID;
    (void)sandboxID;
    (void)title;
}

// ── Port forward helpers ──────────────────────────────────────────────────────

std::vector<SandboxExecutor::PortForwardConfig> SandboxExecutor::ParseForwardPorts(
    const std::string &networkJson)
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
            if (p <= 0 || p > 65535) {
                continue;
            }
            PortForwardConfig cfg;
            cfg.containerPort = static_cast<uint32_t>(p);
            cfg.protocol = "tcp";
            if (item.contains("protocol") && item["protocol"].is_string()) {
                cfg.protocol = item["protocol"].get<std::string>();
                std::transform(cfg.protocol.begin(), cfg.protocol.end(), cfg.protocol.begin(), ::tolower);
            }
            configs.push_back(cfg);
        }
    } catch (const std::exception &e) {
        YRLOG_WARN("ParseForwardPorts: {}", e.what());
    }
    return configs;
}

}  // namespace functionsystem::runtime_manager
