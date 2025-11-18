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

#ifndef COMMON_COMMON_FLAGS_COMMON_FLAGS_H
#define COMMON_COMMON_FLAGS_COMMON_FLAGS_H

#include <unordered_set>
#include <async/flag_parser_impl.hpp>
#include <async/option.hpp>

#include "common/constants/constants.h"

namespace functionsystem {
class CommonFlags : public litebus::flag::FlagParser {
public:
    CommonFlags();
    ~CommonFlags() override;

    int32_t GetLitebusThreadNum() const
    {
        return litebusThreadNum_;
    }

    uint32_t GetSystemTimeout() const
    {
        return systemTimeout_;
    }

    uint64_t GetPullResourceInterval() const
    {
        return pullResourceInterval_;
    }

    uint32_t GetObservabilityAgentGrpcPort() const
    {
        return observabilityAgentGrpcPort_;
    }

    uint32_t GetObservabilityPrometheusPort() const
    {
        return observabilityPrometheusPort_;
    }

    bool GetSslEnable() const
    {
        return sslEnable_;
    }

    bool GetSslDowngradeEnable() const
    {
        return sslDowngradeEnable_;
    }

    const std::string &GetSslBasePath() const
    {
        return sslBasePath_;
    }

    const std::string &GetSslRootFile() const
    {
        return sslRootFile_;
    }

    const std::string &GetSslCertFile() const
    {
        return sslCertFile_;
    }

    const std::string &GetSslKeyFile() const
    {
        return sslKeyFile_;
    }

    uint64_t GetMaxInstanceCpuSize() const
    {
        return maxInstanceCpuSize_;
    }

    uint64_t GetMinInstanceCpuSize() const
    {
        return minInstanceCpuSize_;
    }

    uint64_t GetMaxInstanceMemorySize() const
    {
        return maxInstanceMemorySize_;
    }

    uint64_t GetMinInstanceMemorySize() const
    {
        return minInstanceMemorySize_;
    }

    bool GetEnableMetrics() const
    {
        return enableMetrics_;
    }

    bool GetMetricsSslEnable() const
    {
        return metricsSslEnable_;
    }

    std::string GetMetricsConfig() const
    {
        return metricsConfig_;
    }

    std::string GetMetricsConfigFile() const
    {
        return metricsConfigFile_;
    }

    bool GetEnableTrace() const
    {
        return enableTrace_;
    }

    std::string GetTraceConfig() const
    {
        return traceConfig_;
    }

    uint32_t GetPrometheusPushgatewayPort() const
    {
        return prometheusPushgatewayPort_;
    }

    const std::string GetPrometheusPushgatewayIP() const
    {
        return prometheusPushgatewayIP_;
    }
    const std::string GetEtcdAddress() const
    {
        return etcdAddress_;
    }

    const std::string GetETCDAuthType() const
    {
        return etcdAuthType_;
    }

    const std::string GetEtcdSecretName() const
    {
        return etcdSecretName_;
    }

    const std::string GetEtcdSslBasePath() const
    {
        return etcdSslBasePath_;
    }

    const std::string GetETCDRootCAFile() const
    {
        return etcdRootCAFile_;
    }

    const std::string GetETCDCertFile() const
    {
        return etcdCertFile_;
    }

    const std::string GetETCDKeyFile() const
    {
        return etcdKeyFile_;
    }

    const std::string GetETCDDecryptTool() const
    {
        return etcdDecryptTool_;
    }

    const std::string GetETCDTargetNameOverride() const
    {
        return etcdTargetNameOverride_;
    }

    const std::string GetETCDTablePrefix() const
    {
        if (!etcdTablePrefix_.empty() && !litebus::strings::StartsWithPrefix(etcdTablePrefix_, "/")) {
            return "/" + etcdTablePrefix_;
        }
        return etcdTablePrefix_;
    }

