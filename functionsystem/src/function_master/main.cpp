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

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "async/async.hpp"
#include "async/future.hpp"
#include "async/option.hpp"
#include "common/aksk/aksk_util.h"
#include "common/constants/constants.h"
#include "common/explorer/explorer.h"
#include "common/explorer/explorer_actor.h"
#include "common/kube_client/health_monitor/health_monitor.h"
#include "common/kube_client/kube_client.h"
#include "common/leader/etcd_leader_actor.h"
#include "common/leader/k8s_leader_actor.h"
#include "common/leader/leader_actor.h"
#include "common/leader/txn_leader_actor.h"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "common/rpc/client/grpc_client.h"
#include "common/utils/memory_optimizer.h"
#include "common/utils/module_switcher.h"
#include "common/utils/param_check.h"
#include "common/utils/ssl_config.h"
#include "common/utils/version.h"
#include "flags/flags.h"
#include "global_scheduler/global_sched.h"
#include "global_scheduler/global_sched_driver.h"
#include "group_manager.h"
#include "group_manager_actor.h"
#include "instance_manager.h"
#include "instance_manager/instance_manager_driver.h"
#include "instance_manager_actor.h"
#include "litebus.hpp"
#include "meta_store_client/meta_store_client.h"
#include "meta_store_client/meta_store_struct.h"
#include "meta_store_driver.h"
#include "meta_store_monitor/meta_store_monitor.h"
#include "meta_store_monitor/meta_store_monitor_factory.h"
#include "resource_group_manager/resource_group_manager_driver.h"
#include "scaler/scaler_driver.h"
#include "system_function_loader/bootstrap_actor.h"
#include "system_function_loader/bootstrap_driver.h"
#include "utils/string_utils.hpp"
#include "utils/system_upgrade_switch_utils.h"

using namespace functionsystem;

