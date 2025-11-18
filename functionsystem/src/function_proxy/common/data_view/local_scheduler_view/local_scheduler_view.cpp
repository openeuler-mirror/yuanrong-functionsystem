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
#include "local_scheduler_view.h"

#include "common/logs/logging.h"

namespace functionsystem::function_proxy {
std::shared_ptr<litebus::AID> LocalSchedulerView::Get(const std::string &proxyID)
{
    if (localSchedulers_.find(proxyID) == localSchedulers_.end()) {
        return nullptr;
    }

    return localSchedulers_[proxyID];
}

void LocalSchedulerView::Update(const std::string &proxyID, const std::shared_ptr<litebus::AID> &aid)
{
    YRLOG_DEBUG("update local, proxyID: {}, aid: {}", proxyID, aid->HashString());
    localSchedulers_[proxyID] = aid;
}

void LocalSchedulerView::Delete(const std::string &proxyID)
{
    YRLOG_DEBUG("delete local, proxyID: {}", proxyID);
    (void)localSchedulers_.erase(proxyID);
}

void LocalSchedulerView::Clear()
{
    localSchedulers_.clear();
}

}  // namespace functionsystem::function_proxy