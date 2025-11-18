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

#include <async/future.hpp>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "async/option.hpp"
#include "busproxy/startup/busproxy_startup.h"
#include "common/aksk/aksk_util.h"
#include "common/constants/constants.h"
#include "common/explorer/explorer.h"
#include "common/explorer/explorer_actor.h"
#include "common/flags/flags.h"
#include "common/kube_client/kube_client.h"
#include "common/logs/logging.h"
#include "meta_store_client/meta_store_client.h"
#include "common/proto/pb/posix_pb.h"
#include "common/rpc/server/common_grpc_server.h"
#include "common/status/status.h"
#include "common/utils/certs_utils.h"
#include "common/utils/exec_utils.h"
#include "common/utils/files.h"
#include "common/utils/memory_optimizer.h"
#include "common/utils/module_driver.h"
#include "common/utils/module_switcher.h"
#include "common/utils/param_check.h"
#include "common/utils/sensitive_value.h"
#include "common/utils/ssl_config.h"
#include "common/utils/version.h"
#include "distribute_cache_client/ds_cache_client_impl.h"
#include "function_agent_manager/function_agent_mgr_actor.h"
#include "function_proxy/busproxy/invocation_handler/invocation_handler.h"
#include "function_proxy/common/common_driver/common_driver.h"
#include "function_proxy/common/observer/control_plane_observer/control_plane_observer.h"
#include "function_proxy/common/observer/data_plane_observer/data_plane_observer.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/security/server_credentials.h"
#include "local_scheduler/instance_control/posix_api_handler/posix_api_handler.h"
#include "local_scheduler/local_sched_driver.h"
#include "memory_monitor/memory_monitor.h"
#include "openssl/safestack.h"
#include "openssl/x509.h"
#include "utils/os_utils.hpp"

