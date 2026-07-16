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
#include <chrono>
#include <unordered_set>
#include <nlohmann/json.hpp>

#include "async/asyncafter.hpp"
#include "async/collect.hpp"
#include "common/constants/actor_name.h"
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/metrics/metrics_adapter.h"
#include "common/resource_view/resource_type.h"
#include "common/trace/create_trace_helper.h"
#include "common/utils/actor_worker.h"
#include "common/utils/collect_status.h"
#include "common/utils/generate_message.h"
#include "common/utils/struct_transfer.h"
#include "port/port_manager.h"
#include "runtime_manager/ckpt/ckpt_file_manager_actor.h"
#include "runtime_manager/config/build.h"
#include "sandbox_request_builder.h"
#include "sandbox_command_utils.h"
#include "utils/utils.h"

namespace functionsystem::runtime_manager {

using json = nlohmann::json;

namespace {
// Downstream sandboxd port forwarding only accepts L4 protocols (tcp/udp). L7 portForward schemes (http/https/ws/wss)
// are normalized to tcp before sending to sandboxd because the underlying mapping is TCP NAT; other schemes (tcp/udp, etc.) are preserved.
// The portForward written back to instanceinfo keeps the original scheme for sandboxRouter L7 routing.
std::string ToDownstreamL4Protocol(const std::string &proto)
{
    if (proto == "http" || proto == "https" || proto == "ws" || proto == "wss") {
        return "tcp";
    }
    return proto;
}

std::string ResolveRuntimeLanguage(const messages::RuntimeInstanceInfo &info)
{
    std::string language = info.runtimeconfig().language();
    std::transform(language.begin(), language.end(), language.begin(), ::tolower);
    return language;
}
}  // namespace

namespace {
constexpr int64_t DEFAULT_GRACEFUL_SHUTDOWN         = 5;
constexpr int64_t RECONNECT_INTERVAL_MS             = 5000;
// Backoff bounds for ReconcileRuntimes when containerd is still connecting.
// Exponential: 1s, 2s, 4s, 8s, capped at RECONCILE_RETRY_MAX_INTERVAL_MS.
// MAX_RETRIES is intentionally generous so a slow containerd cold start
// (image pull etc.) doesn't fail-open the reconcile and let orphans survive.
constexpr int64_t RECONCILE_RETRY_INITIAL_MS        = 1000;
constexpr int64_t RECONCILE_RETRY_MAX_INTERVAL_MS   = 10000;
constexpr int32_t RECONCILE_MAX_RETRIES             = 120;
constexpr int32_t CONTAINER_DELETE_TIMEOUT_SEC      = 10;
const std::string YR_ONLY_STDOUT                    = "YR_ONLY_STDOUT";

// Wait retry on sandboxd disconnection (total 5 minutes tolerance)
constexpr int32_t WAIT_MAX_RETRIES                  = 30;
constexpr int64_t WAIT_RETRY_INTERVAL_MS            = 10000;
constexpr int64_t SANDBOX_STATS_COLLECT_INTERVAL_MS    = 10000;
// Heartbeat interval for RUNNING status re-reporting (30s < Prometheus stale window ~2x scrape_interval)
constexpr int64_t SANDBOX_RUNNING_HEARTBEAT_INTERVAL_MS = 30000;
constexpr double CPU_MILLICORES_PER_CORE            = 1000.0;
constexpr double BYTES_PER_MB                       = 1024.0 * 1024.0;
constexpr double DEFAULT_SANDBOX_CPU_MILLICORES     = 500.0;
constexpr double DEFAULT_SANDBOX_MEMORY_MB          = 500.0;

struct SandboxRequestedResources {
    double cpuCores = 0.0;
    double memoryBytes = 0.0;
};

std::string RootfsTypeToLabel(runtime::v1::RootfsSrcType rootfsType)
{
    switch (rootfsType) {
        case runtime::v1::IMAGE:
            return "image";
        case runtime::v1::S3:
            return "s3";
        case runtime::v1::LOCAL:
            return "local";
        default:
            return "unknown";
    }
}

std::string BuildS3RootfsRef(const runtime::v1::S3Config &s3Config)
{
    if (!s3Config.object().empty()) {
        return s3Config.bucket().empty() ? s3Config.object() : s3Config.bucket() + "/" + s3Config.object();
    }
    return s3Config.bucket();
}

std::string ResolveSandboxImage(const messages::RuntimeInstanceInfo &info)
{
    // First, try to get rootfs from deployoptions
    auto it = info.deploymentconfig().deployoptions().find("rootfs");
    if (it != info.deploymentconfig().deployoptions().end() && !it->second.empty()) {
        try {
            auto parser = json::parse(it->second);
            if (!parser.contains("type")) {
                return "";
            }
            const std::string typeStr = parser.at("type").get<std::string>();
            if (typeStr == "s3") {
                if (!parser.contains("storageInfo")) {
                    return "";
                }
                const auto &si = parser.at("storageInfo");
                const auto bucket = si.contains("bucket") && si.at("bucket").is_string()
                    ? si.at("bucket").get<std::string>()
                    : "";
                const auto object = si.contains("object") && si.at("object").is_string()
                    ? si.at("object").get<std::string>()
                    : "";
                return object.empty() ? bucket : bucket + "/" + object;
            } else if (typeStr == "image") {
                if (parser.contains("imageurl") && parser.at("imageurl").is_string()) {
                    return parser.at("imageurl").get<std::string>();
                }
            } else if (typeStr == "local") {
                if (parser.contains("path") && parser.at("path").is_string()) {
                    return parser.at("path").get<std::string>();
                }
            }
        } catch (const std::exception &e) {
            YRLOG_WARN("ResolveSandboxImage: failed to parse rootfs deploy option: {}", e.what());
        }
    }

    // Fallback: use container().rootfsconfig()
    const auto &rootfs = info.container().rootfsconfig();
    switch (rootfs.type()) {
        case runtime::v1::IMAGE:
            if (!rootfs.image_url().empty()) {
                return rootfs.image_url();
            }
            break;
        case runtime::v1::LOCAL:
            if (!rootfs.path().empty()) {
                return rootfs.path();
            }
            break;
        case runtime::v1::S3:
            if (rootfs.has_s3_config()) {
                return BuildS3RootfsRef(rootfs.s3_config());
            }
            break;
        default:
            break;
    }

    return "";
}

double GetEffectiveScalarLimit(const resource_view::Resource &resource, double defaultValue)
{
    if (resource.type() != resource_view::ValueType::Value_Type_SCALAR) {
        return defaultValue;
    }
    if (resource.scalar().limit() > 0) {
        return resource.scalar().limit();
    }
    if (resource.scalar().value() > 0) {
        return resource.scalar().value();
    }
    return defaultValue;
}

SandboxRequestedResources GetSandboxRequestedResources(const messages::RuntimeInstanceInfo &info)
{
    SandboxRequestedResources requested;
    const auto &resources = info.runtimeconfig().resources().resources();

    auto cpuIt = resources.find(resource_view::CPU_RESOURCE_NAME);
    double cpuMillicores = cpuIt != resources.end()
        ? GetEffectiveScalarLimit(cpuIt->second, DEFAULT_SANDBOX_CPU_MILLICORES)
        : DEFAULT_SANDBOX_CPU_MILLICORES;
    requested.cpuCores = cpuMillicores / CPU_MILLICORES_PER_CORE;

    auto memoryIt = resources.find(resource_view::MEMORY_RESOURCE_NAME);
    double memoryMb = memoryIt != resources.end()
        ? GetEffectiveScalarLimit(memoryIt->second, DEFAULT_SANDBOX_MEMORY_MB)
        : DEFAULT_SANDBOX_MEMORY_MB;
    requested.memoryBytes = memoryMb * BYTES_PER_MB;

    return requested;
}

functionsystem::metrics::LabelType BuildSandboxMetricLabels(const messages::RuntimeInstanceInfo &info,
                                                            const std::string &runtimeID,
                                                            const std::string &sandboxID)
{
    return {
        { "instance_id", info.instanceid() },
        { "runtime_id", runtimeID },
        { "sandbox_id", sandboxID },
        { "sandbox_runtime", info.container().runtime() },
        { "rootfs_type", RootfsTypeToLabel(info.container().rootfsconfig().type()) },
        { "image", ResolveSandboxImage(info) },
    };
}

// Switch (env YR_SANDBOX_METRICS_ENABLED) for the new sandbox-level metrics added in this branch.
// Default OFF to avoid the high-cardinality storm (sandbox_id / runtime_id labels) that overloads
// the OTel collector (10+ CPU cores) and triggers downstream actor stalls / OtlpHttpClient mutex
// assertion on shutdown. Set to "1" / "true" to re-enable.
bool IsSandboxMetricsEnabled()
{
    static const bool enabled = []() {
        auto envOpt = litebus::os::GetEnv("YR_SANDBOX_METRICS_ENABLED");
        bool e = false;
        if (envOpt.IsSome()) {
            const auto &v = envOpt.Get();
            e = (v == "1" || v == "true" || v == "TRUE" || v == "True");
        }
        YRLOG_INFO("[sandbox-metrics] YR_SANDBOX_METRICS_ENABLED={} (default OFF; set to 1 to enable)",
                   e ? "true" : "false");
        return e;
    }();
    return enabled;
}

void ReportSandboxGauge(const functionsystem::metrics::MeterTitle &title,
                        const functionsystem::metrics::LabelType &labels,
                        double value)
{
    if (!IsSandboxMetricsEnabled()) {
        return;
    }
    functionsystem::metrics::MeterData data{ value, labels };
    functionsystem::metrics::MetricsAdapter::GetInstance().ReportDoubleGauge(title, data, { "node_id", "ip" });
}

bool IsNormalSandboxExit(const runtime::v1::WaitResponse &response)
{
    return response.exit_code() == 0 && response.status() == 0;
}

}  // namespace

bool SandboxExecutor::IsRetryableWaitError(const Status &status)
{
    const auto code = status.StatusCode();
    return code == GRPC_UNAVAILABLE
        || code == GRPC_CANCELLED
        || code == GRPC_DEADLINE_EXCEEDED
        || code == GRPC_INTERNAL;
}

void SandboxExecutor::StartSandboxCreateSpan(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    trace::StartSandboxCreateSpan(request);
}

void SandboxExecutor::StopSandboxCreateSpan(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                            const runtime::v1::StartResponse &response)
{
    trace::StopSandboxCreateSpan(request, response);
}

// ── Construction ──────────────────────────────────────────────────────────────

SandboxExecutor::SandboxExecutor(const std::string &name, const litebus::AID &functionAgentAID,
                                 const std::string &checkpointDir)
    : Executor(name), functionAgentAID_(functionAgentAID)
{
    const std::string &dir = checkpointDir.empty() ? DEFAULT_CHECKPOINT_DIR : checkpointDir;
    auto ckptActor = std::make_shared<CkptFileManagerActor>(name + "_CkptFileManager", dir);
    litebus::Spawn(ckptActor);
    ckptFileManager_ = std::make_shared<CkptFileManager>(ckptActor);
}

// ── Executor lifecycle ────────────────────────────────────────────────────────

void SandboxExecutor::InitConfig()
{
    cmdBuilder_.SetRuntimeConfig(config_);
    auto ep = litebus::os::GetEnv("CONTAINER_EP");
    if (!ep.IsNone()) {
        const std::string endpoint = ep.Get();
        YRLOG_INFO("SandboxExecutor: connecting to containerd at {}", endpoint);
        containerd_ = GrpcClient<runtime::v1::RuntimeLauncher>::CreateUdsGrpcClient(endpoint);
        synced_ = true;
        CheckConnectivity();
    } else {
        YRLOG_INFO("SandboxExecutor: no containerd endpoint, executor disabled");
    }
    ckptOrch_ = std::make_shared<CheckpointOrchestrator>(GetAID(), containerd_, ckptFileManager_, stateManager_);
    // Sync MUST run after containerd_ is created. Init() runs before InitConfig()
    // (via Executor::SetRuntimeConfig), so calling Sync() there would be a silent no-op.
    Sync();
}

void SandboxExecutor::Init()
{
    // intentionally empty: real init happens in InitConfig() once containerd_ is ready
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

    stateManager_.Register(SandboxInfo{runtimeID, {}, {}, {}, info});
    stateManager_.ClearPendingDelete(runtimeID);
    sandboxLifecycleStates_.erase(runtimeID);

    bool isWarmUp = info.warmuptype() != static_cast<int32_t>(WarmupType::NONE);
    if (!isWarmUp) {
        ReportSandboxLifecycleStatus(info, runtimeID, SandboxLifecycleStatus::CREATING);
    }

    std::string port;
    if (const auto &tls = info.runtimeconfig().tlsconfig(); tls.enableservermode()) {
        port = tls.posixport();
    }

    CommandArgs cmdArgs;
    if (HasSelfContainedSandboxBootstrap(request)) {
        YRLOG_DEBUG("{}|{}|StartInstance: using self-contained bootstrap command without runtime args",
                    info.traceid(), info.requestid());
    } else {
        std::string language = info.runtimeconfig().language();
        std::transform(language.begin(), language.end(), language.begin(), ::tolower);
        auto [buildStatus, builtCmdArgs] = cmdBuilder_.BuildArgs(language, port, *request);
        if (buildStatus.IsError()) {
            YRLOG_ERROR("{}|{}|BuildArgs failed for instanceID({}): {}", info.traceid(), info.requestid(),
                        info.instanceid(), buildStatus.RawMessage());
            ReportSandboxLifecycleStatus(info, runtimeID, SandboxLifecycleStatus::ABNORMAL);
            stateManager_.Unregister(runtimeID);
            return GenFailStartInstanceResponse(request, buildStatus.StatusCode(), buildStatus.GetMessage());
        }
        cmdArgs = std::move(builtCmdArgs);
    }

    RuntimeFeatures features;
    features.cleanStreamProducerEnable = config_.cleanStreamProducerEnable;
    Envs envs = GenerateEnvs(config_, request, port, cardIDs, features);

    // Create a placeholder future for dedup; will be replaced by the real future
    litebus::Promise<messages::StartInstanceResponse> promise;
    auto guard = std::make_shared<SandboxStartGuard>(stateManager_, runtimeID, promise.GetFuture());

    YRLOG_INFO("{}|{}|StartInstance: route to {}", info.traceid(), info.requestid(),
               isWarmUp ? "WarmUp" : (info.snapshotinfo().checkpointid().empty() ? "Normal" : "Restore"));

    litebus::Future<messages::StartInstanceResponse> future;
    if (isWarmUp) {
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
                    const std::string hostPort = std::to_string(hostPorts[i]);
                    const std::string containerPort = std::to_string(forwardConfigs[i].containerPort);
                    const std::string &scheme = forwardConfigs[i].protocol;
                    // Send L4 protocol to downstream sandboxd (http/https/ws/wss -> tcp, original tcp unchanged), otherwise sandboxd rejects it.
                    params.portMappings.push_back(ToDownstreamL4Protocol(scheme) + ":" + hostPort + ":" + containerPort);
                    portJson.push_back(FormatPortForwardMapping({
                        forwardConfigs[i].routeKind, scheme, static_cast<uint16_t>(hostPorts[i]),
                        static_cast<uint16_t>(forwardConfigs[i].containerPort), false}));
                }
                stateManager_.UpdatePortMappings(params.runtimeID, portJson.dump());
            }
        }
    }

    auto [status, startReq] = builder.Build(params);
    if (!status.IsOk()) {
        ReportSandboxLifecycleStatus(request->runtimeinstanceinfo(), params.runtimeID, SandboxLifecycleStatus::ABNORMAL);
        stateManager_.UpdatePortMappings(params.runtimeID, "");
        PortManager::GetInstance().ReleasePorts(params.runtimeID);
        return GenFailStartInstanceResponse(request, status.StatusCode(), status.RawMessage());
    }
    StartSandboxCreateSpan(request);
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
    StopSandboxCreateSpan(request, response);

    if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("{}|{}|StartNormal failed for instance({}) runtime({}): {}", info.traceid(), info.requestid(),
                    info.instanceid(), runtimeID, response.message());
        ReportSandboxLifecycleStatus(info, runtimeID, SandboxLifecycleStatus::ABNORMAL);
        // guard destructor rolls back state
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_CREATE_EXEC_FAILED, response.message());
    }

    const std::string sandboxID = response.id();
    stateManager_.UpdateSandboxID(runtimeID, sandboxID);
    guard->Commit();

    sandboxStatsPollingRuntimes_.insert(runtimeID);
    CollectSandboxStats(runtimeID, sandboxID);
    DoWaitWithRetry(sandboxID, runtimeID, 0);
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

    for (const auto &cmd : BuildBootstrapCommands(request)) {
        *warmup->add_command() = cmd;
    }
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

    // YR_LANGUAGE follows the service runtime field. The container runtime is
    // the sandbox backend (for example runc/runsc), not the user runtime.
    (*warmup->mutable_runtimeenvs())["YR_LANGUAGE"] = ResolveRuntimeLanguage(info);

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

    return ckptOrch_->DownloadForRestore(snapshotInfo.checkpointid(), snapshotInfo.storage(), info.requestid())
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

    return ckptOrch_->AddRef(checkpointID, runtimeID, info.requestid())
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
        ReportSandboxLifecycleStatus(info, info.runtimeid(), SandboxLifecycleStatus::ABNORMAL);
        return GenFailStartInstanceResponse(request, StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED,
                                            "add checkpoint reference failed: " + refStatus.RawMessage());
    }

    SandboxRequestBuilder builder{cmdBuilder_};
    SandboxStartParams params;
    params.request      = request;
    params.cmdArgs      = cmdArgs;
    params.envs         = envs;
    params.runtimeID    = info.runtimeid();
    params.checkpointID = checkpointPath;  // populates StartRequest.ckpt_dir

    auto [status, startReq] = builder.Build(params);
    if (!status.IsOk()) {
        ReportSandboxLifecycleStatus(info, info.runtimeid(), SandboxLifecycleStatus::ABNORMAL);
        // Compensate: remove the reference we just added
        ckptOrch_->ReleaseRef(info.runtimeid(), info.requestid());
        return GenFailStartInstanceResponse(request, status.StatusCode(), status.RawMessage());
    }
    StartSandboxCreateSpan(request);
    return DoStart(request, startReq)
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnRestoreDone, std::placeholders::_1, request, guard));
}

