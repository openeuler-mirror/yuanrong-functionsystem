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

#ifndef FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_FORWARD_INTERFACE_H
#define FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_FORWARD_INTERFACE_H
#include "async/future.hpp"
#include "common/proto/pb/posix_pb.h"
namespace functionsystem::busproxy {
class ForwardInterface {
public:
    ForwardInterface() = default;
    virtual ~ForwardInterface() = default;
    virtual litebus::Future<SharedStreamMsg> SendForwardCall(const litebus::AID &aid, const std::string &callerTenantID,
                                                             const SharedStreamMsg &request) = 0;

    virtual litebus::Future<SharedStreamMsg> SendForwardCallResult(const litebus::AID &aid,
                                                                   const SharedStreamMsg &request) = 0;
};
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_FORWARD_INTERFACE_H
