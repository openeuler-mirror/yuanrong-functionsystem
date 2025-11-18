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

#include "local_sched_driver.h"

#include "common/constants/actor_name.h"
#include "meta_store_monitor/meta_store_monitor_factory.h"
#include "common/utils/param_check.h"
#include "local_scheduler/bundle_manager/bundle_mgr_actor.h"
#include "local_scheduler/debug_instance_info_monitor/debug_instance_info_monitor.h"
#include "local_scheduler/instance_control/posix_api_handler/posix_api_handler.h"
#include "local_scheduler/local_group_ctrl/local_group_ctrl_actor.h"
#include "local_scheduler/grpc_server/bus_service/bus_service.h"

namespace functionsystem::local_scheduler {

const std::string LOCAL_SCHEDULER = "local-scheduler";

void LocalSchedDriver::SetRuntimeConfig(InstanceCtrlConfig &config)
{
    ASSERT_IF_NULL(param_.dsAuthConfig);
    RuntimeConfig runtimeConfig{
        .runtimeHeartbeatEnable = param_.runtimeHeartbeatEnable,
        .runtimeMaxHeartbeatTimeoutTimes = param_.runtimeMaxHeartbeatTimeoutTimes,
        .runtimeHeartbeatTimeoutMS = param_.runtimeHeartbeatTimeoutMS,
        .runtimeInitCallTimeoutMS = param_.runtimeInitCallTimeoutMS,
        .runtimeShutdownTimeoutSeconds = param_.runtimeShutdownTimeoutSeconds,
        .runtimeDsAuthEnable = param_.dsAuthConfig->isRuntimeEnable,
        .runtimeDsEncryptEnable = param_.dsAuthConfig->isRuntimeEncryptEnable,
        .dataSystemAccessKey = param_.dsAuthConfig->ak,
        .dataSystemSecurityKey = param_.dsAuthConfig->sk,
        .runtimeDsClientPublicKey = param_.dsAuthConfig->clientPublicKey,
        .runtimeDsClientPrivateKey = param_.dsAuthConfig->clientPrivateKey,
        .runtimeDsServerPublicKey = param_.dsAuthConfig->serverPublicKey,
    };
    YRLOG_INFO(
        "runtime heartbeat config: runtimeHeartbeatEnable: {}, runtimeMaxHeartbeatTimeoutTimes: {}, "
        "runtimeHeartbeatTimeoutMS: {}, runtimeRecoverEnable: {}, runtimeInitCallTimeoutMS:{}, "
        "runtimeShutdownTimeoutSeconds:{} ",
        param_.runtimeHeartbeatEnable, param_.runtimeMaxHeartbeatTimeoutTimes, param_.runtimeHeartbeatTimeoutMS,
        param_.runtimeRecoverEnable, param_.runtimeInitCallTimeoutMS, param_.runtimeShutdownTimeoutSeconds);

    config.runtimeConfig = runtimeConfig;
}

Status LocalSchedDriver::Create()
{
    resourceViewMgr_ = std::make_shared<resource_view::ResourceViewMgr>();
    resourceViewMgr_->Init(param_.nodeID, param_.resourceViewActorParam);

    localSchedSrv_ = LocalSchedSrv::Create(param_.localSchedSrvParam);

    funcAgentMgr_ = FunctionAgentMgr::Create(param_.nodeID, param_.funcAgentMgrParam, metaStoreClient_);

    abnormalProcessor_ = AbnormalProcessor::Create(param_.nodeID);

    rGroupCtrl_ = ResourceGroupCtrl::Init();

    InstanceCtrlConfig config{};
    SetRuntimeConfig(config);
    config.maxGrpcSize = param_.maxGrepSize;
    config.connectTimeout = param_.runtimeConnTimeoutSeconds;
    config.isPseudoDataPlane = param_.isPseudoDataPlane;
    config.cacheStorageHost = param_.cacheStorageHost;
    config.limitResource = {
        .minCpu = param_.limitResource.minCpu,
        .minMemory = param_.limitResource.minMemory,
        .maxCpu = param_.limitResource.maxCpu,
        .maxMemory = param_.limitResource.maxMemory,
    };
    config.enableServerMode = param_.enableServerMode;
    config.enableSSL = param_.enableSSL;
    config.serverRootCert = param_.serverRootCert;
    config.serverNameOverride = param_.serverNameOverride;
    config.posixPort = param_.posixPort;
    config.schedulePlugins = param_.schedulePlugins;
    config.enableTenantAffinity = param_.enableTenantAffinity;
    config.createLimitationEnable = param_.createLimitationEnable;
    config.tokenBucketCapacity = param_.tokenBucketCapacity;
    config.isMetaStoreEnabled = param_.isMetaStoreEnabled;
    config.isPartialWatchInstances = param_.isPartialWatchInstances;
    config.maxPriority = param_.maxPriority;
    config.enablePreemption = param_.enablePreemption;
    instanceCtrl_ = InstanceCtrl::Create(param_.nodeID, config);
    PosixAPIHandler::BindInstanceCtrl(instanceCtrl_);
    PosixAPIHandler::BindControlClientManager(param_.controlInterfacePosixMgr);
    PosixAPIHandler::BindLocalSchedSrv(localSchedSrv_);
    PosixAPIHandler::BindResourceGroupCtrl(rGroupCtrl_);
    PosixAPIHandler::SetMaxPriority(param_.maxPriority);

    subscriptionMgr_ = SubscriptionMgr::Init(param_.nodeID,
        SubscriptionMgrConfig{ .isPartialWatchInstances = param_.isPartialWatchInstances });
    subscriptionMgr_->BindInstanceCtrl(instanceCtrl_);
    subscriptionMgr_->BindLocalSchedSrv(localSchedSrv_);

    // create http server
    httpServer_ = std::make_shared<HttpServer>(LOCAL_SCHEDULER);
    apiRouteRegister_ = std::make_shared<DefaultHealthyRouter>(param_.nodeID);
    metaStoreHealthyObserver_ = std::make_shared<InstanceCtrlMetaStoreHealthyObserver>(instanceCtrl_);
    if (auto registerStatus(httpServer_->RegisterRoute(apiRouteRegister_)); registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("failed to register health check api router.");
    }
    return Status::OK();
}

std::string GetMonitorAddress(const LocalSchedStartParam &param)
{
    // if enabled, return master address; else return etcd address
    return param.metaStoreAddress;
}

Status LocalSchedDriver::Start()
{
    YRLOG_INFO(
        "start local scheduler driver, nodeID: {}, global scheduler address: {}, scheduler policy: {}, "
        "meta store address: {}, driver gateway service enable: {}, enablePrintResourceView: {}",
        param_.nodeID, param_.globalSchedulerAddress, param_.schedulePolicy, param_.metaStoreAddress,
        param_.enableDriver, param_.enablePrintResourceView);

    if (auto status(Create()); status != StatusCode::SUCCESS) {
        return status;
    }
    if (!CreatePosixAndDriverServer()) {
        YRLOG_ERROR("failed to start posix and driver server");
        return Status(StatusCode::FAILED);
    }
    BindInstanceCtrl();

    abnormalProcessor_->BindMetaStoreClient(metaStoreClient_);
    abnormalProcessor_->BindObserver(param_.controlPlaneObserver);
    abnormalProcessor_->BindInstanceCtrl(instanceCtrl_);
    abnormalProcessor_->BindRaiseWrapper(std::make_shared<RaiseWrapper>());
    abnormalProcessor_->BindFunctionAgentMgr(funcAgentMgr_);
    localSchedSrv_->Start(instanceCtrl_, resourceViewMgr_);
    funcAgentMgr_->Start(instanceCtrl_, resourceViewMgr_->GetInf(resource_view::ResourceType::PRIMARY));
    abnormalProcessor_->Start();
    localSchedSrv_->BindFunctionAgentMgr(funcAgentMgr_);
    localSchedSrv_->BindSubscriptionMgr(subscriptionMgr_);
    funcAgentMgr_->BindLocalSchedSrv(localSchedSrv_);

    BundleManagerActorParam bundleManagerActorParam = {
        .actorName = BUNDLE_MGR_ACTOR_NAME,
        .nodeId = param_.nodeID,
        .metaStoreClient = metaStoreClient_
    };
    auto bundleMgrActor = std::make_shared<BundleMgrActor>(bundleManagerActorParam);
    bundleMgr_ = std::make_shared<BundleMgr>(bundleMgrActor);
    bundleMgrActor->BindInstanceCtrl(instanceCtrl_);
    bundleMgrActor->BindLocalSchedSrv(localSchedSrv_);
    bundleMgrActor->BindResourceViewMgr(resourceViewMgr_);
    bundleMgrActor->BindScheduler(instanceCtrl_->GetScheduler());
    litebus::Spawn(bundleMgrActor);
    funcAgentMgr_->BindBundleMgr(bundleMgr_);

    param_.controlPlaneObserver->AttachTenantListener(funcAgentMgr_);
    auto localGroupCtrlActor =
        std::make_shared<LocalGroupCtrlActor>(LOCAL_GROUP_CTRL_ACTOR_NAME, param_.nodeID, metaStoreClient_);
    localGroupCtrl_ = std::make_shared<LocalGroupCtrl>(localGroupCtrlActor);
    localGroupCtrlActor->BindScheduler(instanceCtrl_->GetScheduler());
    localGroupCtrlActor->BindLocalSchedSrv(localSchedSrv_);
    localGroupCtrlActor->BindControlInterfaceClientManager(param_.controlInterfacePosixMgr);
    localGroupCtrlActor->BindInstanceCtrl(instanceCtrl_);
    localGroupCtrlActor->BindResourceView(resourceViewMgr_);
    PosixAPIHandler::BindLocalGroupCtrl(localGroupCtrl_);
    litebus::Spawn(localGroupCtrlActor);
    (void)litebus::Spawn(httpServer_);

    auto monitor = MetaStoreMonitorFactory::GetInstance().GetMonitor(GetMonitorAddress(param_));
    if (monitor != nullptr) {
        monitor->RegisterHealthyObserver(funcAgentMgr_);
        monitor->RegisterHealthyObserver(metaStoreHealthyObserver_);
        monitor->RegisterHealthyObserver(localGroupCtrl_);
    } else {
        YRLOG_WARN("failed to get monitor of address {}.", GetMonitorAddress(param_));
    }
    resourceViewMgr_->GetInf(resource_view::ResourceType::PRIMARY)
        ->RegisterUnitDisableFunc([localSchedSrv(localSchedSrv_)](const std::string &agentName) {
            localSchedSrv->DeletePod(agentName,
                                     "disable-agent-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(),
                                     "agent disabled");
        });

    if (param_.distributedCacheClient != nullptr && param_.distributedCacheClient->IsDsClientEnable()) {
        StartDsHealthyCheck();
    }
    if (param_.runtimeInstanceDebugEnable) {
        StartDebugInstanceInfoMonitor();
    }

    isStarted_ = true;
    return Status::OK();
}

Status LocalSchedDriver::Sync()
{
    auto status =
        ActorSync({ abnormalProcessor_, funcAgentMgr_, instanceCtrl_, localGroupCtrl_, localSchedSrv_, bundleMgr_ });
    if (status.IsError()) {
        return status;
    }
    YRLOG_INFO("successful to sync state of local scheduler");
    return Status::OK();
}

Status LocalSchedDriver::Recover()
{
    auto status =
        ActorRecover({ abnormalProcessor_, funcAgentMgr_, instanceCtrl_, localGroupCtrl_, localSchedSrv_, bundleMgr_ });
    if (status.IsError()) {
        return status;
    }
    YRLOG_INFO("successful to recover local scheduler");
    return Status::OK();
}

void LocalSchedDriver::ToReady()
{
    ActorReady({ abnormalProcessor_, funcAgentMgr_, instanceCtrl_, localGroupCtrl_, localSchedSrv_, bundleMgr_,
                 resourceViewMgr_->GetInf(resource_view::ResourceType::PRIMARY),
                 resourceViewMgr_->GetInf(resource_view::ResourceType::VIRTUAL) });
}

Status LocalSchedDriver::Stop()
{
    if (param_.unRegisterWhileStop && localSchedSrv_ != nullptr && isStarted_) {
        // block to wait instance & agent to be cleared
        (void)localSchedSrv_->GracefulShutdown().Get();
    }
    if (dsHealthyChecker_) {
        litebus::Terminate(dsHealthyChecker_->GetAID());
    }
    if (httpServer_) {
        litebus::Terminate(httpServer_->GetAID());
    }
    StopActor({ abnormalProcessor_, funcAgentMgr_, instanceCtrl_, localGroupCtrl_, localSchedSrv_, bundleMgr_ });
    return Status::OK();
}

void LocalSchedDriver::Await()
{
    if (dsHealthyChecker_) {
        litebus::Await(dsHealthyChecker_->GetAID());
    }
    if (httpServer_) {
        litebus::Await(httpServer_->GetAID());
    }
    AwaitActor({ abnormalProcessor_, funcAgentMgr_, instanceCtrl_, localGroupCtrl_, localSchedSrv_, bundleMgr_ });
}

void LocalSchedDriver::BindInstanceCtrl()
{
    instanceCtrl_->SetEnablePrintResourceView(param_.enablePrintResourceView);
    instanceCtrl_->Start(funcAgentMgr_, resourceViewMgr_, param_.controlPlaneObserver,
                         param_.aggregatedStrategy_, param_.maxPriority);
    instanceCtrl_->BindControlInterfaceClientManager(param_.controlInterfacePosixMgr);
    instanceCtrl_->BindMetaStoreClient(metaStoreClient_);
    instanceCtrl_->BindLocalSchedSrv(localSchedSrv_);
    instanceCtrl_->BindInternalIAM(param_.internalIAM);
    instanceCtrl_->BindResourceGroupCtrl(rGroupCtrl_);
    instanceCtrl_->BindSubscriptionMgr(subscriptionMgr_);
}

void LocalSchedDriver::StartDsHealthyCheck()
{
    YRLOG_INFO("enable ds healthy checker, check ds api with interval {} by max {} times", param_.dsHealthCheckInterval,
               param_.maxDsHealthCheckTimes);

    dsHealthyChecker_ = std::make_shared<DsHealthyChecker>(param_.dsHealthCheckInterval, param_.maxDsHealthCheckTimes,
                                                           param_.distributedCacheClient);
    dsHealthyChecker_->SubscribeDsHealthy(
        [localSchedSrv(localSchedSrv_), instanceCtrl(instanceCtrl_)](const bool isHealthy) {
            instanceCtrl->NotifyDsHealthy(isHealthy);  // evict instance on local node
            (void)localSchedSrv->NotifyDsHealthy(isHealthy);
        });
    (void) litebus::Spawn(dsHealthyChecker_);
}

void LocalSchedDriver::StartDebugInstanceInfoMonitor()
{
    YRLOG_INFO("enable debug_instance_info_monitor, check debug_instance_info with interval 3000ms");
    auto infoMonitor = std::make_shared<DebugInstanceInfoMonitor>(funcAgentMgr_, QUERY_DEBUG_INSTANCE_INFO_INTERVAL_MS);
    litebus::Spawn(infoMonitor);
    litebus::Async(infoMonitor->GetAID(), &DebugInstanceInfoMonitor::Start);
}

bool LocalSchedDriver::CreatePosixAndDriverServer()
{
    functionsystem::grpc::CommonGrpcServerConfig serverConfig;
    serverConfig.ip = param_.ip;
    serverConfig.listenPort = param_.posixPort;
    serverConfig.creds = ::grpc::InsecureServerCredentials();
    if (param_.enableSSL) {
        if (param_.creds == nullptr) {
            return false;
        }
        serverConfig.creds = param_.creds;
    }
    param_.posixGrpcServer = std::make_shared<functionsystem::grpc::CommonGrpcServer>(serverConfig);
    if (param_.enableServerMode) {
        param_.posixService->BindInternalIAM(param_.internalIAM);
        param_.posixGrpcServer->RegisterService(param_.posixService);
    }
    BusServiceParam serviceParam{ .nodeID = param_.nodeID,
                                  .controlPlaneObserver = param_.controlPlaneObserver,
                                  .controlInterfaceClientMgr = param_.controlInterfacePosixMgr,
                                  .instanceCtrl = instanceCtrl_,
                                  .localSchedSrv = localSchedSrv_,
                                  .isEnableServerMode = param_.enableServerMode,
                                  .hostIP = param_.ip  };
    std::shared_ptr<BusService> busService = std::make_shared<BusService>(std::move(serviceParam));
    param_.posixGrpcServer->RegisterService(busService);
    param_.posixGrpcServer->Start();

    if (!param_.posixGrpcServer->WaitServerReady()) {
        YRLOG_ERROR("failed to start posix grpc server.");
        return false;
    }
    return true;
}
}  // namespace functionsystem::local_scheduler