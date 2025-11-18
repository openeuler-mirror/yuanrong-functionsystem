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
#include <iostream>
#include <memory>
#include <string>

#include "common/constants/constants.h"
#include "common/explorer/explorer.h"
#include "common/logs/logging.h"
#include "common/rpc/client/grpc_client.h"
#include "common/utils/module_switcher.h"
#include "common/utils/version.h"
#include "domain_scheduler/flags/flags.h"
#include "include/domain_scheduler_launcher.h"
#include "utils/os_utils.hpp"
#include "async/future.hpp"
#include "async/option.hpp"
#include "common/explorer/explorer_actor.h"
#include "common/kube_client/kube_client.h"
#include "meta_store_client/meta_store_client.h"
#include "common/status/status.h"
#include "common/utils/ssl_config.h"
#include "domain_scheduler/include/structure.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/aksk/aksk_util.h"
#include "common/constants/actor_name.h"

using namespace functionsystem;
namespace {
const std::string COMPONENT_NAME = COMPONENT_NAME_DOMAIN_SCHEDULER;
std::shared_ptr<litebus::Promise<bool>> stopSignal{ nullptr };
std::shared_ptr<functionsystem::ModuleSwitcher> domainSchedulerSwitcher_{ nullptr };
std::shared_ptr<domain_scheduler::DomainSchedulerLauncher> domainSchedulerDriver_{ nullptr };
std::shared_ptr<KubeClient> kubeClient_ = nullptr;

void OnStopHandler(int signum)
{
    YRLOG_INFO("receive signal: {}", signum);
    stopSignal->SetValue(true);
}

void OnCreate(const domain_scheduler::Flags &flags)
{
    YRLOG_INFO("{} is starting", COMPONENT_NAME);
    YRLOG_INFO("version:{} branch:{} commit_id:{}", BUILD_VERSION, GIT_BRANCH_NAME, GIT_HASH);
    auto address = litebus::os::Join(flags.GetIP(), flags.GetDomainListenPort(), ':');
    if (!domainSchedulerSwitcher_->InitLiteBus(address, flags.GetLitebusThreadNum())) {
        domainSchedulerSwitcher_->SetStop();
        return;
    }

    MetaStoreTimeoutOption option;
    // retries must take longer than health check
    option.operationRetryTimes = static_cast<int64_t>((flags.GetMaxTolerateMetaStoreFailedTimes() + 1) *
                                 (flags.GetMetaStoreCheckInterval() + flags.GetMetaStoreCheckTimeout()) /
                                 KV_OPERATE_RETRY_INTERVAL_LOWER_BOUND);
    MetaStoreMonitorParam monitorParam{
        .maxTolerateFailedTimes = flags.GetMaxTolerateMetaStoreFailedTimes(),
        .checkIntervalMs = flags.GetMetaStoreCheckInterval(),
        .timeoutMs = flags.GetMetaStoreCheckTimeout(),
    };
    MetaStoreConfig metaStoreConfig;
    metaStoreConfig.etcdAddress = flags.GetMetaStoreAddress();
    metaStoreConfig.etcdTablePrefix = flags.GetETCDTablePrefix();
    metaStoreConfig.excludedKeys = flags.GetMetaStoreExcludedKeys();
    // standalone domain never enable metastore
    auto metaClient = MetaStoreClient::Create(metaStoreConfig, GetGrpcSSLConfig(flags), option, true, monitorParam);
    if (metaClient == nullptr) {
        YRLOG_ERROR("failed to create meta store client");
        domainSchedulerSwitcher_->SetStop();
        return;
    }
    kubeClient_ = KubeClient::CreateKubeClient(flags.GetK8sBasePath(), KubeClient::ClusterSslConfig("", "", false));
    auto leaderName = flags.GetElectionMode() == K8S_ELECTION_MODE ? explorer::FUNCTION_MASTER_K8S_LEASE_NAME
                                                                   : explorer::DEFAULT_MASTER_ELECTION_KEY;
    explorer::LeaderInfo leaderInfo{ .name = leaderName, .address = flags.GetGlobalAddress() };
    explorer::ElectionInfo electionInfo{ .identity = flags.GetIP(),
                                         .mode = flags.GetElectionMode(),
                                         .electKeepAliveInterval = flags.GetElectKeepAliveInterval() };
    if (!explorer::Explorer::CreateExplorer(electionInfo, leaderInfo, metaClient, kubeClient_,
                                            flags.GetK8sNamespace())) {
        return;
    }
    auto identity = litebus::os::Join(flags.GetNodeID(), address, '-');
    domain_scheduler::DomainSchedulerParam param;
    param.identity = identity;
    param.globalAddress = flags.GetGlobalAddress();
    param.metaStoreClient = metaClient;
    param.heartbeatTimeoutMs = flags.GetSystemTimeout();
    param.pullResourceInterval = flags.GetPullResourceInterval();
    param.isScheduleTolerateAbnormal = flags.GetIsScheduleTolerateAbnormal();
    param.maxPriority = flags.GetMaxPriority();
	param.enablePreemption = flags.GetEnablePreemption();
	param.relaxed = flags.GetScheduleRelaxed();
    param.aggregatedStrategy = flags.GetAggregatedStrategy();
    param.componentName = COMPONENT_NAME;
    domainSchedulerDriver_ = std::make_shared<domain_scheduler::DomainSchedulerLauncher>(param);
    if (auto status = domainSchedulerDriver_->Start(); status.IsError()) {
        YRLOG_ERROR("failed to start {}, errMsg: {}", COMPONENT_NAME, status.ToString());
        domainSchedulerSwitcher_->SetStop();
        return;
    }
    YRLOG_INFO("{} is started", COMPONENT_NAME);
}

void OnDestroy()
{
    YRLOG_INFO("{} is stopping", COMPONENT_NAME);

    if (domainSchedulerDriver_ != nullptr && domainSchedulerDriver_->Stop().IsOk()) {
        domainSchedulerDriver_->Await();
        domainSchedulerDriver_ = nullptr;
        YRLOG_INFO("success to stop {}", COMPONENT_NAME);
    } else {
        YRLOG_WARN("failed to stop {}", COMPONENT_NAME);
    }

    explorer::Explorer::GetInstance().Clear();
    domainSchedulerSwitcher_->CleanMetrics();
    domainSchedulerSwitcher_->StopLogger();
    domainSchedulerSwitcher_->FinalizeLiteBus();
}

}  // namespace
int main(int argc, char **argv)
{
    domain_scheduler::Flags flags;
    litebus::Option<std::string> parse = flags.ParseFlags(argc, argv);
    if (parse.IsSome()) {
        std::cerr << COMPONENT_NAME << " parse flag error, flags: " << parse.Get() << std::endl
                  << flags.Usage() << std::endl;
        return EXIT_COMMAND_MISUSE;
    }

    domainSchedulerSwitcher_ = std::make_shared<functionsystem::ModuleSwitcher>(COMPONENT_NAME, flags.GetNodeID());
    if (!domainSchedulerSwitcher_->InitLogger(flags)) {
        return EXIT_ABNORMAL;
    }

    auto sslCertConfig = GetSSLCertConfig(flags);
    if (flags.GetSslEnable() && InitLitebusSSLEnv(sslCertConfig).IsError()) {
        YRLOG_ERROR("failed to init litebus ssl env");
        domainSchedulerSwitcher_->SetStop();
        return EXIT_ABNORMAL;
    }
    if (!InitLitebusAKSKEnv(flags).IsOk()) {
        YRLOG_ERROR("failed to get aksk config");
        domainSchedulerSwitcher_->SetStop();
        return EXIT_ABNORMAL;
    }
    domainSchedulerSwitcher_->InitMetrics(flags.GetEnableMetrics(), flags.GetMetricsConfig(),
                                          flags.GetMetricsConfigFile(), sslCertConfig);

    if (!domainSchedulerSwitcher_->RegisterHandler(OnStopHandler, stopSignal)) {
        return EXIT_ABNORMAL;
    }

    OnCreate(flags);

    domainSchedulerSwitcher_->WaitStop();

    OnDestroy();

    return 0;
}