litebus::Future<messages::StartInstanceResponse> SandboxExecutor::OnRestoreDone(
    const runtime::v1::StartResponse &response,
    const std::shared_ptr<messages::StartInstanceRequest> &request,
    std::shared_ptr<SandboxStartGuard> guard)
{
    const auto &info = request->runtimeinstanceinfo();
    StopSandboxCreateSpan(request, response);
    if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("{}|{}|restore failed for runtime({}): {}", info.traceid(), info.requestid(),
                    info.runtimeid(), response.message());
        ReportSandboxLifecycleStatus(info, info.runtimeid(), SandboxLifecycleStatus::ABNORMAL);
        // Compensate: release the checkpoint ref we added before start
        ckptOrch_->ReleaseRef(info.runtimeid(), info.requestid());
        // guard destructor rolls back state
        return GenFailStartInstanceResponse(request, RUNTIME_MANAGER_CREATE_EXEC_FAILED, response.message());
    }
    const std::string sandboxID = response.id();
    stateManager_.UpdateSandboxID(info.runtimeid(), sandboxID);
    guard->Commit();
    sandboxStatsPollingRuntimes_.insert(info.runtimeid());
    CollectSandboxStats(info.runtimeid(), sandboxID);
    DoWaitWithRetry(sandboxID, info.runtimeid(), 0);
    ReportMetrics(info.instanceid(), info.runtimeid(), sandboxID,
                  {"yr_app_instance_start_time", " start timestamp", "ms"});
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
    ckptOrch_->ReleaseRef(runtimeID, requestID);

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
    userInitiatedTerminateRuntimes_.insert(runtimeID);
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
        auto lifecycleIt = sandboxLifecycleStates_.find(runtimeID);
        if (lifecycleIt == sandboxLifecycleStates_.end()
            || lifecycleIt->second != SandboxLifecycleStatus::ABNORMAL) {
            ReportSandboxLifecycleStatus(info->instanceInfo, runtimeID, SandboxLifecycleStatus::COMPLETED);
        }
        ReportMetrics(info->instanceInfo.instanceid(), runtimeID, sandboxID,
                      {"yr_instance_stop_time", "stop timestamp", "num"});
    }

    ClearSandboxMetricsState(runtimeID);
    sandboxLifecycleStates_.erase(runtimeID);
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
    return ckptOrch_->TakeSnapshot(request);
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

