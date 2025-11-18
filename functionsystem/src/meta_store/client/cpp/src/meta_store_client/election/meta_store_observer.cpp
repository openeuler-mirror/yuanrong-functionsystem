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

#include "meta_store_observer.h"

#include "async/async.hpp"
#include "common/logs/logging.h"
#include "meta_store_client/utils/etcd_util.h"
#include "common/utils/meta_store_kv_operation.h"
#include "meta_store_election_client_strategy.h"

namespace functionsystem {
MetaStoreObserver::MetaStoreObserver(std::string name, std::function<void(LeaderResponse)> callback,
                                     const std::string &etcdTablePrefix,
                                     const std::function<void(uint64_t)> &closeMethod)
    : Observer(std::move(name), std::move(callback), etcdTablePrefix), closeMethod_(closeMethod)
{
}

void MetaStoreObserver::OnObserve(const LeaderResponse &leaderResponse)
{
    if (callback_) {
        callback_(leaderResponse);
    }
}

void MetaStoreObserver::Shutdown() noexcept
{
    if (closeMethod_) {
        YRLOG_DEBUG("shutdown observer({}) for key({})", observeID_, name_);
        isCanceled_ = true;
        closeMethod_(observeID_);
    }
}

uint64_t MetaStoreObserver::GetObserveID() const
{
    return observeID_;
}

void MetaStoreObserver::SetObserveID(uint64_t observeID)
{
    observeID_ = observeID;
}

std::string MetaStoreObserver::GetName() const
{
    return name_;
}

bool MetaStoreObserver::IsCanceled() const
{
    return isCanceled_;
}

std::function<void(LeaderResponse)> MetaStoreObserver::GetCallBack() const
{
    return callback_;
}
}  // namespace functionsystem