namespace {
const std::string COMPONENT_NAME = COMPONENT_NAME_FUNCTION_MASTER;              // NOLINT
const std::string DEFAULT_META_STORE_ADDRESS = "127.0.0.1:32279";  // NOLINT
const std::string META_STORE_MODE_LOCAL = "local";                 // NOLINT

// litebus thread reserve for resource view
const int32_t RESERVE_THREAD = 2;
std::shared_ptr<litebus::Promise<bool>> stopSignal{ nullptr };
std::shared_ptr<functionsystem::ModuleSwitcher> g_functionMasterSwitcher{ nullptr };
std::shared_ptr<global_scheduler::GlobalSchedDriver> g_globalSchedDriver = nullptr;
std::shared_ptr<instance_manager::InstanceManagerDriver> g_instanceMgrDriver = nullptr;
std::shared_ptr<system_function_loader::BootstrapDriver> g_bootstrapDriver = nullptr;
std::shared_ptr<scaler::ScalerDriver> g_scalerDriver = nullptr;
std::shared_ptr<KubeClient> g_kubeClient = nullptr;
std::shared_ptr<functionsystem::leader::LeaderActor> g_leader{ nullptr };
ScalerHandlers g_handlers;
std::shared_ptr<meta_store::MetaStoreDriver> g_metaStoreDriver{ nullptr };
std::shared_ptr<instance_manager::InstanceManager> g_instanceMgr{ nullptr };
std::shared_ptr<resource_group_manager::ResourceGroupManagerDriver> g_resourceGroupManagerDriver = nullptr;

bool CheckFlags(const functionmaster::Flags &flags)
{
    if (!IsNodeIDValid(flags.GetNodeID())) {
        std::cerr << COMPONENT_NAME << " node id: " << flags.GetNodeID() << " is invalid." << std::endl;
        return false;
    }
    return true;
}

void Stop(int signum)
{
    YRLOG_INFO("receive signal: {}", signum);
    stopSignal->SetValue(true);
}

void OnDestroy()
{
    YRLOG_INFO("{} is stopping", COMPONENT_NAME);
    MetaStoreMonitorFactory::GetInstance().Clear();
    explorer::Explorer::GetInstance().Clear();
    if (g_leader != nullptr) {
        litebus::Terminate(g_leader->GetAID());
        litebus::Await(g_leader->GetAID());
        YRLOG_INFO("success to stop leader actor");
    }

    if (g_bootstrapDriver != nullptr && g_bootstrapDriver->Stop().IsOk()) {
        g_bootstrapDriver->Await();
        g_bootstrapDriver = nullptr;
        YRLOG_INFO("success to stop SysFuncLoader");
    } else {
        YRLOG_WARN("failed to stop SysFuncLoader");
    }

    if (g_resourceGroupManagerDriver != nullptr && g_resourceGroupManagerDriver->Stop().IsOk()) {
        g_resourceGroupManagerDriver->Await();
        g_resourceGroupManagerDriver = nullptr;
        YRLOG_INFO("success to stop ResourceGroupManager");
    } else {
        YRLOG_INFO("failed to stop ResourceGroupManager");
    }

    // global only can be stopped after bootstrap, because bootstrap needs global
    // to kill system function
    if (g_globalSchedDriver != nullptr && g_globalSchedDriver->Stop().IsOk()) {
        g_globalSchedDriver->Await();
        g_globalSchedDriver = nullptr;
        YRLOG_INFO("success to stop GlobalScheduler");
    } else {
        YRLOG_WARN("failed to stop GlobalScheduler");
    }

    // instance manager only can be stopped after bootstrap, because instances
    // might be taken over by instance manager, need to be killed by instance
    // manager
    if (g_instanceMgrDriver != nullptr) {
        if (g_instanceMgrDriver->Stop().IsOk()) {
            g_instanceMgrDriver->Await();
            g_instanceMgrDriver = nullptr;
            YRLOG_INFO("success to stop InstanceManger");
        } else {
            YRLOG_WARN("failed to stop InstanceManager");
        }
    }

    if (g_scalerDriver != nullptr) {
        if (g_scalerDriver->Stop().IsOk()) {
            g_scalerDriver->Await();
            g_scalerDriver = nullptr;
            YRLOG_INFO("success to stop Scaler");
        } else {
            YRLOG_WARN("failed to stop Scaler");
        }
    }

    g_functionMasterSwitcher->CleanMetrics();
    g_functionMasterSwitcher->FinalizeLiteBus();
    g_functionMasterSwitcher->StopLogger();

    if (g_metaStoreDriver != nullptr) {
        g_metaStoreDriver->Stop();
        g_metaStoreDriver->Await();
    }
}

bool GetRuntimeRecoverEnableFlag(const functionmaster::Flags &flags)
{
    if (const auto &address(flags.GetRuntimeRecoverEnable()); !address) {
        YRLOG_INFO("config recover runtime is disable");
        return false;
    }
    YRLOG_INFO("config recover runtime is enable");
    return true;
}

bool IsClientEnableMetaStore(const functionmaster::Flags &flags)
{
    return flags.GetEnableMetaStore();
}

bool CreateExplorer(const functionmaster::Flags &flags, const std::shared_ptr<MetaStoreClient> &metaClient)
{
    auto leaderName = flags.GetElectionMode() == K8S_ELECTION_MODE ? explorer::FUNCTION_MASTER_K8S_LEASE_NAME
                                                                   : explorer::DEFAULT_MASTER_ELECTION_KEY;
    explorer::LeaderInfo leaderInfo{ .name = leaderName, .address = flags.GetIP() };
    explorer::ElectionInfo electionInfo{ .identity = flags.GetIP(),
                                         .mode = flags.GetElectionMode(),
                                         .electKeepAliveInterval = flags.GetElectKeepAliveInterval() };
    if (!explorer::Explorer::CreateExplorer(electionInfo, leaderInfo, metaClient, g_kubeClient,
                                            flags.GetK8sNamespace())) {
        return false;
    }
    return true;
}

void CreateLeader(const functionmaster::Flags &flags, const std::shared_ptr<MetaStoreClient> &metaStoreClient)
{
    if (flags.GetElectionMode() == STANDALONE_MODE) {
        return;
    }

    auto renewInterval = flags.GetElectLeaseTTL() / 3;
    explorer::ElectionInfo electionInfo{ .identity = flags.GetIP(),
                                         .mode = flags.GetElectionMode(),
                                         .electKeepAliveInterval = flags.GetElectKeepAliveInterval(),
                                         .electLeaseTTL = flags.GetElectLeaseTTL(),
                                         .electRenewInterval = renewInterval };
    if (flags.GetElectionMode() == K8S_ELECTION_MODE) {
        g_leader = std::make_shared<leader::K8sLeaderActor>(explorer::FUNCTION_MASTER_K8S_LEASE_NAME, electionInfo,
                                                            g_kubeClient, flags.GetK8sNamespace());
        (void)litebus::Spawn(g_leader);
        litebus::Async(g_leader->GetAID(), &leader::LeaderActor::Elect);
    } else if (flags.GetElectionMode() == TXN_ELECTION_MODE) {
        g_leader = std::make_shared<leader::TxnLeaderActor>(explorer::DEFAULT_MASTER_ELECTION_KEY, electionInfo,
                                                            metaStoreClient);
        (void)litebus::Spawn(g_leader);
    } else {
        g_leader = std::make_shared<leader::EtcdLeaderActor>(explorer::DEFAULT_MASTER_ELECTION_KEY, electionInfo,
                                                             metaStoreClient);
        auto explorerActor = explorer::Explorer::GetInstance().GetExplorer(explorer::DEFAULT_MASTER_ELECTION_KEY);
        if (explorerActor != nullptr) {
            g_leader->RegisterPublishLeaderCallBack([aid(explorerActor->GetAID())](const explorer::LeaderInfo &leader) {
                litebus::Async(aid, &explorer::ExplorerActor::FastPublish, leader);
            });
        }
        (void)litebus::Spawn(g_leader);
        litebus::Async(g_leader->GetAID(), &leader::LeaderActor::Elect);
    }
}

bool CreateClient(const functionmaster::Flags &flags)
{
    // set k8s certificates
    HealthMonitorParam param = {
        .maxFailedTimes = flags.GetGetHealthMonitorMaxFailure(),
        .checkIntervalMs = flags.GetGetHealthMonitorRetryInterval(),
        .k8sInfo = flags.GetClusterID(),
    };
    
    g_kubeClient = KubeClient::CreateKubeClient(
        flags.GetK8sBasePath(),
        KubeClient::ClusterSslConfig(flags.GetK8sClientCertFile(), flags.GetK8sClientKeyFile(),
                                     flags.GetIsSkipTlsVerify()),
        true, param);
    g_kubeClient->InitOwnerReference(flags.GetK8sNamespace(), "function-master");
    return true;
}

bool SetSSLConfig(const functionmaster::Flags &flags)
{
    if (!CreateClient(flags)) {
        return false;
    }
    // set ssl mutual-auth certificates
    auto sslCertConfig = GetSSLCertConfig(flags);
    if (flags.GetSslEnable()) {
        if (!InitLitebusSSLEnv(sslCertConfig).IsOk()) {
            YRLOG_ERROR("failed to init litebus ssl env");
            return false;
        }
    }

    g_functionMasterSwitcher->InitMetrics(flags.GetEnableMetrics(), flags.GetMetricsConfig(),
                                          flags.GetMetricsConfigFile(), sslCertConfig);
    return true;
}

std::shared_ptr<MetaStoreClient> GetMetaStoreClient(const functionmaster::Flags &flags)
{
    MetaStoreTimeoutOption option;
    // retries must take longer than health check
    option.operationRetryTimes =
        static_cast<int64_t>((flags.GetMaxTolerateMetaStoreFailedTimes() + static_cast<uint32_t>(1))
                             * (flags.GetMetaStoreCheckInterval() + flags.GetMetaStoreCheckTimeout()))
        / KV_OPERATE_RETRY_INTERVAL_LOWER_BOUND;
    MetaStoreMonitorParam param{
        .maxTolerateFailedTimes = flags.GetMaxTolerateMetaStoreFailedTimes(),
        .checkIntervalMs = flags.GetMetaStoreCheckInterval(),
        .timeoutMs = flags.GetMetaStoreCheckTimeout(),
    };
    MetaStoreConfig metaStoreConfig;
    metaStoreConfig.enableMetaStore = IsClientEnableMetaStore(flags);
    metaStoreConfig.etcdTablePrefix = flags.GetETCDTablePrefix();
    metaStoreConfig.excludedKeys = flags.GetMetaStoreExcludedKeys();
    // if enabled, metastore address is master ip + global scheduler port; etcd
    // address is used for persistence else metastore address is etcd ip
    if (metaStoreConfig.enableMetaStore) {
        metaStoreConfig.etcdAddress = flags.GetEtcdAddress();
        metaStoreConfig.metaStoreAddress = flags.GetMetaStoreAddress();
    } else {
        metaStoreConfig.etcdAddress = flags.GetMetaStoreAddress();
    }

    auto metaClient = MetaStoreClient::Create(metaStoreConfig, GetGrpcSSLConfig(flags), option, true, param);
    if (metaClient == nullptr) {
        YRLOG_ERROR("failed to create meta store client");
        g_functionMasterSwitcher->SetStop();
    }
    return metaClient;
}

std::shared_ptr<MetaStoreClient> GetUpgradeWatchClient(const functionmaster::Flags &flags)
{
    if (!flags.GetSystemUpgradeWatchEnable()) {
        return nullptr;
    }

    MetaStoreTimeoutOption option;
    // retries must take longer than health check
    option.operationRetryTimes =
        static_cast<int64_t>((flags.GetMaxTolerateMetaStoreFailedTimes() + static_cast<uint32_t>(1))
                             * (flags.GetMetaStoreCheckInterval() + flags.GetMetaStoreCheckTimeout()))
        / KV_OPERATE_RETRY_INTERVAL_LOWER_BOUND;
    MetaStoreMonitorParam param{
        .maxTolerateFailedTimes = flags.GetMaxTolerateMetaStoreFailedTimes(),
        .checkIntervalMs = flags.GetMetaStoreCheckInterval(),
        .timeoutMs = flags.GetMetaStoreCheckTimeout(),
    };
    MetaStoreConfig metaStoreConfig;
    metaStoreConfig.etcdAddress = flags.GetSystemUpgradeWatchAddress();
    metaStoreConfig.etcdTablePrefix = flags.GetETCDTablePrefix();
    // upgrade watch client never enable meta store
    auto upgradeWatchClient = MetaStoreClient::Create(metaStoreConfig, GetGrpcSSLConfig(flags), option, true, param);
    if (upgradeWatchClient == nullptr) {
        YRLOG_ERROR("failed to create upgrade watch client");
        g_functionMasterSwitcher->SetStop();
    }
    return upgradeWatchClient;
}

bool InitGlobalSchedDriver(const functionmaster::Flags &flags, const std::shared_ptr<MetaStoreClient> &metaClient,
                           const std::shared_ptr<global_scheduler::GlobalSched> &globalSched)
{
    g_globalSchedDriver = std::make_shared<global_scheduler::GlobalSchedDriver>(globalSched, flags, metaClient);
    g_globalSchedDriver->BindComponentName(COMPONENT_NAME);
    if (g_globalSchedDriver == nullptr || !g_globalSchedDriver->Start().IsOk()) {
        YRLOG_ERROR("failed to start global-scheduler");
        g_functionMasterSwitcher->SetStop();
        return false;
    }
    return true;
}

bool InitInstanceManagerDriver(const functionmaster::Flags &flags, const std::shared_ptr<MetaStoreClient> &metaClient,
                               const std::shared_ptr<global_scheduler::GlobalSched> &globalSched,
                               const std::shared_ptr<MetaStoreMonitor> &metaStoreMonitor)
{
    auto groupMgrActor = std::make_shared<::instance_manager::GroupManagerActor>(metaClient, globalSched);
    auto groupManager = std::make_shared<::instance_manager::GroupManager>(groupMgrActor);
    auto instanceMgrActor = std::make_shared<::instance_manager::InstanceManagerActor>(
        metaClient, globalSched, groupManager,
        instance_manager::InstanceManagerStartParam{ .runtimeRecoverEnable = GetRuntimeRecoverEnableFlag(flags),
                                                     .isMetaStoreEnable = flags.GetEnableMetaStore(),
                                                     .servicesPath = flags.GetServicesPath(),
                                                     .libPath = flags.GetLibPath(),
                                                     .functionMetaPath = flags.GetFunctionMetaPath(),
                                                     .enableAbnormalDoubleCheck =
                                                         flags.GetEnableAbnormalDoubleCheck() });
    g_instanceMgr = std::make_shared<::instance_manager::InstanceManager>(instanceMgrActor);
    metaStoreMonitor->RegisterHealthyObserver(g_instanceMgr);
    groupMgrActor->BindInstanceManager(g_instanceMgr);

    g_instanceMgrDriver = std::make_shared<::instance_manager::InstanceManagerDriver>(instanceMgrActor, groupMgrActor);

    g_handlers.systemUpgradeHandler = [aid(instanceMgrActor->GetAID())](bool isUpgrading) {
        litebus::Async(aid, &instance_manager::InstanceManagerActor::HandleSystemUpgrade, isUpgrading);
    };
    g_handlers.localSchedFaultHandler = [aid(instanceMgrActor->GetAID())](const std::string &nodeName) {
        litebus::Async(aid, &instance_manager::InstanceManagerActor::OnLocalSchedFault, nodeName);
    };

    if (!g_instanceMgrDriver->Start().IsOk()) {
        YRLOG_ERROR("failed to start instance-manager");
        g_functionMasterSwitcher->SetStop();
        return false;
    }
    return true;
}

bool InitResourceGroupManager(const std::shared_ptr<MetaStoreClient> &metaClient,
                               const std::shared_ptr<global_scheduler::GlobalSched> &globalSched)
{
    auto resourceGroupManagerActor =
        std::make_shared<resource_group_manager::ResourceGroupManagerActor>(metaClient, globalSched);
    g_resourceGroupManagerDriver =
        std::make_shared<resource_group_manager::ResourceGroupManagerDriver>(resourceGroupManagerActor);
    if (!g_resourceGroupManagerDriver->Start().IsOk()) {
        YRLOG_ERROR("failed to start resource group manager");
        g_functionMasterSwitcher->SetStop();
        return false;
    }
    return true;
}

void StartScaler(const functionmaster::Flags &flags, const std::shared_ptr<MetaStoreClient> &metaClient,
                 const std::shared_ptr<global_scheduler::GlobalSched> &globalSched)
{
    g_handlers.evictAgentHandler = [sched(globalSched)](
                                       const std::string &localID,
                                       const std::shared_ptr<functionsystem::messages::EvictAgentRequest> &req) {
        return sched->EvictAgent(localID, req);
    };
    if (flags.GetEnableFrontendPool()) {
        litebus::AID bootstrapAID(SYSTEM_FUNCTION_LOADER_BOOTSTRAP_ACTOR, flags.GetIP());
        g_handlers.scaleUpSystemFuncHandler = [aid(bootstrapAID)](const std::string &funcName, uint32_t instanceNum) {
            litebus::Async(aid, &system_function_loader::BootstrapActor::ScaleByFunctionName, funcName, instanceNum);
        };
    }
    g_scalerDriver = std::make_shared<scaler::ScalerDriver>(flags, metaClient, GetUpgradeWatchClient(flags),
                                                            g_kubeClient, g_handlers);
    if (!g_scalerDriver->Start().IsOk()) {
        YRLOG_WARN("failed to start scaler");
        stopSignal->SetValue(true);
    }
}

void StartMetaStore(const functionmaster::Flags &flags)
{
    if (!flags.GetEnableMetaStore()) {
        return;
    }

    g_metaStoreDriver = std::make_shared<meta_store::MetaStoreDriver>();
    if (flags.GetMetaStoreMode() == META_STORE_MODE_LOCAL && !flags.GetEnablePersistence()) {
        YRLOG_INFO("enable local meta-store without persistence");
        g_metaStoreDriver->Start();
        return;
    }

    std::string etcdAddress = flags.GetEtcdAddress();
    if (etcdAddress.empty()) {
        YRLOG_WARN("etcd address is not specified, use default address: {}", DEFAULT_META_STORE_ADDRESS);
        etcdAddress = DEFAULT_META_STORE_ADDRESS;
    }

    MetaStoreTimeoutOption option;
    option.operationRetryTimes =
        static_cast<int64_t>((flags.GetMaxTolerateMetaStoreFailedTimes() + 1)
                             * (flags.GetMetaStoreCheckInterval() + flags.GetMetaStoreCheckTimeout())
                             / KV_OPERATE_RETRY_INTERVAL_LOWER_BOUND);
    MetaStoreMonitorParam param{
        .maxTolerateFailedTimes = flags.GetMaxTolerateMetaStoreFailedTimes(),
        .checkIntervalMs = flags.GetMetaStoreCheckInterval(),
        .timeoutMs = flags.GetMetaStoreCheckTimeout(),
    };

    if (flags.GetMetaStoreMode() == META_STORE_MODE_LOCAL && flags.GetEnablePersistence()) {
        YRLOG_INFO("enable local meta-store with persistence");
        g_metaStoreDriver->Start({
            etcdAddress, option, GetGrpcSSLConfig(flags),
            MetaStoreBackupOption{ .enableSyncSysFunc = flags.GetEnableSyncSysFunc(),
                                   .metaStoreMaxFlushConcurrency = flags.GetMetaStoreMaxFlushConcurrency(),
                                   .metaStoreMaxFlushBatchSize = flags.GetMetaStoreMaxFlushBatchSize() },
            flags.GetElectionMode() == K8S_ELECTION_MODE,
            param
    });
        return;
    }
}

std::string GetMonitorAddress(const functionmaster::Flags &flags)
{
    // if enabled, return master address; else return etcd address
    return flags.GetMetaStoreAddress();
}

void OnCreate(const functionmaster::Flags &flags)
{
    YRLOG_INFO("{} is starting", COMPONENT_NAME);
    YRLOG_INFO("version:{} branch:{} commit_id:{}", BUILD_VERSION, GIT_BRANCH_NAME, GIT_HASH);

    if (!SetSSLConfig(flags)) {
        YRLOG_ERROR("failed to get sslConfig");
        g_functionMasterSwitcher->SetStop();
        return;
    }
    if (!InitLitebusAKSKEnv(flags).IsOk()) {
        YRLOG_ERROR("failed to get aksk config");
        g_functionMasterSwitcher->SetStop();
        return;
    }
    if (!g_functionMasterSwitcher->InitLiteBus(flags.GetIP(), flags.GetLitebusThreadNum() + RESERVE_THREAD)) {
        YRLOG_ERROR("failed to init litebus.");
        g_functionMasterSwitcher->SetStop();
        return;
    }

    auto memOpt = MemoryOptimizer();
    memOpt.StartTrimming();

    // meta-store relay on k8s election
    if (flags.GetElectionMode() == K8S_ELECTION_MODE && !CreateExplorer(flags, nullptr)) {
        g_functionMasterSwitcher->SetStop();
        return;
    }

    StartMetaStore(flags);

    auto metaClient = GetMetaStoreClient(flags);
    if (metaClient == nullptr) {
        g_functionMasterSwitcher->SetStop();
        return;
    }
    auto metaStoreMonitor = MetaStoreMonitorFactory::GetInstance().GetMonitor(GetMonitorAddress(flags));
    if (metaStoreMonitor == nullptr || metaStoreMonitor->CheckMetaStoreConnected().IsError()) {
        g_functionMasterSwitcher->SetStop();
        return;
    }
    if (flags.GetElectionMode() != K8S_ELECTION_MODE && !CreateExplorer(flags, metaClient)) {
        g_functionMasterSwitcher->SetStop();
        return;
    }
    if (flags.GetEnableMetaStore() && flags.GetElectionMode() == K8S_ELECTION_MODE) {
        explorer::Explorer::GetInstance().AddLeaderChangedCallback(
            "MetaStoreClientMgr", [metaClient](const explorer::LeaderInfo &leaderInfo) {
                if (metaClient != nullptr) {
                    metaClient->UpdateMetaStoreAddress(leaderInfo.address);
                }
            });
    }

    CreateLeader(flags, metaClient);

    auto globalSched = std::make_shared<global_scheduler::GlobalSched>();
    metaStoreMonitor->RegisterHealthyObserver(globalSched);
    if (!InitGlobalSchedDriver(flags, metaClient, globalSched)) {
        return;
    }

    if (!InitInstanceManagerDriver(flags, metaClient, globalSched, metaStoreMonitor)) {
        return;
    }
    if (!InitResourceGroupManager(metaClient, globalSched)) {
        return;
    }

    system_function_loader::SystemFunctionLoaderStartParam param{ .globalSched = globalSched,
                                                                  .sysFuncRetryPeriod = flags.GetSysFuncRetryPeriod(),
                                                                  .sysFuncCustomArgs = flags.GetSysFuncCustomArgs(),
                                                                  .masterAddress = flags.GetIP(),
                                                                  .instanceMgr = g_instanceMgr,
                                                                  .enableFrontendPool = flags.GetEnableFrontendPool() };
    g_bootstrapDriver = std::make_shared<system_function_loader::BootstrapDriver>(param, metaClient);
    if (!g_bootstrapDriver->Start().IsOk()) {
        YRLOG_ERROR("failed to start system function loader");
        g_functionMasterSwitcher->SetStop();
        return;
    }

    StartScaler(flags, metaClient, globalSched);
    YRLOG_INFO("{} is started", COMPONENT_NAME);
}

}  // namespace

int main(int argc, char **argv)
{
    functionsystem::functionmaster::Flags flags;
    auto parse = flags.ParseFlags(argc, argv);
    if (parse.IsSome()) {
        std::cerr << COMPONENT_NAME << " parse flag error, flags: " << parse.Get() << std::endl
                  << flags.Usage() << std::endl;
        return EXIT_COMMAND_MISUSE;
    }

    if (!CheckFlags(flags)) {
        return EXIT_COMMAND_MISUSE;
    }

    g_functionMasterSwitcher = std::make_shared<functionsystem::ModuleSwitcher>(COMPONENT_NAME, flags.GetNodeID());
    if (!g_functionMasterSwitcher->InitLogger(flags)) {
        return EXIT_ABNORMAL;
    }

    // register handler
    if (!g_functionMasterSwitcher->RegisterHandler(Stop, stopSignal)) {
        return EXIT_ABNORMAL;
    }

    OnCreate(flags);

    g_functionMasterSwitcher->WaitStop();

    OnDestroy();
    return 0;
}