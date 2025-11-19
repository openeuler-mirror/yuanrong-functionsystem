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
#include "proxy_view.h"

#include "common/logs/logging.h"

namespace functionsystem {
std::shared_ptr<proxy::Client> ProxyView::Get(const std::string &proxyID)
{
    if (proxyClients_.find(proxyID) == proxyClients_.end()) {
        return nullptr;
    }

    return proxyClients_[proxyID];
}

void ProxyView::Update(const std::string &proxyID, const std::shared_ptr<proxy::Client> &client)
{
    RETURN_IF_NULL(client);
    YRLOG_DEBUG("update proxy, proxyID: {}, client info: {}", proxyID, client->GetClientInfo());
    proxyClients_[proxyID] = client;

    if (proxyUpdateCbFunc_.find(proxyID) == proxyUpdateCbFunc_.end()) {
        return;
    }
    for (auto cb : proxyUpdateCbFunc_[proxyID]) {
        if (cb) {
            cb(client);
        }
    }
    (void)proxyUpdateCbFunc_.erase(proxyID);
}

void ProxyView::SetUpdateCbFunc(const std::string &proxyID, const UpdateCbFunc &updateCbFunc)
{
    YRLOG_DEBUG("set proxy({}) update callback function", proxyID);
    (void)proxyUpdateCbFunc_[proxyID].emplace_back(updateCbFunc);
}

void ProxyView::Delete(const std::string &proxyID)
{
    YRLOG_DEBUG("delete proxy, proxyID: {}", proxyID);
    (void)proxyClients_.erase(proxyID);
}

void ProxyView::ClearProxyClient()
{
    YRLOG_DEBUG("clear proxy client");
    proxyClients_.clear();
}

}  // namespace functionsystem