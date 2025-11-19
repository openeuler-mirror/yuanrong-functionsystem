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
#include "client.h"

#include <async/async.hpp>

#include "common/status/status.h"

namespace functionsystem::proxy {
void Client::UpdateDstAID(const litebus::AID &dst)
{
    dst_ = dst;
}

litebus::Future<internal::ForwardCallResponse> Client::Call(
    const std::shared_ptr<internal::ForwardCallRequest> &request)
{
    return internal::ForwardCallResponse();
}

litebus::Future<internal::ForwardCallResultResponse> Client::CallResult(
    const internal::ForwardCallResultRequest &request)
{
    return internal::ForwardCallResultResponse();
}
}  // namespace functionsystem::proxy
