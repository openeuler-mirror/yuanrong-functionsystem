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

#ifndef FUNCTIONSYSTEM_META_STORE_LEASE_CLIENT_H
#define FUNCTIONSYSTEM_META_STORE_LEASE_CLIENT_H

#include <async/future.hpp>

#include "meta_store_struct.h"

namespace functionsystem::meta_store {
class LeaseClient {
public:
    virtual ~LeaseClient() = default;

    /**
     * grant a lease.
     *
     * @param ttl the second to live.
     * @return the response of request.
     */
    virtual litebus::Future<LeaseGrantResponse> Grant(int ttl) = 0;

    /**
     * keep a lease to alive once by lease's id.
     *
     * @param lease_id lease's id.
     * @return the response of request.
     */
    virtual litebus::Future<LeaseKeepAliveResponse> KeepAliveOnce(int64_t leaseId) = 0;

    /**
     * revoke a lease by lease's id.
     *
     * @param lease_id lease's id.
     * @return the response of request.
     */
    virtual litebus::Future<LeaseRevokeResponse> Revoke(int64_t leaseId) = 0;

protected:
    LeaseClient() = default;
};
} // namespace functionsystem::meta_store

#endif // FUNCTIONSYSTEM_META_STORE_LEASE_CLIENT_H
