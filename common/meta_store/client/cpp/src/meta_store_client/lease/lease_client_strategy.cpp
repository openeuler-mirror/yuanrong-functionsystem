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

#include "lease_client_strategy.h"

#include "async/asyncafter.hpp"

namespace functionsystem::meta_store {
LeaseClientStrategy::LeaseClientStrategy(const std::string &name, const std::string &address,
                                         const MetaStoreTimeoutOption &timeoutOption)
    : ActorBase(name), address_(address), timeoutOption_(timeoutOption)
{
}

void LeaseClientStrategy::OnHealthyStatus(const Status &status)
{
    YRLOG_WARN("update lease client healthy status: {}", status.ToString());
    healthyStatus_ = status;
}
}  // namespace functionsystem::meta_store