std::shared_ptr<litebus::Exec> SandboxExecutor::GetExecByRuntimeID(const std::string &runtimeID)
{
    // Check base class map first (for runtimes started in this process lifetime)
    if (auto it = runtime2Exec_.find(runtimeID); it != runtime2Exec_.end()) {
        return it->second;
    }
    // For runtimes restored via reconciliation, stateManager_ knows them but
    // runtime2Exec_ does not. Return nullptr — the health-check stop path
    // handles nullptr gracefully, and StopInstance uses stateManager_ directly.
    if (stateManager_.IsActive(runtimeID)) {
        YRLOG_INFO("GetExecByRuntimeID: runtime({}) found in stateManager (reconciled), no local exec", runtimeID);
        return nullptr;
    }
    YRLOG_ERROR("can not find exec by runtimeID: {}", runtimeID);
    return nullptr;
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

// ── Runtime reconciliation ────────────────────────────────────────────────────

litebus::Future<runtime::v1::ListContainersResponse> SandboxExecutor::DoList()
{
    ASSERT_IF_NULL(containerd_);
    auto req = std::make_shared<runtime::v1::ListContainersRequest>();
    auto resp = std::make_shared<runtime::v1::ListContainersResponse>();
    return containerd_
        ->CallAsyncX("List", *req, resp.get(),
                     &runtime::v1::RuntimeLauncher::Stub::AsyncList)
        .Then([resp](const Status &status) -> litebus::Future<runtime::v1::ListContainersResponse> {
            if (status.IsOk()) {
                return *resp;
            }
            YRLOG_ERROR("List gRPC failed: {}", status.RawMessage());
            return runtime::v1::ListContainersResponse{};
        });
}

litebus::Future<messages::ReconcileRuntimesResponse> SandboxExecutor::ReconcileRuntimes(
    const std::shared_ptr<messages::ReconcileRuntimesRequest> &request)
{
    if (!request) {
        messages::ReconcileRuntimesResponse resp;
        resp.set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
        resp.set_message("request is null");
        return resp;
    }
    if (!containerd_) {
        // No CONTAINER_EP configured — return empty success (nothing to reconcile)
        messages::ReconcileRuntimesResponse resp;
        resp.set_requestid(request->requestid());
        resp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
        return resp;
    }

    auto promise = std::make_shared<litebus::Promise<messages::ReconcileRuntimesResponse>>();
    WaitAndReconcile(request, 0, promise);
    return promise->GetFuture();
}

void SandboxExecutor::WaitAndReconcile(
    const std::shared_ptr<messages::ReconcileRuntimesRequest> &request, int32_t retryCount,
    const std::shared_ptr<litebus::Promise<messages::ReconcileRuntimesResponse>> &promise)
{
    if (!containerd_->IsConnected()) {
        if (retryCount >= RECONCILE_MAX_RETRIES) {
            YRLOG_ERROR("{}|ReconcileRuntimes: containerd still not connected after {} retries, returning error",
                        request->requestid(), retryCount);
            messages::ReconcileRuntimesResponse errResp;
            errResp.set_requestid(request->requestid());
            errResp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
            errResp.set_message("containerd not connected after max retries");
            promise->SetValue(errResp);
            return;
        }

        YRLOG_INFO("{}|ReconcileRuntimes: containerd not connected yet, retry {}/{}",
                   request->requestid(), retryCount + 1, RECONCILE_MAX_RETRIES);
        // Exponential backoff capped at RECONCILE_RETRY_MAX_INTERVAL_MS.
        int64_t delayMs = RECONCILE_RETRY_INITIAL_MS << std::min(retryCount, 6);
        if (delayMs > RECONCILE_RETRY_MAX_INTERVAL_MS || delayMs <= 0) {
            delayMs = RECONCILE_RETRY_MAX_INTERVAL_MS;
        }
        litebus::AsyncAfter(delayMs, GetAID(),
                            &SandboxExecutor::WaitAndReconcile, request, retryCount + 1, promise);
        return;
    }

    auto req = std::make_shared<runtime::v1::ListContainersRequest>();
    auto resp = std::make_shared<runtime::v1::ListContainersResponse>();
    auto resultFuture = containerd_
        ->CallAsyncX("List", *req, resp.get(),
                     &runtime::v1::RuntimeLauncher::Stub::AsyncList)
        .Then([aid = GetAID(), request, resp](const Status &status)
                  -> litebus::Future<messages::ReconcileRuntimesResponse> {
            if (!status.IsOk()) {
                YRLOG_ERROR("{}|ReconcileRuntimes: DoList gRPC failed: {}",
                            request->requestid(), status.RawMessage());
                messages::ReconcileRuntimesResponse errResp;
                errResp.set_requestid(request->requestid());
                errResp.set_code(static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR));
                errResp.set_message("DoList gRPC failed: " + status.RawMessage());
                return errResp;
            }
            return litebus::Async(aid, &SandboxExecutor::OnReconcileRuntimes, request, resp);
        });
    promise->Associate(resultFuture);
}

