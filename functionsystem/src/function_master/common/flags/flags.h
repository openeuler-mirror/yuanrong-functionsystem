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

#ifndef FUNCTION_MASTER_FLAGS_H
#define FUNCTION_MASTER_FLAGS_H

#include <async/flag_parser_impl.hpp>

#include "common/common_flags/common_flags.h"

namespace functionsystem::functionmaster {

class Flags : public CommonFlags {
public:
    Flags();

    ~Flags() override = default;

    [[nodiscard]] const std::string &GetLogConfig() const;

    [[nodiscard]] const std::string &GetNodeID() const;

    [[nodiscard]] const std::string &GetIP() const;

    [[nodiscard]] const std::string &GetMetaStoreAddress() const;

    [[nodiscard]] const std::string &GetK8sBasePath() const;

    [[nodiscard]] const std::string &GetK8sClientCertFile() const;

    [[nodiscard]] const std::string &GetK8sClientKeyFile() const;

    [[nodiscard]] bool GetIsSkipTlsVerify() const;

    [[nodiscard]] const litebus::Option<int> &GetD1() const;

    [[nodiscard]] const litebus::Option<int> &GetD2() const;

    [[nodiscard]] uint32_t GetSysFuncRetryPeriod() const;

    [[nodiscard]] const std::string &GetSysFuncCustomArgs() const;

    [[nodiscard]] int GetAccessorPort() const;

    [[nodiscard]] bool GetRuntimeRecoverEnable() const;

    [[nodiscard]] bool GetIsScheduleTolerateAbnormal() const;

    [[nodiscard]] const std::string &GetDecryptAlgorithm() const;

    [[nodiscard]] const std::string &GetK8sNamespace() const;

    const std::string &GetElectionMode() const
    {
        return electionMode_;
    }

    uint32_t GetElectLeaseTTL() const
    {
        return electLeaseTTL_;
    }

    uint32_t GetElectKeepAliveInterval() const
    {
        return electKeepAliveInterval_;
    }

    bool GetEnablePrintResourceView() const
    {
        return enablePrintResourceView_;
    }

    const std::string GetMigratePrefix() const
    {
        return migratePrefix_;
    }

    const std::string GetTaintToleranceList() const
    {
        return taintToleranceList_;
    }

    const std::string GetWorkerTaintExcludeLabels() const
    {
        return workerTaintExcludeLabels_;
    }

    bool GetMigrateEnable() const
    {
        return migrateEnable_;
    }

    bool GetSystemUpgradeWatchEnable() const
    {
        return systemUpgradeWatchEnable_;
    }

    uint32_t GetAzID() const
    {
        return azID_;
    }

    std::string GetSystemUpgradeKey() const
    {
        return systemUpgradeKey_;
    }

    std::string GetSystemUpgradeWatchAddress() const
    {
        return systemUpgradeWatchAddress_;
    }

    uint32_t GetGracePeriodSeconds() const
    {
        return gracePeriodSeconds_;
    }

    std::string GetSchedulePlugins() const
    {
        return schedulePlugins_;
    }

    uint32_t GetKubeClientRetryTimes() const
    {
        return kubeClientRetryTimes_;
    }

    uint32_t GetKubeClientRetryCycleMs() const
    {
        return kubeClientRetryCycleMs_;
    }

    uint32_t GetGetHealthMonitorMaxFailure() const
    {
        return healthMonitorMaxFailure_;
    }

    uint32_t GetGetHealthMonitorRetryInterval() const
    {
        return healthMonitorRetryInterval_;
    }

    bool GetEnableMetaStore() const
    {
        return enableMetaStore_;
    }

    std::string GetMetaStoreMode() const
    {
        return metaStoreMode_;
    }

    bool GetEnablePersistence() const
    {
        return enablePersistence_;
    }

    uint32_t GetMetaStoreMaxFlushConcurrency() const
    {
        return metaStoreMaxFlushConcurrency_;
    }

    uint32_t GetMetaStoreMaxFlushBatchSize() const
    {
        return metaStoreMaxFlushBatchSize_;
    }

    bool GetEnableSyncSysFunc() const
    {
        return enableSyncFuncSysFunc_;
    }

    std::string GetEvictedTaintKey() const
    {
        return evictedTaintKey_;
    }

    std::string GetLocalSchedulerPort() const
    {
        return localSchedulerPort_;
    }

    const std::string GetSelfTaintPrefix() const
    {
        return selfTaintPrefix_;
    }

    const std::string GetServicesPath() const
    {
        return servicesPath_;
    }

    const std::string GetLibPath() const
    {
        return libPath_;
    }

    const std::string GetFunctionMetaPath() const
    {
        return functionMetaPath_;
    }

    const std::string &GetPoolConfigPath() const
    {
        return poolConfigPath_;
    }

    const std::string &GetAgentTemplatePath() const
    {
        return agentTemplatePath_;
    }

    bool GetEnableFrontendPool() const
    {
        return enableFrontendPool_;
    }

    [[nodiscard]] bool GetEnableAbnormalDoubleCheck() const
    {
        return enableAbnormalDoubleCheck_;
    }

    uint32_t GetDomainHeartbeatTimeout() const
    {
        return domainHeartbeatTimeoutMs_;
    }
protected:
    void InitScalerFlags();
    void InitMetaStoreFlags();

    std::string logConfig_;
    std::string nodeID_;
    std::string ip_;

    std::string metaStoreAddress_;

    std::string basePath_;
    std::string clientKeyFile_;
    std::string clientCertFile_;
    std::string isSkipTlsVerify_;
    std::string k8sNamespace_;

    litebus::Option<int> d1_;
    litebus::Option<int> d2_;

    uint32_t electLeaseTTL_;
    uint32_t electKeepAliveInterval_;
    uint32_t sysFuncRetryPeriod_;
    std::string sysFuncCustomArgs_;

    bool runtimeRecoverEnable_;
    bool isScheduleTolerateAbnormal_;

    std::string decryptAlgorithm_;

    std::string electionMode_;
    bool enablePrintResourceView_;

    std::string migratePrefix_;
    std::string taintToleranceList_;
    bool migrateEnable_;
    std::string workerTaintExcludeLabels_;
    std::string evictedTaintKey_;
    std::string localSchedulerPort_;
    std::string selfTaintPrefix_;
    std::string servicesPath_;
    std::string libPath_;
    std::string functionMetaPath_;

    bool systemUpgradeWatchEnable_;
    uint32_t azID_;
    std::string systemUpgradeKey_;
    std::string systemUpgradeWatchAddress_;

    uint32_t gracePeriodSeconds_;
    std::string schedulePlugins_;

    uint32_t kubeClientRetryTimes_;
    uint32_t kubeClientRetryCycleMs_;

    uint32_t healthMonitorMaxFailure_;
    uint32_t healthMonitorRetryInterval_;

    bool enableMetaStore_;
    bool enablePersistence_;
    bool enableSyncFuncSysFunc_;
    bool metaStoreNeedExplore_;
    std::string metaStoreMode_;
    uint32_t metaStoreMaxFlushConcurrency_{ 0 };
    uint32_t metaStoreMaxFlushBatchSize_{ 0 };

    std::string poolConfigPath_;
    std::string agentTemplatePath_;

    bool enableFrontendPool_{ false };
    bool enableAbnormalDoubleCheck_{ false };
    uint32_t domainHeartbeatTimeoutMs_{ DEFAULT_DOMAIN_HEARTBEAT_TIMEOUT };
};

}  // namespace functionsystem::functionmaster

#endif  // FUNCTION_MASTER_FLAGS_H