using namespace functionsystem;
using namespace functionsystem::local_scheduler;
using namespace functionsystem::function_proxy;
using litebus::Option;
namespace {
const std::string COMPONENT_NAME = COMPONENT_NAME_FUNCTION_PROXY;
const uint32_t MS_PER_SECOND = 1000;
const uint32_t DEFAULT_HEARTBEAT_TIMES = 12;
// litebus thread reserve for resource view
const int32_t RESERVE_THREAD = 1;

std::shared_ptr<litebus::Promise<bool>> stopSignal{ nullptr };
std::shared_ptr<functionsystem::ModuleSwitcher> g_functionProxySwitcher{ nullptr };
std::shared_ptr<BusproxyStartup> g_busproxyStartup{ nullptr };
std::shared_ptr<LocalSchedDriver> g_localSchedDriver{ nullptr };
std::shared_ptr<CommonDriver> g_commonDriver {nullptr};
std::shared_ptr<functionsystem::grpc::CommonGrpcServer> g_posixGrpcServer{ nullptr };
std::shared_ptr<KubeClient> g_kubeClient = nullptr;
std::atomic_bool g_isCentOS{ false };

void Stop(int signum)
{
    YRLOG_INFO("receive signal: {}", signum);
    if (g_isCentOS) {
        // Temporary workaround: core dump occurs when the system exits. Deleted after the logs function is merged.
        std::cout << "the operating system is CentOS and raise signal kill" << std::endl;
        raise(SIGKILL);
    }
    if (stopSignal->GetFuture().IsOK()) {
        return;
    }
    stopSignal->SetValue(true);
}

std::shared_ptr<::grpc::ServerCredentials> InitPosixGrpcServerSecureOption(const function_proxy::Flags &flags)
{
    // read from file
    std::string basePath = flags.GetSslBasePath();
    litebus::Option<SensitiveValue> password;
    const std::string caPath = litebus::os::Join(basePath, flags.GetSslRootFile());
    const std::string keyFilePath = litebus::os::Join(basePath, flags.GetSslKeyFile());
    SensitiveValue serverKey =
        GetSensitivePrivateKeyFromFile(keyFilePath, SensitiveValue());
    std::string serverCert = Read(litebus::os::Join(basePath, flags.GetSslCertFile()));
    std::string caCert = Read(caPath);
    if (serverKey.Empty() || serverCert.empty() || caCert.empty()) {
        YRLOG_ERROR("read ssl cert and key file failed!");
        return nullptr;
    }
    ::grpc::SslServerCredentialsOptions::PemKeyCertPair pemKeyCertPair;
    pemKeyCertPair.private_key = serverKey.GetData();
    pemKeyCertPair.cert_chain = serverCert;
    ::grpc::SslServerCredentialsOptions
        sslServerCredentialsOptions(GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
    sslServerCredentialsOptions.pem_key_cert_pairs.push_back(std::move(pemKeyCertPair));
    sslServerCredentialsOptions.pem_root_certs = caCert;
    return ::grpc::SslServerCredentials(sslServerCredentialsOptions);
}

bool CreateBusProxy(const function_proxy::Flags &flags)
{
    if (g_commonDriver == nullptr) {
        return false;
    }
    auto dataInterfaceClientMgrProxy = g_commonDriver->GetDataInterfaceClientManagerProxy();
    auto observer = g_commonDriver->GetObserverActor();
    auto metaStoreClient = g_commonDriver->GetMetaStoreClient();
    auto internalIAM = g_commonDriver->GetInternalIAM();
    auto metaStorageAccessor = g_commonDriver->GetMetaStorageAccessor();

    MemoryControlConfig memoryControlConfig;
    memoryControlConfig.enable = flags.GetInvokeLimitationEnable();
    if (memoryControlConfig.enable) {
        // check input value
        auto inputHighThreshold = flags.GetHighMemoryThreshold();
        auto inputLowThreshold = flags.GetLowMemoryThreshold();
        auto inputMsgThreshold = flags.GetMessageSizeThreshold();
        if (inputLowThreshold > 0 && inputHighThreshold < 1 && inputLowThreshold < inputHighThreshold) {
            memoryControlConfig.highMemoryThreshold = inputHighThreshold;
            memoryControlConfig.lowMemoryThreshold = inputLowThreshold;
        }
        if (inputMsgThreshold > 0 && inputMsgThreshold < MAXIMUM_BUSPROXY_MESSAGE_SIZE_THRESHOLD) {
            memoryControlConfig.msgSizeThreshold = inputMsgThreshold;
        }
    }
    auto memoryMonitor = std::make_shared<MemoryMonitor>(memoryControlConfig);

    auto dataPlaneObserver = std::make_shared<function_proxy::DataPlaneObserver>(observer);
    BusProxyStartParam busproxyStartParam{
        .nodeID = flags.GetNodeID(),
        .modelName = COMPONENT_NAME,
        .localAddress = flags.GetAddress(),
        .serviceTTL = flags.GetServiceTTL(),
        .dataInterfaceClientMgr = dataInterfaceClientMgrProxy,
        .dataPlaneObserver = dataPlaneObserver,
        .memoryMonitor = memoryMonitor,
        .internalIam = internalIAM,
        .isEnablePerf = flags.GetEnablePerf(),
        .unRegisterWhileStop = flags.UnRegisterWhileStop()
    };

    g_busproxyStartup = std::make_shared<BusproxyStartup>(std::move(busproxyStartParam), metaStorageAccessor);
    if (const auto status = g_busproxyStartup->Run(); status.IsError()) {
        YRLOG_ERROR("failed to start busproxy, errMsg: {}", status.ToString());
        g_functionProxySwitcher->SetStop();
        return false;
    }
    return true;
}

void InitSslOptionFromCertFile(const function_proxy::Flags &flags, LocalSchedStartParam &param)
{
    if (!flags.GetIsEnableServerMode() || !flags.GetSslEnable()) {
        return;
    }
    std::string certPath = flags.GetSslBasePath();
    std::string rootCertFile = certPath + "/" + flags.GetSslRootFile();
    std::string certFile = certPath + "/" + flags.GetSslCertFile();
    auto cert = GetCertFromFile(certFile);
    auto caCert = GetCertFromFile(rootCertFile);
    if (!cert || !caCert) {
        YRLOG_ERROR("failed to load certificate from {} or {}", certFile, rootCertFile);
        return;
    }
    STACK_OF(X509) *caCerts = sk_X509_new_null();
    (void)sk_X509_push(caCerts, caCert);
    param.serverRootCert = GetCa(caCerts);
    param.serverNameOverride = GetAltNameDNSFromCert(cert);
    sk_X509_free(caCerts);
    X509_free(caCert);
    X509_free(cert);
}

void InitPosixServerOption(const function_proxy::Flags &flags, LocalSchedStartParam &param)
{
    param.enableServerMode = flags.GetIsEnableServerMode();
    param.enableSSL = flags.GetSslEnable();
    if (!flags.GetIsEnableServerMode() || !flags.GetSslEnable()) {
        return;
    }
    YRLOG_INFO("load certificate from mounted secret file");
    InitSslOptionFromCertFile(flags, param);
}

std::shared_ptr<DSAuthConfig> InitDsAuthConfig(const function_proxy::Flags &flags)
{
    auto dsConfig = std::make_shared<DSAuthConfig>();
    dsConfig->isEnable = flags.GetCacheStorageAuthEnable();
    dsConfig->isRuntimeEnable = flags.GetRuntimeDsAuthEnable();
    dsConfig->isRuntimeEncryptEnable = flags.GetRuntimeDsEncryptEnable();
    dsConfig->type = flags.GetCacheStorageAuthType();
    if (flags.GetCacheStorageAuthAK().empty()) {
        auto envAk = litebus::os::GetEnv(litebus::os::LITEBUS_ACCESS_KEY);
        if (envAk.IsSome()) {
            dsConfig->ak = envAk.Get();
            YRLOG_INFO("get cache store ak from env");
        }
    } else {
        YRLOG_INFO("get cache store ak from flags");
        dsConfig->ak = flags.GetCacheStorageAuthAK();
    }
    std::string clientPublicKeyPath = litebus::os::Join(flags.GetCurveKeyPath(), flags.GetRuntimeDsClientPublicKey());
    std::string clientPrivateKeyPath = litebus::os::Join(flags.GetCurveKeyPath(), flags.GetRuntimeDsClientPrivateKey());
    std::string serverPublicKeyPath = litebus::os::Join(flags.GetCurveKeyPath(), flags.GetRuntimeDsServerPublicKey());
    auto clientPublicKey = litebus::SensitiveValue(Read(clientPublicKeyPath));
    auto clientPrivateKey = litebus::SensitiveValue(Read(clientPrivateKeyPath));
    auto serverPublicKey = litebus::SensitiveValue(Read(serverPublicKeyPath));
    if (!clientPublicKey.Empty()) {
        dsConfig->clientPublicKey = clientPublicKey;
    }
    if (!clientPrivateKey.Empty()) {
        dsConfig->clientPrivateKey = clientPrivateKey;
    }
    if (!serverPublicKey.Empty()) {
        dsConfig->serverPublicKey = serverPublicKey;
    }
    if (flags.GetCacheStorageAuthSK().empty()) {
        auto envSk = litebus::os::GetEnv(litebus::os::LITEBUS_SECRET_KEY);
        if (envSk.IsSome()) {
            dsConfig->sk = envSk.Get();
            YRLOG_INFO("get cache store sk from env");
        }
    } else {
        auto sk = flags.GetCacheStorageAuthSK();
        if (!sk.empty()) {
            dsConfig->sk = sk;
            YRLOG_INFO("get cache store sk from flags");
        }
    }
    return dsConfig;
}

LocalSchedStartParam InitLocalSchedParam(const function_proxy::Flags &flags,
                                         const std::shared_ptr<DSAuthConfig> &dsAuthConfig)
{
    auto controlInterfaceClientMgrProxy = g_commonDriver->GetControlInterfaceClientManagerProxy();
    auto observer = g_commonDriver->GetObserverActor();
    auto posixService = g_commonDriver->GetPosixService();
    auto internalIAM = g_commonDriver->GetInternalIAM();
    auto controlPlaneObserver = std::make_shared<function_proxy::ControlPlaneObserver>(observer);
    auto pingCycleMs = flags.GetSystemTimeout() / DEFAULT_HEARTBEAT_TIMES;
    auto pingTimeoutMs = flags.GetSystemTimeout() / 2;

    return LocalSchedStartParam{
        .nodeID = flags.GetNodeID(),
        .globalSchedulerAddress = flags.GetGlobalSchedulerAddress(),
        .schedulePolicy = flags.GetSchedulePolicy(),
        .metaStoreAddress = flags.GetMetaStoreAddress(),
        .ip = flags.GetIP(),
        .cacheStorageHost = flags.GetCacheStorageHost(),
        .grpcListenPort = flags.GetGrpcListenPort(),
        .serverRootCert = flags.GetSslRootFile(),
        .serverNameOverride = "",
        .runtimeHeartbeatEnable = flags.GetRuntimeHeartbeatEnable(),
        .runtimeMaxHeartbeatTimeoutTimes = flags.GetRuntimeMaxHeartbeatTimeoutTimes(),
        .runtimeHeartbeatTimeoutMS = flags.GetRuntimeHeartbeatTimeoutMS(),
        .runtimeInitCallTimeoutMS = flags.GetRuntimeInitCallTimeoutSeconds() * MS_PER_SECOND,
        .runtimeConnTimeoutSeconds = flags.GetRuntimeConnTimeoutSeconds(),
        .runtimeShutdownTimeoutSeconds = flags.GetRuntimeShutdownTimeoutSeconds(),
        .runtimeRecoverEnable = flags.GetRuntimeRecoverEnable(),
        .dsAuthConfig = dsAuthConfig,
        .funcAgentMgrParam = { .retryTimes = flags.GetFuncAgentMgrRetryTimes(),
                               .retryCycleMs = flags.GetFuncAgentMgrRetryCycleMs(),
                               .pingTimes = DEFAULT_HEARTBEAT_TIMES,
                               .pingCycleMs = pingCycleMs,
                               .enableTenantAffinity = flags.GetEnableTenantAffinity(),
                               .tenantPodReuseTimeWindow = flags.GetTenantPodReuseTimeWindow(),
                               .enableIpv4TenantIsolation = flags.GetEnableIpv4TenantIsolation(),
                               .enableForceDeletePod = flags.EnableForceDeletePod() },
        .localSchedSrvParam = { .nodeID = flags.GetNodeID(),
                                .globalSchedAddress = flags.GetGlobalSchedulerAddress(),
                                .isK8sEnabled = !flags.GetK8sBasePath().empty(),
                                .registerCycleMs = flags.GetServiceRegisterCycleMs(),
                                .pingTimeOutMs = pingTimeoutMs,
                                .updateResourceCycleMs = flags.GetServiceUpdateResourceCycleMs(),
                                .componentName = COMPONENT_NAME },
        .resourceViewActorParam = { .isLocal = true,
                                    .enableTenantAffinity = flags.GetEnableTenantAffinity(),
                                    .tenantPodReuseTimeWindow = flags.GetTenantPodReuseTimeWindow() },
        .controlInterfacePosixMgr = controlInterfaceClientMgrProxy,
        .controlPlaneObserver = controlPlaneObserver,
        .internalIAM = internalIAM,
        .maxGrepSize = flags.GetMaxGrpcSize(),
        .enableDriver = flags.GetEnableDriver(),
        .isPseudoDataPlane = flags.GetIsPseudoDataPlane(),
        .enableServerMode = flags.GetIsEnableServerMode(),
        .enableSSL = flags.GetSslEnable(),
        .dsHealthCheckInterval = flags.GetDsHealthyCheckInterval(),
        .maxDsHealthCheckTimes = flags.GetMaxDsHealthCheckTimes(),
        .limitResource = { .minCpu = flags.GetMinInstanceCpuSize(),
                           .minMemory = flags.GetMinInstanceMemorySize(),
                           .maxCpu = flags.GetMaxInstanceCpuSize(),
                           .maxMemory = flags.GetMaxInstanceMemorySize() },
        .enablePrintResourceView = flags.GetEnablePrintResourceView(),
        .posixGrpcServer = g_posixGrpcServer,
        .posixService = posixService,
        .creds = InitPosixGrpcServerSecureOption(flags),
        .posixPort = flags.GetGrpcListenPort(),
        .schedulePlugins = flags.GetSchedulePlugins(),
        .enableTenantAffinity = flags.GetEnableTenantAffinity(),
        .createLimitationEnable = flags.GetCreateLimitationEnable(),
        .tokenBucketCapacity = flags.GetTokenBucketCapacity(),
        .isMetaStoreEnabled = flags.GetEnableMetaStore(),
        .maxPriority = flags.GetMaxPriority(),
        .aggregatedStrategy_ = flags.GetAggregatedStrategy(),
        .enablePreemption = flags.GetEnablePreemption(),
        .isPartialWatchInstances = flags.IsPartialWatchInstances(),
        .distributedCacheClient = g_commonDriver->GetDistributedCacheClient(),
        .runtimeInstanceDebugEnable = flags.IsRuntimeInstanceDebugEnable(),
        .unRegisterWhileStop = flags.UnRegisterWhileStop()
    };
}

Status InitLocalSchedulerDriver(const function_proxy::Flags &flags, const std::shared_ptr<DSAuthConfig> &dsAuthConfig)
{
    if (g_commonDriver == nullptr) {
        return Status(StatusCode::FAILED, "common is not initialized, failed to init local sched");
    }
    auto metaStoreClient = g_commonDriver->GetMetaStoreClient();
    auto localSchedStartParam = InitLocalSchedParam(flags, dsAuthConfig);
    InitPosixServerOption(flags, localSchedStartParam);
    g_localSchedDriver = std::make_shared<LocalSchedDriver>(std::move(localSchedStartParam), metaStoreClient);
    return Status::OK();
}

bool SetSSLConfig(const function_proxy::Flags &flags)
{
    // set k8s certificates
    g_kubeClient = KubeClient::CreateKubeClient(flags.GetK8sBasePath(), KubeClient::ClusterSslConfig("", "", false));
    auto sslCertConfig = GetSSLCertConfig(flags);
    if (flags.GetSslEnable()) {
        if (!InitLitebusSSLEnv(sslCertConfig).IsOk()) {
            YRLOG_ERROR("failed to init litebus ssl env");
            return false;
        }
    }
    g_functionProxySwitcher->InitMetrics(flags.GetEnableMetrics(), flags.GetMetricsConfig(),
                                         flags.GetMetricsConfigFile(), sslCertConfig);
    return true;
}

Status InitCommonDriver(const function_proxy::Flags &flags, const std::shared_ptr<DSAuthConfig> &dsAuthConfig)
{
    g_commonDriver = std::make_shared<CommonDriver>(flags, dsAuthConfig);
    return g_commonDriver->Init();
}

Status InitMasterExplorer(const function_proxy::Flags &flags, const std::shared_ptr<MetaStoreClient> &metaStoreClient,
                          const std::shared_ptr<KubeClient> &kubeClient)
{
    auto leaderName = flags.GetElectionMode() == K8S_ELECTION_MODE ? explorer::FUNCTION_MASTER_K8S_LEASE_NAME
                                                                   : explorer::DEFAULT_MASTER_ELECTION_KEY;
    explorer::LeaderInfo leaderInfo{ .name = leaderName,
                                     .address = flags.GetGlobalSchedulerAddress() };
    explorer::ElectionInfo electionInfo{ .identity = flags.GetIP(),
                                         .mode = flags.GetElectionMode(),
                                         .electKeepAliveInterval = flags.GetElectKeepAliveInterval() };
    if (!explorer::Explorer::CreateExplorer(electionInfo, leaderInfo, metaStoreClient, kubeClient,
                                            flags.GetK8sNamespace())) {
        return Status(StatusCode::FAILED, "failed to init master explorer");
    }
    if (flags.GetEnableMetaStore() && flags.GetElectionMode() == K8S_ELECTION_MODE) {
        explorer::Explorer::GetInstance().AddLeaderChangedCallback(
            "MetaStoreClientMgr", [metaStoreClient](const explorer::LeaderInfo &leaderInfo) {
                if (metaStoreClient != nullptr) {
                    metaStoreClient->UpdateMetaStoreAddress(leaderInfo.address);
                }
            });
    }
    return Status::OK();
}

void StartUpModule()
{
    if (const auto status = StartModule({g_commonDriver, g_localSchedDriver}); status.IsError()) {
        YRLOG_ERROR("failed to start function proxy, err: {}", status.ToString());
        g_functionProxySwitcher->SetStop();
        return;
    }

    if (const auto status = SyncModule({g_commonDriver, g_localSchedDriver}); status.IsError()) {
        YRLOG_ERROR("failed to sync function proxy, err: {}", status.ToString());
        g_functionProxySwitcher->SetStop();
        return;
    }

    if (const auto status = RecoverModule({g_commonDriver, g_localSchedDriver}); status.IsError()) {
        YRLOG_ERROR("failed to sync function proxy, err: {}", status.ToString());
        g_functionProxySwitcher->SetStop();
        return;
    }
    YRLOG_INFO("all modules are successful started, ready to serve");
    ModuleIsReady({g_commonDriver, g_localSchedDriver});
}

void OnCreate(const Flags &flags)
{
    YRLOG_INFO("{} is starting", COMPONENT_NAME);
    YRLOG_INFO("version:{} branch:{} commit_id:{}", BUILD_VERSION, GIT_BRANCH_NAME, GIT_HASH);

    if (!SetSSLConfig(flags)) {
        YRLOG_ERROR("failed to get sslConfig");
        g_functionProxySwitcher->SetStop();
        return;
    }
    if (!InitLitebusAKSKEnv(flags).IsOk()) {
        YRLOG_ERROR("failed to get aksk config");
        g_functionProxySwitcher->SetStop();
        return;
    }
    if (!g_functionProxySwitcher->InitLiteBus(flags.GetAddress(), flags.GetLitebusThreadNum() + RESERVE_THREAD)) {
        g_functionProxySwitcher->SetStop();
        return;
    }

    InvocationHandler::RegisterCreateCallResultReceiver(PosixAPIHandler::CallResult);
    const auto dsAuthConfig = InitDsAuthConfig(flags);
    if (const auto status = InitCommonDriver(flags, dsAuthConfig); status.IsError()) {
        YRLOG_ERROR("failed to init common, err: {}", status.ToString());
        g_functionProxySwitcher->SetStop();
        return;
    }

    if (const auto status = InitMasterExplorer(flags, g_commonDriver->GetMetaStoreClient(), g_kubeClient);
        status.IsError()) {
        YRLOG_ERROR("failed to init master explorer, err: {}", status.ToString());
        g_functionProxySwitcher->SetStop();
        return;
    }

    if (const auto status = InitLocalSchedulerDriver(flags, dsAuthConfig); status.IsError()) {
        YRLOG_ERROR("failed to init local scheduler, err: {}", status.ToString());
        g_functionProxySwitcher->SetStop();
        return;
    }

    if (!CreateBusProxy(flags)) {
        return;
    }

    StartUpModule();
}

void OnDestroy()
{
    YRLOG_INFO("{} is stopping", COMPONENT_NAME);

    (void)StopModule({g_localSchedDriver, g_commonDriver});

    if (g_busproxyStartup != nullptr) {
        (void)g_busproxyStartup->Stop();
    }

    AwaitModule({g_localSchedDriver, g_commonDriver});
    g_commonDriver = nullptr;
    g_localSchedDriver = nullptr;

    if (g_busproxyStartup != nullptr) {
        g_busproxyStartup->Await();
        g_busproxyStartup = nullptr;
        YRLOG_INFO("success to stop Busproxy");
    }

    InvocationHandler::StopMemoryMonitor();

    g_functionProxySwitcher->CleanMetrics();
    g_functionProxySwitcher->FinalizeLiteBus();
    g_functionProxySwitcher->StopLogger();
}

bool CheckFlags(const function_proxy::Flags &flags)
{
    if (!IsNodeIDValid(flags.GetNodeID())) {
        std::cerr << COMPONENT_NAME << " node id: " << flags.GetNodeID() << " is invalid." << std::endl;
        return false;
    }
    return true;
}
}  // namespace

int main(int argc, char **argv)
{
    g_isCentOS = IsCentos();

    Flags flags;

    if (const Option<std::string> parse = flags.ParseFlags(argc, argv); parse.IsSome()) {
        std::cerr << COMPONENT_NAME << " parse flag error, flags: " << parse.Get() << std::endl
                  << flags.Usage() << std::endl;
        return EXIT_COMMAND_MISUSE;
    }

    if (!CheckFlags(flags)) {
        return EXIT_COMMAND_MISUSE;
    }

    g_functionProxySwitcher = std::make_shared<ModuleSwitcher>(COMPONENT_NAME, flags.GetNodeID());
    if (!g_functionProxySwitcher->InitLogger(flags)) {
        return EXIT_ABNORMAL;
    }

    if (!g_functionProxySwitcher->RegisterHandler(Stop, stopSignal)) {
        return EXIT_ABNORMAL;
    }

    OnCreate(flags);

    auto memOpt = MemoryOptimizer();
    memOpt.StartTrimming();
    YRLOG_INFO("StartTrimming");

    g_functionProxySwitcher->WaitStop();

    OnDestroy();

    return 0;
}
