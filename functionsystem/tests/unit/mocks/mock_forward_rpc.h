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

#ifndef UT_MOCKS_MOCK_FORWARD_RPC_H
#define UT_MOCKS_MOCK_FORWARD_RPC_H

#include <gmock/gmock.h>

#include "function_proxy/common/communication/rpc_client/forward_rpc.h"

namespace functionsystem::test {

class MockForwardRPC : public ForwardRPC {
public:
    MOCK_METHOD(litebus::Future<internal::ForwardCallResponse>, Call,
                (const std::shared_ptr<internal::ForwardCallRequest> &request), (override));
    MOCK_METHOD(litebus::Future<internal::ForwardCallResultResponse>, CallResult,
                (const internal::ForwardCallResultRequest &request), (override));
};

}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_FORWARD_RPC_H
