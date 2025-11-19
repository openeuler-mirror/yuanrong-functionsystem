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

#ifndef LOCAL_SCHEDULER_LOCAL_SCHED_DRIVER_H
#define LOCAL_SCHEDULER_LOCAL_SCHED_DRIVER_H

#include "common/distribute_cache_client/ds_cache_client_impl.h"
#include "common/http/http_server.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "common/posix_service/posix_service.h"
#include "common/resource_view/resource_view_mgr.h"
#include "common/rpc/server/common_grpc_server.h"
#include "common/status/status.h"
#include "common/utils/module_driver.h"
#include "local_scheduler/abnormal_processor/abnormal_processor.h"
#include "local_scheduler/bundle_manager/bundle_mgr.h"
#include "local_scheduler/ds_healthy_checker/ds_healthy_checker.h"
#include "local_scheduler/function_agent_manager/function_agent_mgr.h"
#include "local_scheduler/instance_control/instance_ctrl.h"
#include "local_scheduler/local_group_ctrl/local_group_ctrl.h"
#include "local_scheduler/local_scheduler_service/local_sched_srv.h"
#include "local_scheduler/local_scheduler_service/local_sched_srv_actor.h"
#include "local_scheduler/resource_group_controller/resource_group_ctrl.h"
#include "local_scheduler/subscription_manager/subscription_mgr.h"

namespace functionsystem::local_scheduler {
struct LocalSchedStartParam {
    std::string nodeID;
    std::string globalSchedulerAddress;  // used to register to global scheduler
    std::string schedulePolicy;          // if schedule policy is empty, use default schedule policy
    std::string metaStoreAddress;
    std::string ip;
    std::string cacheStorageHost;
    std::string grpcListenPort;
    std::string serverRootCert;
    std::string serverNameOverride;
    std::string runtimeHeartbeatEnable;
    uint32_t runtimeMaxHeartbeatTimeoutTimes;
    uint32_t runtimeHeartbeatTimeoutMS;
    uint32_t runtimeInitCallTimeoutMS;
    uint32_t runtimeConnTimeoutSeconds;
    uint32_t runtimeShutdownTimeoutSeconds;
    bool runtimeRecoverEnable;
    std::shared_ptr<functionsystem::DSAuthConfig> dsAuthConfig;
    FunctionAgentMgrActor::Param funcAgentMgrParam;
    LocalSchedSrvActor::Param localSchedSrvParam;
    ResourceViewActor::Param resourceViewActorParam;
    std::shared_ptr<ControlInterfaceClientManagerProxy> controlInterfacePosixMgr;
    std::shared_ptr<function_proxy::ControlPlaneObserver> controlPlaneObserver;
    std::shared_ptr<function_proxy::InternalIAM> internalIAM;
    int32_t maxGrepSize;
    bool enableDriver;
    bool isPseudoDataPlane;
    bool enableServerMode;
    bool enableSSL;
    uint64_t dsHealthCheckInterval;
    uint64_t maxDsHealthCheckTimes;
    InstanceLimitResource limitResource;
    bool enablePrintResourceView;
    std::shared_ptr<functionsystem::grpc::CommonGrpcServer> posixGrpcServer;
    std::shared_ptr<functionsystem::PosixService> posixService;
    std::shared_ptr<::grpc::ServerCredentials> creds;
    std::string posixPort;
    std::string schedulePlugins;
    bool enableTenantAffinity;
    bool createLimitationEnable;
    uint32_t tokenBucketCapacity;
    bool isMetaStoreEnabled;
    uint16_t maxPriority;
    std::string aggregatedStrategy_;
    bool enablePreemption;
    bool isPartialWatchInstances;
    std::shared_ptr<DSCacheClientImpl> distributedCacheClient;
    bool runtimeInstanceDebugEnable;
    bool unRegisterWhileStop;
};

class LocalSchedDriver : public ModuleDriver {
public:
    explicit LocalSchedDriver(LocalSchedStartParam &&param, const std::shared_ptr<MetaStoreClient> metaStoreClient)
        : param_(std::move(param)), metaStoreClient_(metaStoreClient){};
    ~LocalSchedDriver() override = default;

    Status Start() override;
    Status Stop() override;
    Status Sync() override;
    Status Recover() override;
    void ToReady() override;
    void Await() override;

protected:
    Status Create();

private:
    bool CreatePosixAndDriverServer();
    void StartDsHealthyCheck();
    void StartDebugInstanceInfoMonitor();
    void SetRuntimeConfig(InstanceCtrlConfig &config);
    void BindInstanceCtrl();

    class InstanceCtrlMetaStoreHealthyObserver : public MetaStoreHealthyObserver {
    public:
        explicit InstanceCtrlMetaStoreHealthyObserver(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
            : instanceCtrl_(instanceCtrl)
        {
        }

        void OnHealthyStatus(const Status &status) override
        {
            if (instanceCtrl_) {
                instanceCtrl_->OnHealthyStatus(status);
            }
        }

    private:
        std::shared_ptr<InstanceCtrl> instanceCtrl_;
    };

    LocalSchedStartParam param_;
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
    std::shared_ptr<resource_view::ResourceViewMgr> resourceViewMgr_;
    std::shared_ptr<LocalSchedSrv> localSchedSrv_;
    std::shared_ptr<local_scheduler::FunctionAgentMgr> funcAgentMgr_;
    std::shared_ptr<HttpServer> httpServer_;
    std::shared_ptr<DefaultHealthyRouter> apiRouteRegister_;
    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    std::shared_ptr<AbnormalProcessor> abnormalProcessor_;
    std::shared_ptr<DsHealthyChecker> dsHealthyChecker_;
    std::shared_ptr<BundleMgr> bundleMgr_;
    std::shared_ptr<LocalGroupCtrl> localGroupCtrl_;
    std::shared_ptr<ResourceGroupCtrl> rGroupCtrl_;
    std::shared_ptr<SubscriptionMgr> subscriptionMgr_;
    std::shared_ptr<InstanceCtrlMetaStoreHealthyObserver> metaStoreHealthyObserver_;
    bool isStarted_ = false;
};
}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_LOCAL_SCHED_DRIVER_H
