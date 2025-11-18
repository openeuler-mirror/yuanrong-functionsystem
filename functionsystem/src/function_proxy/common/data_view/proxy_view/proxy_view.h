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
#ifndef PROXY_VIEW_H
#define PROXY_VIEW_H

#include <optional>
#include <unordered_map>

#include "common/status/status.h"
#include "function_proxy/common/communication/proxy/client.h"
#include "function_proxy/common/communication/rpc_client/forward_rpc.h"

namespace functionsystem {
class ProxyView {
public:
    using UpdateCbFunc = std::function<void(const std::shared_ptr<proxy::Client> &)>;

    ProxyView() = default;

    ~ProxyView() = default;
    std::shared_ptr<proxy::Client> Get(const std::string &proxyID);
    void Update(const std::string &proxyID, const std::shared_ptr<proxy::Client> &client);
    void SetUpdateCbFunc(const std::string &proxyID, const UpdateCbFunc &updateCbFunc);
    void Delete(const std::string &proxyID);
    void ClearProxyClient();

private:
    std::unordered_map<std::string, std::shared_ptr<proxy::Client>> proxyClients_;
    std::unordered_map<std::string, std::vector<UpdateCbFunc>> proxyUpdateCbFunc_;
};
}  // namespace functionsystem
#endif
