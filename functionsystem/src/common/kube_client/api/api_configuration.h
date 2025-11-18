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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_API_API_CONFIGURATION_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_API_API_CONFIGURATION_H

#include <map>

#include "api_exception.h"

namespace functionsystem::kube_client {
namespace api {
struct SslConfig {
    std::string clientCertFile;   /* client certificate */
    std::string clientKeyFile;    /* client private key */
    std::string caCertFile;       /* CA certificate */
    std::string credential;       /* multi-certificates credential */
    bool isSkipTlsVerify = false; /* 0 -- verify server certificate */
                                  /* 1 -- skip ssl verify for server certificate */
};
class ApiConfiguration {
public:
    ApiConfiguration();
    virtual ~ApiConfiguration();

    std::string GetBaseUrl() const;
    void SetBaseUrl(const std::string value);

    std::string GetUserAgent() const;
    void SetUserAgent(const std::string value);

    const std::map<std::string, std::string> &GetDefaultHeaders() const;

    std::string GetApiKey(const std::string &prefix) const;
    void SetApiKey(const std::string &prefix, const std::string &apiKey);

    const SslConfig &GetSslConfig() const;
    void SetSslConfig(const SslConfig &value);

protected:
    std::string m_baseUrl;
    std::map<std::string, std::string> m_defaultHeaders;
    std::map<std::string, std::string> m_apiKeys;
    std::string m_userAgent;
    SslConfig m_sslConfig;
};

}  // namespace api
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_API_API_CONFIGURATION_H */
