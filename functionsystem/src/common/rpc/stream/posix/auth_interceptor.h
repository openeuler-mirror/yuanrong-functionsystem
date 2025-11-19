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

#ifndef COMMON_RPC_STREAM_POSIX_AUTH_INTERCEPTOR_H
#define COMMON_RPC_STREAM_POSIX_AUTH_INTERCEPTOR_H

#include "async/future.hpp"
namespace functionsystem {
template <typename Message>
class AuthInterceptor {
public:
    virtual litebus::Future<bool> Sign(const std::shared_ptr<Message> &message) = 0;
    virtual litebus::Future<bool> Verify(const std::shared_ptr<Message> &message) = 0;

    virtual ~AuthInterceptor() = default;
};
}
#endif  // COMMON_RPC_STREAM_POSIX_AUTH_INTERCEPTOR_H