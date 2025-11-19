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

#ifndef FUNCTION_AGENT_FLAGS_H
#define FUNCTION_AGENT_FLAGS_H

#include "common/common_flags/common_flags.h"

namespace functionsystem::function_agent {
class FunctionAgentFlags : public CommonFlags {
public:
    FunctionAgentFlags();
    ~FunctionAgentFlags() override;

    const std::string &GetLogConfig() const
    {
        return logConfig;
    }

    const std::string &GetNodeID() const
    {
        return nodeID;
    }

    const std::string &GetLocalNodeID() const
    {
        return localNodeID;
    }

    const std::string &GetAlias() const
    {
        return alias;
    }

    const std::string &GetIP() const
    {
        return ip;
    }

    const std::string &GetLocalSchedulerAddress() const
    {
        return localSchedulerAddress;
    }

    const std::string &GetAgentListenPort() const
    {
        return agentListenPort;
    }

    const int32_t &GetFileCountMax() const
    {
        return fileCountMax;
    }

    const int32_t &GetZipFileSizeMaxMB() const
    {
        return zipFileSizeMaxMB;
    }

    const int32_t &GetUnzipFileSizeMaxMB() const
    {
        return unzipFileSizeMaxMB;
    }

    const int32_t &GetDirDepthMax() const
    {
        return dirDepthMax;
    }

    const int32_t &GetCodeAgingTime() const
    {
        return codeAgingTime_;
    }

const std::string &GetAccessKey() const
    {
        return accessKey;
    }

    const std::string &GetSecretKey() const
    {
        return secretKey;
    }

    const std::string &GetS3Endpoint() const
    {
        return s3Endpoint;
    }

    const std::string &GetS3Protocol() const
    {
        return s3Protocol;
    }

    const std::string &GetCredentialType() const
    {
        return credentialType;
    }
    const std::string &GetDecryptAlgorithm() const
    {
        return decryptAlgorithm;
    }

    const bool &GetEnableMergeProcess() const
    {
        return enableMergeProcess;
    }

    const bool &GetS3Enable() const
    {
        return isEnableS3;
    }

    const std::string &GetAgentUID() const
    {
        return agentUID;
    }

    const bool &GetEnableSignatureValidation() const
    {
        return enableSignatureValidation_;
    }

protected:
    std::string logConfig;
    std::string nodeID;
    std::string localNodeID;
    std::string alias;
    std::string ip;
    std::string localSchedulerAddress;
    std::string agentListenPort;

    int32_t fileCountMax{};
    int32_t zipFileSizeMaxMB{};
    int32_t unzipFileSizeMaxMB{};
    int32_t dirDepthMax{};
    int32_t codeAgingTime_{ 0 };

    bool isEnableS3{ DEFAULT_ENABLE_S3 };
    std::string credentialType;
    std::string accessKey;
    std::string secretKey;
    std::string s3Endpoint;
    std::string s3Protocol;
    std::string decryptAlgorithm;

    bool enableMergeProcess = false;
    std::string agentUID = "";
    bool enableSignatureValidation_ = false;
};
}  // namespace functionsystem::function_agent

#endif  // FUNCTION_AGENT_FLAGS_H
