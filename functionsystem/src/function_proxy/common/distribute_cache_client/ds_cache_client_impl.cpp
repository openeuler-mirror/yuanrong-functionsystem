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

#include "ds_cache_client_impl.h"

#include <memory>

#include "common/logs/logging.h"
#include "common/utils/files.h"

#define RETURN_IF_DS_ERROR(statement)                                      \
    do {                                                                   \
        ::datasystem::Status rc = (statement);                             \
        if (rc.IsError()) {                                                \
            YRLOG_ERROR("DS return failed, error: {}", rc.ToString());     \
            return Status(StatusCode::BP_DATASYSTEM_ERROR, rc.ToString()); \
        }                                                                  \
    } while (false)

namespace functionsystem {

DSCacheClientImpl::DSCacheClientImpl(const datasystem::ConnectOptions &connectOptions)
{
    kvClient_ = std::make_unique<datasystem::KVClient>(connectOptions);
    dsObjectClient_ = std::make_unique<datasystem::ObjectClient>(connectOptions);
}

Status DSCacheClientImpl::Init()
{
    std::unique_lock<std::mutex> lk(mut_);
    if (isDSEnabled_) {
        RETURN_IF_DS_ERROR(kvClient_->Init());
        RETURN_IF_DS_ERROR(dsObjectClient_->Init());
    }
    return Status::OK();
}

Status DSCacheClientImpl::Set(const std::string &key, const std::string &val)
{
    RETURN_IF_DS_ERROR(kvClient_->Set(key, val));
    return Status::OK();
}

Status DSCacheClientImpl::Get(const std::string &key, std::string &val)
{
    std::string getVal;
    RETURN_IF_DS_ERROR(kvClient_->Get(key, getVal));
    val = getVal;
    return Status::OK();
}

Status DSCacheClientImpl::Get(const std::vector<std::string> &keys, std::vector<std::string> &vals)
{
    std::vector<std::string> getVals;
    RETURN_IF_DS_ERROR(kvClient_->Get(keys, getVals));
    for (auto &val : getVals) {
        (void)vals.emplace_back(val);
    }
    return Status::OK();
}

Status DSCacheClientImpl::Del(const std::string &key)
{
    RETURN_IF_DS_ERROR(kvClient_->Del(key));
    return Status::OK();
}

Status DSCacheClientImpl::Del(const std::vector<std::string> &keys, std::vector<std::string> &failedKeys)
{
    RETURN_IF_DS_ERROR(kvClient_->Del(keys, failedKeys));
    return Status::OK();
}

void DSCacheClientImpl::GetAuthConnectOptions(const std::shared_ptr<DSAuthConfig> &config,
                                              datasystem::ConnectOptions &connectOptions)
{
    RETURN_IF_NULL(config);
    if (!config->isEnable || config->type == "Noauth") {
        return;
    }

    if (config->type.find("ZMQ") != std::string::npos) {
        connectOptions.clientPublicKey =
            std::string(config->clientPublicKey.GetData(), config->clientPublicKey.GetSize());
        connectOptions.serverPublicKey =
            std::string(config->serverPublicKey.GetData(), config->serverPublicKey.GetSize());
        connectOptions.clientPrivateKey =
            datasystem::SensitiveValue(config->clientPrivateKey.GetData(), config->clientPrivateKey.GetSize());
    }

    if (config->type.find("AK/SK") != std::string::npos) {
        connectOptions.accessKey = config->ak;
        connectOptions.secretKey = datasystem::SensitiveValue(config->sk.GetData(), config->sk.GetSize());
    }
}

Status DSCacheClientImpl::GetHealthStatus()
{
    ::datasystem::Status healthCheckResult = (dsObjectClient_->HealthCheck());
    if (healthCheckResult.IsError()) {
        YRLOG_ERROR("DS return failed, error: {}", healthCheckResult.ToString());
        return Status(StatusCode::BP_DATASYSTEM_ERROR, healthCheckResult.ToString());
    }

    return Status::OK();
}

Status DSCacheClientImpl::ShutDown()
{
    RETURN_IF_DS_ERROR(dsObjectClient_->ShutDown());
    return Status::OK();
}

void DSCacheClientImpl::EnableDSClient(bool isEnable)
{
    isDSEnabled_ = isEnable;
}

void DSCacheClientImpl::SetDSAuthEnable(bool isEnable)
{
    isDSAuthEnable_ = isEnable;
}

}  // namespace functionsystem