messages::ReconcileRuntimesResponse SandboxExecutor::OnReconcileRuntimes(
    const std::shared_ptr<messages::ReconcileRuntimesRequest> &request,
    const std::shared_ptr<runtime::v1::ListContainersResponse> &listResp)
{
    messages::ReconcileRuntimesResponse response;
    response.set_requestid(request->requestid());
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));

    // Build expected containerID set from proxy's view
    std::unordered_set<std::string> expectedIDs;
    for (const auto &entry : request->entries()) {
        if (!entry.containerid().empty()) {
            expectedIDs.insert(entry.containerid());
        }
    }

    // Build actual running containerID set from sandboxd
    std::unordered_set<std::string> actualRunningIDs;
    for (const auto &container : listResp->containers()) {
        if (container.state() == runtime::v1::CONTAINER_RUNNING) {
            actualRunningIDs.insert(container.id());
        }
    }

    auto now = std::chrono::steady_clock::now();
    int32_t orphansCleaned = 0;

    CleanupExitedContainers(request->requestid(), listResp, &response, &orphansCleaned);
    CleanupOrphanContainers(request->requestid(), listResp, expectedIDs, now, &response,
                            &orphansCleaned, &actualRunningIDs);
    AddMissingAndConfirmedEntries(request, actualRunningIDs, &response);
    PurgeOrphanTracking(actualRunningIDs);

    response.set_orphanscleaned(orphansCleaned);
    YRLOG_INFO("{}|ReconcileRuntimes: {} orphans cleaned, {} missing, {} confirmed",
               request->requestid(), orphansCleaned, response.missingids_size(),
               response.confirmedentries_size());
    return response;
}

