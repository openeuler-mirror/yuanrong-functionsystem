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

#include "api_configuration.h"

namespace functionsystem::kube_client {
namespace api {

ApiConfiguration::ApiConfiguration()
{
}

ApiConfiguration::~ApiConfiguration()
{
}

std::string ApiConfiguration::GetBaseUrl() const
{
    return m_baseUrl;
}

void ApiConfiguration::SetBaseUrl(const std::string value)
{
    m_baseUrl = value;
}

std::string ApiConfiguration::GetUserAgent() const
{
    return m_userAgent;
}

void ApiConfiguration::SetUserAgent(const std::string value)
{
    m_userAgent = value;
}

const std::map<std::string, std::string> &ApiConfiguration::GetDefaultHeaders() const
{
    return m_defaultHeaders;
}

std::string ApiConfiguration::GetApiKey(const std::string &prefix) const
{
    auto result = m_apiKeys.find(prefix);
    if (result != m_apiKeys.end()) {
        return result->second;
    }
    return std::string("");
}

void ApiConfiguration::SetApiKey(const std::string &prefix, const std::string &apiKey)
{
    m_apiKeys[prefix] = apiKey;
}

const SslConfig &ApiConfiguration::GetSslConfig() const
{
    return m_sslConfig;
}

void ApiConfiguration::SetSslConfig(const SslConfig &value)
{
    m_sslConfig = value;
}

}  // namespace api
}  // namespace functionsystem::kube_client