    const std::unordered_set<std::string> GetMetaStoreExcludedKeys() const
    {
        std::unordered_set<std::string> set;
        auto splits = litebus::strings::Split(metaStoreExcludedKeys_, ",");
        for (const auto &split : splits) {
            if (!split.empty()) {
                set.insert(split);
            }
        }
        return set;
    }

    uint32_t GetMaxTolerateMetaStoreFailedTimes() const
    {
        return maxTolerateMetaStoreFailedTimes_;
    }

    uint32_t GetMetaStoreCheckInterval() const
    {
        return metaStoreCheckHealthIntervalMs_;
    }

    uint32_t GetMetaStoreCheckTimeout() const
    {
        return metaStoreTimeoutMs_;
    }

    uint16_t GetMaxPriority() const
    {
        return maxPriority_;
    }

    const std::string &GetClusterID() const
    {
        return clusterId_;
    }

    const std::string GetAggregatedStrategy() const
    {
        return aggregatedStrategy_;
    }

    const std::string &GetSystemAuthMode() const
    {
        return systemAuthMode_;
    }

    int32_t GetScheduleRelaxed() const
    {
        return scheduleRelaxed_;
    }

    bool GetEnablePreemption() const
    {
        return enablePreemption_;
    }
protected:
    void InitMetaHealthyCheckFlag();
    void InitMetricsFlag();
    void InitTraceFlag();
    void InitETCDAuthFlag();

    int32_t litebusThreadNum_{ LITEBUS_THREAD_NUM };
    uint32_t systemTimeout_{ DEFAULT_SYSTEM_TIMEOUT };
    uint64_t pullResourceInterval_{ DEFAULT_PULL_RESOURCE_INTERVAL };
    uint32_t observabilityAgentGrpcPort_{ DEFAULT_OBSERVABILITY_AGENT_GRPC_PORT };
    uint32_t observabilityPrometheusPort_{ DEFAULT_OBSERVABILITY_PROMETHEUS_PORT };
    bool sslEnable_{ false };
    bool sslDowngradeEnable_{ false };
    std::string sslBasePath_;
    std::string sslRootFile_;
    std::string sslCertFile_;
    std::string sslKeyFile_;
    uint64_t maxInstanceCpuSize_{ DEFAULT_MAX_INSTANCE_CPU_SIZE };
    uint64_t minInstanceCpuSize_{ DEFAULT_MIN_INSTANCE_CPU_SIZE };
    uint64_t maxInstanceMemorySize_{ DEFAULT_MAX_INSTANCE_MEMORY_SIZE };
    uint64_t minInstanceMemorySize_{ DEFAULT_MIN_INSTANCE_MEMORY_SIZE };
    bool enableMetrics_{ false };
    bool metricsSslEnable_{ false };
    std::string metricsConfig_;
    std::string metricsConfigFile_;
    bool enableTrace_{ false };
    std::string traceConfig_;
    uint32_t prometheusPushgatewayPort_{ DEFAULT_OBSERVABILITY_PUSHGATEWAY_PORT };
    std::string prometheusPushgatewayIP_;
    std::string etcdAddress_;
    std::string etcdAuthType_;
    std::string etcdSslBasePath_;
    std::string etcdSecretName_;
    std::string etcdRootCAFile_;
    std::string etcdCertFile_;
    std::string etcdKeyFile_;
    std::string etcdDecryptTool_;
    std::string etcdTargetNameOverride_;
    std::string etcdTablePrefix_;

    uint32_t maxTolerateMetaStoreFailedTimes_;
    uint32_t metaStoreCheckHealthIntervalMs_;
    uint32_t metaStoreTimeoutMs_;
    std::string metaStoreExcludedKeys_;

    uint16_t maxPriority_;

    std::string aggregatedStrategy_;

    std::string clusterId_;

    std::string systemAuthMode_;
    int32_t scheduleRelaxed_;
    bool enablePreemption_;
};

}  // namespace functionsystem

#endif  // COMMON_COMMON_FLAGS_COMMON_FLAGS_H