void SandboxExecutor::CleanupExitedContainers(const std::string &requestID,
                                              const std::shared_ptr<runtime::v1::ListContainersResponse> &listResp,
                                              messages::ReconcileRuntimesResponse *response,
                                              int32_t *orphansCleaned)
{
    for (const auto &container : listResp->containers()) {
        if (container.state() != runtime::v1::CONTAINER_EXITED) {
            continue;
        }
        const auto &containerID = container.id();
        YRLOG_INFO("{}|ReconcileRuntimes: deleting exited container {}", requestID, containerID);
        DeleteContainerAsync(containerID);
        orphanFirstSeen_.erase(containerID);
        response->add_orphanids(containerID);
        ++(*orphansCleaned);
    }
}

void SandboxExecutor::CleanupOrphanContainers(const std::string &requestID,
                                              const std::shared_ptr<runtime::v1::ListContainersResponse> &listResp,
                                              const std::unordered_set<std::string> &expectedIDs,
                                              const std::chrono::steady_clock::time_point &now,
                                              messages::ReconcileRuntimesResponse *response,
                                              int32_t *orphansCleaned,
                                              std::unordered_set<std::string> *actualRunningIDs)
{
    for (const auto &container : listResp->containers()) {
        if (container.state() != runtime::v1::CONTAINER_RUNNING) {
            continue;
        }
        const auto &containerID = container.id();

        if (expectedIDs.count(containerID) > 0) {
            if (orphanFirstSeen_.erase(containerID)) {
                YRLOG_INFO("{}|ReconcileRuntimes: orphan timer cleared for {} "
                           "(re-appeared in expected)",
                           requestID, containerID);
            }
            continue;
        }

        auto it = orphanFirstSeen_.find(containerID);
        if (it == orphanFirstSeen_.end()) {
            orphanFirstSeen_.emplace(containerID, now);
            YRLOG_INFO("{}|ReconcileRuntimes: orphan candidate container {} (first seen)", requestID, containerID);
            continue;
        }

        auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        if (elapsedSec < static_cast<int64_t>(orphanGracePeriodSec_)) {
            continue;
        }

        YRLOG_INFO("{}|ReconcileRuntimes: deleting orphan container {} (orphan for {}s)",
                   requestID, containerID, elapsedSec);
        CleanupLocalRuntimeStateForOrphan(requestID, containerID);
        DeleteContainerAsync(containerID);
        orphanFirstSeen_.erase(it);
        actualRunningIDs->erase(containerID);
        response->add_orphanids(containerID);
        ++(*orphansCleaned);
    }
}

void SandboxExecutor::AddMissingAndConfirmedEntries(
    const std::shared_ptr<messages::ReconcileRuntimesRequest> &request,
    const std::unordered_set<std::string> &actualRunningIDs,
    messages::ReconcileRuntimesResponse *response)
{
    for (const auto &entry : request->entries()) {
        if (!entry.containerid().empty() && actualRunningIDs.count(entry.containerid()) == 0) {
            response->add_missingids(entry.containerid());
        }
    }

    std::unordered_set<std::string> missingSet;
    for (const auto &id : response->missingids()) {
        missingSet.insert(id);
    }
    for (const auto &entry : request->entries()) {
        if (entry.containerid().empty() || actualRunningIDs.count(entry.containerid()) == 0 ||
            missingSet.count(entry.containerid()) > 0) {
            continue;
        }
        auto *confirmed = response->add_confirmedentries();
        confirmed->set_runtimeid(entry.runtimeid());
        confirmed->set_containerid(entry.containerid());
        confirmed->set_instanceid(entry.instanceid());
        if (!stateManager_.IsActive(entry.runtimeid())) {
            messages::RuntimeInstanceInfo instanceInfo;
            instanceInfo.set_instanceid(entry.instanceid());
            instanceInfo.set_runtimeid(entry.runtimeid());
            stateManager_.Register({entry.runtimeid(), entry.containerid(), {}, {}, instanceInfo});
            stateManager_.MarkStartDone(entry.runtimeid());
            DoWaitWithRetry(entry.containerid(), entry.runtimeid(), 0);
        }
    }
}

void SandboxExecutor::CleanupLocalRuntimeStateForOrphan(const std::string &requestID,
                                                        const std::string &containerID)
{
    const auto runtimeID = stateManager_.FindRuntimeIDBySandboxID(containerID);
    if (runtimeID.empty()) {
        return;
    }

    YRLOG_WARN("{}|ReconcileRuntimes: orphan container {} is still registered as runtime({}); "
               "releasing local runtime resources before orphan delete",
               requestID, containerID, runtimeID);

    if (ckptOrch_ != nullptr) {
        ckptOrch_->ReleaseRef(runtimeID, requestID);
    }
    PortManager::GetInstance().ReleasePorts(runtimeID);
    ClearSandboxMetricsState(runtimeID);
    sandboxLifecycleStates_.erase(runtimeID);
    stateManager_.Unregister(runtimeID);
}

void SandboxExecutor::PurgeOrphanTracking(const std::unordered_set<std::string> &actualRunningIDs)
{
    for (auto it = orphanFirstSeen_.begin(); it != orphanFirstSeen_.end();) {
        if (actualRunningIDs.count(it->first) == 0) {
            it = orphanFirstSeen_.erase(it);
        } else {
            ++it;
        }
    }
}

void SandboxExecutor::DeleteContainerAsync(const std::string &containerID)
{
    auto deleteReq = std::make_shared<runtime::v1::DeleteRequest>();
    deleteReq->set_id(containerID);
    deleteReq->set_timeout(CONTAINER_DELETE_TIMEOUT_SEC);
    // Capture container ID so we can re-arm orphan tracking on failure: the next
    // reconcile cycle will pick it up again as an orphan candidate and retry
    // after the grace period, instead of silently leaking the container.
    // Lambda captures only the actor AID; the completion is dispatched back to
    // the actor via litebus::Async so the member-state mutation (orphanFirstSeen_)
    // runs on the actor's own thread instead of the gRPC callback thread.
    containerd_
        ->CallAsync("Delete", *deleteReq, static_cast<runtime::v1::DeleteResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncDelete)
        .Then([aid(GetAID()), containerID](
                  litebus::Try<runtime::v1::DeleteResponse> rsp) -> litebus::Future<Status> {
            return litebus::Async(aid, &SandboxExecutor::OnDeleteContainerComplete, containerID, rsp);
        });
}

