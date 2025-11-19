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

#ifndef FUNCTION_PROXY_COMMON_DISTRIBUTED_CACHE_CLIENT_DS_CACHE_CLIENT_H
#define FUNCTION_PROXY_COMMON_DISTRIBUTED_CACHE_CLIENT_DS_CACHE_CLIENT_H

#include <memory>
#include <vector>

#include "common/utils/sensitive_value.h"
#include "datasystem/datasystem.h"
#include "distributed_cache_client.h"

namespace functionsystem {

struct DSAuthConfig {
    bool isEnable = false;
    bool isRuntimeEnable = false;
    bool isRuntimeEncryptEnable = false;
    std::string type = "";
    std::string ak;
    SensitiveValue sk;
    SensitiveValue clientPublicKey;
    SensitiveValue clientPrivateKey;
    SensitiveValue serverPublicKey;
};

class DSCacheClientImpl : public DistributedCacheClient {
public:
    explicit DSCacheClientImpl(const datasystem::ConnectOptions &connectOptions);
    ~DSCacheClientImpl() override = default;
    Status Init() override;

    // state client
    Status Set(const std::string &key, const std::string &val) override;
    Status Get(const std::string &key, std::string &val) override;
    Status Get(const std::vector<std::string> &keys, std::vector<std::string> &vals) override;
    Status Del(const std::string &key) override;
    Status Del(const std::vector<std::string> &keys, std::vector<std::string> &failedKeys) override;

    Status GetHealthStatus() override;

    Status ShutDown() override;

    void EnableDSClient(bool isEnable);
    void SetDSAuthEnable(bool isEnable);
    bool IsDsClientEnable() const
    {
        return isDSEnabled_;
    }

    static void GetAuthConnectOptions(const std::shared_ptr<DSAuthConfig> &config,
                                      datasystem::ConnectOptions &connectOptions);

    static void GetAKSKAuthConnectOptions(const std::string &ak, const std::string &sk,
                                          datasystem::ConnectOptions &connectOptions);

private:
    // connect to data-system
    std::unique_ptr<datasystem::KVClient> kvClient_;
    std::unique_ptr<datasystem::ObjectClient> dsObjectClient_;

    bool isDSEnabled_{ false };
    bool isDSAuthEnable_{ false };
    std::mutex mut_;
};

}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_DISTRIBUTED_CACHE_CLIENT_DS_CACHE_CLIENT_H
