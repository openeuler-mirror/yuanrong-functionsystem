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

#ifndef IAM_SERVER_FLAGS_FLAGS_H
#define IAM_SERVER_FLAGS_FLAGS_H

#include "common/common_flags/common_flags.h"

namespace functionsystem::iamserver {

class Flags : public CommonFlags {
public:
    Flags();
    ~Flags() override;

    const std::string &GetLogConfig() const
    {
        return logConfig;
    }

    const std::string &GetNodeID() const
    {
        return nodeID;
    }

    const std::string &GetIP() const
    {
        return ip;
    }

    const std::string &GetHTTPListenPort() const
    {
        return httpListenPort;
    }

    const std::string &GetLocalIP() const
    {
        return localIp_;
    }

    uint16_t GetLocalListenPort() const
    {
        if (localListenPort_.empty()) {
            return 0;
        }
        try { return static_cast<uint16_t>(std::stoul(localListenPort_)); }
        catch (...) { return 0; }
    }

    const std::string &GetMetaStoreAddress() const
    {
        return metaStoreAddress;
    }

    bool GetEnableTrace() const
    {
        return enableTrace;
    }

    bool GetIsEnableIAM() const
    {
        return enableIAM_;
    }

    uint32_t GetTokenExpiredTimeSpan() const
    {
        return tokenExpiredTimeSpan_;
    }

    const std::string &GetDecryptAlgorithm() const
    {
        return decryptAlgorithm_;
    }

    const std::string &GetResourcePath() const
    {
        return resourcePath_;
    }

    const std::string &GetK8sBasePath() const
    {
        return k8sBasePath_;
    }

    const std::string &GetK8sNamespace() const
    {
        return k8sNamespace_;
    }

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

    const std::string GetIamCredentialType() const
    {
        return iamCredentialType_;
    }

    const std::string GetPermanentCredentialConfigPath() const
    {
        return permanentCredentialConfigPath_;
    }

    [[nodiscard]] std::string GetCredentialHostAddress() const
    {
        return credentialHostAddress_;
    }

    const std::string &GetKeycloakUrl() const
    {
        return keycloakUrl_;
    }

    const std::string &GetKeycloakPublicUrl() const
    {
        return keycloakPublicUrl_;
    }

    const std::string &GetKeycloakClientId() const
    {
        return keycloakClientId_;
    }

    const std::string &GetKeycloakClientSecret() const
    {
        return keycloakClientSecret_;
    }

    const std::string &GetKeycloakIssuerUrl() const
    {
        return keycloakIssuerUrl_.empty() ? keycloakUrl_ : keycloakIssuerUrl_;
    }

    const std::string &GetKeycloakRealm() const
    {
        return keycloakRealm_;
    }

    bool GetKeycloakEnabled() const
    {
        return keycloakEnabled_;
    }

    int GetKeycloakCacheTtlSeconds() const
    {
        return keycloakCacheTtlSeconds_;
    }

    const std::string &GetAuthProvider() const
    {
        return authProvider_;
    }

    const std::string &GetCasdoorEndpoint() const
    {
        return casdoorEndpoint_;
    }

    const std::string &GetCasdoorPublicEndpoint() const
    {
        return casdoorPublicEndpoint_;
    }

    const std::string &GetCasdoorClientId() const
    {
        return casdoorClientId_;
    }

    const std::string &GetCasdoorClientSecret() const
    {
        return casdoorClientSecret_;
    }

    const std::string &GetCasdoorOrganization() const
    {
        return casdoorOrganization_;
    }

    const std::string &GetCasdoorApplication() const
    {
        return casdoorApplication_;
    }

    const std::string &GetCasdoorAdminUser() const
    {
        return casdoorAdminUser_;
    }

    const std::string &GetCasdoorAdminPassword() const
    {
        return casdoorAdminPassword_;
    }

    const std::string &GetCasdoorJwtPublicKey() const
    {
        return casdoorJwtPublicKey_;
    }

    bool GetCasdoorEnabled() const
    {
        return casdoorEnabled_;
    }

    /* IAM-specific SSL toggle.
     * When --iam_ssl_enable is set, it overrides the global --ssl_enable for IAM's listener.
     * Certificate paths are always reused from the global ssl_base_path/ssl_cert_file/etc.
     * When --iam_ssl_enable is not set (empty), falls back to global --ssl_enable. */
    bool GetIAMSslEnable() const
    {
        if (iamSslEnable_.empty()) {
            return GetSslEnable();
        }
        return iamSslEnable_ == "true";
    }

    bool HasIAMSslOverride() const
    {
        return !iamSslEnable_.empty();
    }

private:
    void RegisterDualPortAndSslFlags();
    std::string logConfig;
    std::string nodeID;
    std::string ip;
    std::string httpListenPort;
    std::string localIp_;
    std::string localListenPort_;
    std::string metaStoreAddress;
    bool enableTrace = false;
    std::string servicesPath_;
    std::string libPath_;
    bool enableIAM_ = false;
    uint32_t tokenExpiredTimeSpan_;
    std::string resourcePath_;
    std::string decryptAlgorithm_;
    std::string k8sBasePath_;
    std::string k8sNamespace_;
    uint32_t electLeaseTTL_;
    uint32_t electKeepAliveInterval_;
    std::string electionMode_;
    std::string iamCredentialType_;
    std::string permanentCredentialConfigPath_;
    std::string credentialHostAddress_;
    std::string keycloakUrl_;
    std::string keycloakPublicUrl_;
    std::string keycloakClientId_;
    std::string keycloakClientSecret_;
    std::string keycloakIssuerUrl_;
    std::string keycloakRealm_;
    bool keycloakEnabled_ = false;
    int keycloakCacheTtlSeconds_ = 300;

    std::string authProvider_;
    std::string casdoorEndpoint_;
    std::string casdoorPublicEndpoint_;
    std::string casdoorClientId_;
    std::string casdoorClientSecret_;
    std::string casdoorOrganization_;
    std::string casdoorApplication_;
    std::string casdoorAdminUser_;
    std::string casdoorAdminPassword_;
    std::string casdoorJwtPublicKey_;
    bool casdoorEnabled_ = false;

    std::string iamSslEnable_;
};
}  // namespace functionsystem::iamserver
#endif  // IAM_SERVER_FLAGS_FLAGS_H
