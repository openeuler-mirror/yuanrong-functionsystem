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

#include "election_client_strategy.h"

#include "async/asyncafter.hpp"
#include "meta_store_client/utils/etcd_util.h"
#include "common/utils/meta_store_kv_operation.h"
#include "common/utils/random_number.h"

namespace functionsystem::meta_store {

ElectionClientStrategy::ElectionClientStrategy(const std::string &name, const std::string &address,
                                               const MetaStoreTimeoutOption &timeoutOption,
                                               const std::string &etcdTablePrefix)
    : litebus::ActorBase(name), address_(address), etcdTablePrefix_(etcdTablePrefix), timeoutOption_(timeoutOption)
{
}

void ElectionClientStrategy::OnHealthyStatus(const Status &status)
{
    YRLOG_WARN("update election client healthy status: {}", status.ToString());
    healthyStatus_ = status;
}
}  // namespace functionsystem::meta_store
