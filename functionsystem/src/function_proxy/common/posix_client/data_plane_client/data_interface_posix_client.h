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

#ifndef FUNCTION_PROXY_COMMON_POSIX_CLIENT_DATA_INTERFACE_POSIX_CLIENT_H
#define FUNCTION_PROXY_COMMON_POSIX_CLIENT_DATA_INTERFACE_POSIX_CLIENT_H

#include "function_proxy/common/posix_client/base_client.h"

namespace functionsystem {
/**
 * DataInterfacePosixClient only support interface of Call and NotifyResult, which can be inherited from BaseClient
 */
class DataInterfacePosixClient : virtual public BaseClient {
public:
    explicit DataInterfacePosixClient(const std::shared_ptr<grpc::PosixClient> &posix) : BaseClient(posix) {}
    ~DataInterfacePosixClient() override = default;
};
}


#endif  // FUNCTION_PROXY_COMMON_POSIX_CLIENT_DATA_INTERFACE_POSIX_CLIENT_H
