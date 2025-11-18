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

#ifndef DOMAIN_SCHEDULER_FLAGS_H
#define DOMAIN_SCHEDULER_FLAGS_H

#include "common/common_flags/common_flags.h"

namespace functionsystem::domain_scheduler {
class Flags : public CommonFlags {
public:
    Flags();
    ~Flags() override = default;

    const std::string &GetLogConfig() const
    {
        return logConfig_;
    }

    const std::string &GetGlobalAddress() const
    {
        return globalAddress_;
    }

    const std::string &GetMetaStoreAddress() const
    {
        return metaStoreAddress_;
    }

    const std::string &GetElectionMode() const
    {
        return electionMode_;
    }

    const std::string &GetIP() const
    {
        return ip_;
    }

    const std::string &GetDomainListenPort() const
    {
        return domainListenPort_;
    }

    const std::string &GetNodeID() const
    {
        return nodeID_;
    }

    bool GetIsScheduleTolerateAbnormal() const
    {
        return isScheduleTolerateAbnormal_;
    }

    bool GetEnablePrintResourceView() const
    {
        return enablePrintResourceView_;
    }

    const std::string &GetK8sBasePath() const
    {
        return basePath_;
    }

    const std::string &GetK8sNamespace() const
    {
        return k8sNamespace_;
    }

    uint32_t GetElectKeepAliveInterval() const
    {
        return electKeepAliveInterval_;
    }
protected:
    std::string electionMode_;
    std::string logConfig_;
    std::string globalAddress_;
    std::string metaStoreAddress_;
    std::string ip_;
    std::string domainListenPort_;
    std::string nodeID_;
    bool isScheduleTolerateAbnormal_;
    bool enablePrintResourceView_;
    std::string k8sNamespace_;
    std::string basePath_;
    uint32_t electKeepAliveInterval_;
};
}  // namespace functionsystem::domain_scheduler
#endif  // DOMAIN_SCHEDULER_FLAGS_H