Status SandboxExecutor::OnDeleteContainerComplete(const std::string &containerID,
                                                  litebus::Try<runtime::v1::DeleteResponse> rsp)
{
    if (rsp.IsOK()) {
        YRLOG_INFO("DeleteContainerAsync: container({}) deleted", containerID);
        return Status::OK();
    }
    YRLOG_ERROR("DeleteContainerAsync: container({}) delete failed: {}, scheduling retry in ~{}s",
                containerID, rsp.GetErrorCode(), kOrphanDeleteRetryIntervalSec);
    // Re-arm the orphan timer so the delete is retried on the *next* reconcile
    // cycle rather than after a full grace period.  We set first-seen to
    // (now - gracePeriodSec + retryIntervalSec), which means elapsed will reach
    // gracePeriodSec after approximately retryIntervalSec seconds.
    auto retryFirstSeen = std::chrono::steady_clock::now() -
                          std::chrono::seconds(static_cast<int64_t>(orphanGracePeriodSec_)) +
                          std::chrono::seconds(static_cast<int64_t>(kOrphanDeleteRetryIntervalSec));
    orphanFirstSeen_.emplace(containerID, retryFirstSeen);
    return Status(StatusCode::ERR_INNER_SYSTEM_ERROR);
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
    // Discover running sandboxes and resume Wait for them
    DoList().Then([aid(GetAID())](const runtime::v1::ListContainersResponse &listResp) -> litebus::Future<Status> {
        int resumed = 0;
        for (const auto &container : listResp.containers()) {
            if (container.state() == runtime::v1::CONTAINER_RUNNING) {
                YRLOG_INFO("Sync: resume Wait for running sandbox({})", container.id());
                litebus::Async(aid, &SandboxExecutor::RestoreWait, container.id());
                ++resumed;
            }
        }
        YRLOG_INFO("Sync: resumed Wait for {} running sandboxes", resumed);
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

void SandboxExecutor::DoWait(
    const std::string &sandboxID, const std::string &runtimeID)
{
    ASSERT_IF_NULL(containerd_);
    auto req = std::make_shared<runtime::v1::WaitRequest>();
    req->set_id(sandboxID);
    auto resp = std::make_shared<runtime::v1::WaitResponse>();
    YRLOG_INFO("DoWait: sandbox({}) runtime({})", sandboxID, runtimeID);
    containerd_
        ->CallAsyncX("Wait", *req, resp.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncWait)
        .Then([req, resp, runtimeID, aid(GetAID())](const Status &status) -> litebus::Future<Status> {
            runtime::v1::WaitResponse response;
            if (status.IsOk()) {
                response = *resp;
            } else {
                auto msg = fmt::format("failed to wait sandbox {}, grpc err: {}", req->id(), status.RawMessage());
                YRLOG_ERROR("{}", msg);
                response.set_status(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                response.set_message(msg);
            }
            return litebus::Async(aid, &SandboxExecutor::OnWaitDone, runtimeID, response);
        });
}
void SandboxExecutor::RestoreWait(const std::string &sandboxID)
{
    DoWaitWithRetry(sandboxID, sandboxID, 0);
}

// ── Wait retry on sandboxd disconnection ──────────────────────────────────────

void SandboxExecutor::DoWaitWithRetry(const std::string &sandboxID, const std::string &runtimeID, int retryCount)
{
    ASSERT_IF_NULL(containerd_);
    auto req = std::make_shared<runtime::v1::WaitRequest>();
    req->set_id(sandboxID);
    auto resp = std::make_shared<runtime::v1::WaitResponse>();
    YRLOG_INFO("DoWaitWithRetry: sandbox({}) runtime({}) retry({})", sandboxID, runtimeID, retryCount);
    containerd_
        ->CallAsyncX("Wait", *req, resp.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncWait)
        .Then([req, resp, sandboxID, runtimeID, retryCount, aid(GetAID())]
              (const Status &status) -> litebus::Future<Status> {
            YRLOG_INFO("DoWaitWithRetry returned: sandbox({}) runtime({}) retry({}) statusCode({}) msg({})",
                       sandboxID, runtimeID, retryCount,
                       fmt::underlying(status.StatusCode()), status.RawMessage());
            if (status.IsOk()) {
                return litebus::Async(aid, &SandboxExecutor::OnWaitDone, runtimeID, *resp);
            }
            if (!SandboxExecutor::IsRetryableWaitError(status)) {
                runtime::v1::WaitResponse response;
                response.set_status(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
                response.set_message(fmt::format("failed to wait sandbox {}, non-retryable grpc err: {}",
                                                 req->id(), status.RawMessage()));
                return litebus::Async(aid, &SandboxExecutor::OnWaitDone, runtimeID, response);
            }
            if (retryCount >= WAIT_MAX_RETRIES) {
                return litebus::Async(aid, &SandboxExecutor::CleanupSandboxAfterMaxRetries, runtimeID, sandboxID);
            }
            YRLOG_WARN("DoWait failed for sandbox {}, retryable error ({}), will retry in {}ms (attempt {}/{})",
                       req->id(), status.RawMessage(), WAIT_RETRY_INTERVAL_MS, retryCount + 1, WAIT_MAX_RETRIES);
            (void)litebus::AsyncAfter(WAIT_RETRY_INTERVAL_MS, aid, &SandboxExecutor::DoWaitWithRetry,
                                      sandboxID, runtimeID, retryCount + 1);
            return Status::OK();
        });
}

void SandboxExecutor::ScheduleSandboxStatsCollection(const std::string &runtimeID, const std::string &sandboxID)
{
    if (!IsSandboxMetricsEnabled()) {
        return;
    }
    if (sandboxStatsPollingRuntimes_.count(runtimeID) == 0) {
        return;
    }
    if (stateManager_.GetSandboxID(runtimeID) != sandboxID) {
        return;
    }
    (void)litebus::AsyncAfter(SANDBOX_STATS_COLLECT_INTERVAL_MS, GetAID(),
                              &SandboxExecutor::CollectSandboxStats, runtimeID, sandboxID);
}

void SandboxExecutor::ScheduleRunningStatusHeartbeat(const std::string &runtimeID)
{
    if (!IsSandboxMetricsEnabled()) {
        return;
    }
    // Guard: only heartbeat while sandbox is still tracked as RUNNING
    auto it = sandboxLifecycleStates_.find(runtimeID);
    if (it == sandboxLifecycleStates_.end() || it->second != SandboxLifecycleStatus::RUNNING) {
        return;
    }
    (void)litebus::AsyncAfter(SANDBOX_RUNNING_HEARTBEAT_INTERVAL_MS, GetAID(),
                              &SandboxExecutor::ReportRunningStatusHeartbeat, runtimeID);
}

void SandboxExecutor::ReportRunningStatusHeartbeat(const std::string &runtimeID)
{
    auto infoOpt = stateManager_.Find(runtimeID);
    if (!infoOpt.has_value()) {
        return;
    }
    auto stateIt = sandboxLifecycleStates_.find(runtimeID);
    if (stateIt == sandboxLifecycleStates_.end() || stateIt->second != SandboxLifecycleStatus::RUNNING) {
        return;  // sandbox already in terminal state, stop heartbeat
    }
    ReportSandboxLifecycleStatus(infoOpt->instanceInfo, runtimeID, SandboxLifecycleStatus::RUNNING);
    ScheduleRunningStatusHeartbeat(runtimeID);
}

void SandboxExecutor::CollectSandboxStats(const std::string &runtimeID, const std::string &sandboxID)
{
    if (!IsSandboxMetricsEnabled()) {
        return;
    }
    if (sandboxStatsPollingRuntimes_.count(runtimeID) == 0) {
        return;
    }
    if (sandboxID.empty() || stateManager_.GetSandboxID(runtimeID) != sandboxID) {
        return;
    }

    auto req = std::make_shared<runtime::v1::StatsRequest>();
    req->set_id(sandboxID);
    auto resp = std::make_shared<runtime::v1::StatsResponse>();
    auto collectedAt = std::chrono::steady_clock::now();

    ASSERT_IF_NULL(containerd_);
    containerd_
        ->CallAsyncX("Stats", *req, resp.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncStats)
        .Then([runtimeID, sandboxID, resp, collectedAt, aid(GetAID())]
              (const Status &status) -> litebus::Future<Status> {
            runtime::v1::StatsResponse statsResponse;
            if (status.IsOk()) {
                statsResponse = *resp;
            }
            return litebus::Async(aid, &SandboxExecutor::OnSandboxStatsCollected,
                                  runtimeID, sandboxID, status, statsResponse, collectedAt);
        });
}

litebus::Future<Status> SandboxExecutor::OnSandboxStatsCollected(
    const std::string &runtimeID, const std::string &sandboxID, const Status &status,
    const runtime::v1::StatsResponse &response, std::chrono::steady_clock::time_point collectedAt)
{
    if (sandboxStatsPollingRuntimes_.count(runtimeID) == 0) {
        return Status::OK();
    }
    if (sandboxID.empty() || stateManager_.GetSandboxID(runtimeID) != sandboxID) {
        return Status::OK();
    }

    if (!status.IsOk()) {
        YRLOG_WARN("OnSandboxStatsCollected: runtime({}) sandbox({}) stats failed: {}",
                   runtimeID, sandboxID, status.RawMessage());
        ScheduleSandboxStatsCollection(runtimeID, sandboxID);
        return Status::OK();
    }

    if (auto info = stateManager_.Find(runtimeID)) {
        ReportSandboxUsageMetrics(info->instanceInfo, runtimeID, response, collectedAt);
    }
    ScheduleSandboxStatsCollection(runtimeID, sandboxID);
    return Status::OK();
}

litebus::Future<Status> SandboxExecutor::CleanupSandboxAfterMaxRetries(const std::string &runtimeID,
                                                                       const std::string &sandboxID)
{
    auto info = stateManager_.Find(runtimeID);
    if (!info.has_value()) {
        YRLOG_WARN("CleanupSandboxAfterMaxRetries: runtime({}) already unregistered", runtimeID);
        return Status::OK();
    }

    const auto &instanceID = info->instanceInfo.instanceid();
    auto requestID = litebus::os::Join("update-instance-status-request", runtimeID, '-');

    auto msg = fmt::format("Sandbox {} wait failed after {} retries, marking instance fatal",
                           sandboxID, WAIT_MAX_RETRIES);
    YRLOG_ERROR("{}|{}", requestID, msg);

    ReportSandboxLifecycleStatus(info->instanceInfo, runtimeID, SandboxLifecycleStatus::ABNORMAL);
    ClearSandboxMetricsState(runtimeID);
    sandboxLifecycleStates_.erase(runtimeID);
    stateManager_.Unregister(runtimeID);

    return healthCheckClient_->NotifySandboxExit(
        instanceID, runtimeID, -1, msg, requestID);
}


litebus::Future<Status> SandboxExecutor::OnWaitDone(
    const std::string &runtimeID, const runtime::v1::WaitResponse &response)
{
    auto info = stateManager_.Find(runtimeID);
    if (!info.has_value()) {
        YRLOG_INFO("OnWaitDone: runtime({}) already unregistered, skip", runtimeID);
        return Status::OK();
    }

    const auto &instanceID = info->instanceInfo.instanceid();
    auto requestID = litebus::os::Join("update-instance-status-request", runtimeID, '-');

    YRLOG_INFO("{}|OnWaitDone: sandbox exited for runtime({}), exit_code({}), status({})",
               requestID, runtimeID, response.exit_code(), response.status());

    // If TerminateSandbox was already called (userInitiatedTerminateRuntimes_ contains runtimeID),
    // OnDeleteDone will handle lifecycle reporting, metrics cleanup, and Unregister.
    // Doing it here too would cause double-reporting and a spurious NotifySandboxExit to
    // function_proxy (which already initiated the eviction and does not expect this notification).
    if (userInitiatedTerminateRuntimes_.count(runtimeID) > 0) {
        YRLOG_INFO("{}|OnWaitDone: user-initiated terminate for runtime({}), defer to OnDeleteDone",
                   requestID, runtimeID);
        return Status::OK();
    }

    ReportSandboxLifecycleStatus(info->instanceInfo, runtimeID,
                                 IsNormalSandboxExit(response)
                                     ? SandboxLifecycleStatus::COMPLETED
                                     : SandboxLifecycleStatus::ABNORMAL);
    ClearSandboxMetricsState(runtimeID);

    return healthCheckClient_->NotifySandboxExit(
        instanceID, runtimeID, response.exit_code(), response.message(), requestID);
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
    DoReportMetrics(instanceID, runtimeID, sandboxID, title);
}

void SandboxExecutor::ReportSandboxLifecycleStatus(const messages::RuntimeInstanceInfo &info,
                                                  const std::string &runtimeID,
                                                  SandboxLifecycleStatus lifecycleStatus)
{
    sandboxLifecycleStates_[runtimeID] = lifecycleStatus;
    const auto sandboxID = stateManager_.GetSandboxID(runtimeID);

    if (lifecycleStatus == SandboxLifecycleStatus::RUNNING) {
        // Only record start time once; heartbeat re-fires RUNNING but must NOT reset the clock.
        sandboxRunningStartTimes_.emplace(runtimeID, std::chrono::steady_clock::now());
        // Start periodic heartbeat so Prometheus staleness clears stale RUNNING entries on cluster restart
        ScheduleRunningStatusHeartbeat(runtimeID);
    } else if (lifecycleStatus == SandboxLifecycleStatus::COMPLETED ||
               lifecycleStatus == SandboxLifecycleStatus::ABNORMAL) {
        auto startIt = sandboxRunningStartTimes_.find(runtimeID);
        if (startIt != sandboxRunningStartTimes_.end()) {
            const double durationSec = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startIt->second).count();
            ReportSandboxGauge(
                { functionsystem::metrics::YR_SANDBOX_LIFECYCLE_SECONDS,
                  "sandbox lifecycle duration in seconds", "s" },
                BuildSandboxMetricLabels(info, runtimeID, sandboxID), durationSec);
        }
    }

    ReportSandboxGauge(
        { functionsystem::metrics::YR_SANDBOX_LIFECYCLE_STATUS,
          "sandbox lifecycle status: 1-Creating, 2-Running, 3-Completed, 4-Abnormal",
          "enum" },
        BuildSandboxMetricLabels(info, runtimeID, sandboxID),
        static_cast<double>(lifecycleStatus));
}

void SandboxExecutor::ReportSandboxRequestedResources(const messages::RuntimeInstanceInfo &info,
                                                     const std::string &runtimeID)
{
    const auto sandboxID = stateManager_.GetSandboxID(runtimeID);
    const auto labels = BuildSandboxMetricLabels(info, runtimeID, sandboxID);
    const auto requested = GetSandboxRequestedResources(info);

    ReportSandboxGauge(
        { functionsystem::metrics::YR_SANDBOX_REQUESTED_CPU_CORES,
          "requested cpu limit for sandbox", "cores" },
        labels, requested.cpuCores);
    ReportSandboxGauge(
        { functionsystem::metrics::YR_SANDBOX_REQUESTED_MEMORY_BYTES,
          "requested memory limit for sandbox", "By" },
        labels, requested.memoryBytes);
}

void SandboxExecutor::ReportSandboxUsageMetrics(const messages::RuntimeInstanceInfo &info,
                                               const std::string &runtimeID,
                                               const runtime::v1::StatsResponse &response,
                                               std::chrono::steady_clock::time_point collectedAt)
{
    const auto sandboxID = stateManager_.GetSandboxID(runtimeID);
    auto labels = BuildSandboxMetricLabels(info, runtimeID, sandboxID);

    ReportSandboxGauge(
        { functionsystem::metrics::YR_SANDBOX_MEMORY_USAGE_BYTES,
          "sandbox memory usage in bytes", "By" },
        labels, static_cast<double>(response.memory_usage_bytes()));
    ReportSandboxGauge(
        { functionsystem::metrics::YR_SANDBOX_MEMORY_LIMIT_BYTES,
          "sandbox memory limit in bytes", "By" },
        labels, static_cast<double>(response.memory_limit_bytes()));
    ReportSandboxGauge(
        { functionsystem::metrics::YR_SANDBOX_MEMORY_USAGE_RATIO,
          "sandbox memory usage ratio", "ratio" },
        labels,
        response.memory_limit_bytes() == 0
            ? 0.0
            : static_cast<double>(response.memory_usage_bytes()) /
                  static_cast<double>(response.memory_limit_bytes()));

    auto previousIt = sandboxStatsSnapshots_.find(runtimeID);
    if (previousIt != sandboxStatsSnapshots_.end()) {
        const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            collectedAt - previousIt->second.collectedAt).count();
        if (elapsedNs > 0 && response.cpu_usage_ns() >= previousIt->second.cpuUsageNs) {
            const double cpuUsageCores =
                static_cast<double>(response.cpu_usage_ns() - previousIt->second.cpuUsageNs) /
                static_cast<double>(elapsedNs);
            ReportSandboxGauge(
                { functionsystem::metrics::YR_SANDBOX_CPU_USAGE_CORES,
                  "sandbox cpu usage expressed as used cores", "cores" },
                labels, cpuUsageCores);
        }
    }

    sandboxStatsSnapshots_[runtimeID] = SandboxStatsSnapshot{ response.cpu_usage_ns(), collectedAt };

    ReportSandboxRequestedResources(info, runtimeID);
}

void SandboxExecutor::ClearSandboxMetricsState(const std::string &runtimeID)
{
    sandboxStatsSnapshots_.erase(runtimeID);
    sandboxStatsPollingRuntimes_.erase(runtimeID);
    userInitiatedTerminateRuntimes_.erase(runtimeID);
    sandboxRunningStartTimes_.erase(runtimeID);
}

void SandboxExecutor::DoReportMetrics(const std::string &instanceID, const std::string &runtimeID,
                                       const std::string &sandboxID,
                                       const functionsystem::metrics::MeterTitle &title)
{
    (void)instanceID;

    auto info = stateManager_.Find(runtimeID);
    if (!info.has_value()) {
        return;
    }

    auto labels = BuildSandboxMetricLabels(info->instanceInfo, runtimeID, sandboxID);
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ReportSandboxGauge(title, labels, static_cast<double>(nowMs));
    ReportSandboxRequestedResources(info->instanceInfo, runtimeID);

    if (title.name == "yr_app_instance_start_time") {
        ReportSandboxLifecycleStatus(info->instanceInfo, runtimeID, SandboxLifecycleStatus::RUNNING);
    } else if (title.name == "yr_instance_stop_time") {
        auto lifecycleIt = sandboxLifecycleStates_.find(runtimeID);
        if (lifecycleIt == sandboxLifecycleStates_.end()
            || lifecycleIt->second != SandboxLifecycleStatus::ABNORMAL) {
            ReportSandboxLifecycleStatus(info->instanceInfo, runtimeID, SandboxLifecycleStatus::COMPLETED);
        }
    }
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
            if (item.contains("routeKind")) {
                if (!item["routeKind"].is_string()) {
                    continue;
                }
                const auto routeKind = ParsePortRouteKind(item["routeKind"].get<std::string>());
                if (!routeKind.has_value()) {
                    YRLOG_WARN("ParseForwardPorts: unsupported routeKind '{}'", item["routeKind"].get<std::string>());
                    continue;
                }
                cfg.routeKind = *routeKind;
            }
            configs.push_back(cfg);
        }
    } catch (const std::exception &e) {
        YRLOG_WARN("ParseForwardPorts: {}", e.what());
    }
    return configs;
}

}  // namespace functionsystem::runtime_manager
