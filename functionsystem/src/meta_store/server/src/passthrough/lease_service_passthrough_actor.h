/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTION_MASTER_META_STORE_LEASE_SERVICE_PASSTHROUGH_ACTOR_H
#define FUNCTION_MASTER_META_STORE_LEASE_SERVICE_PASSTHROUGH_ACTOR_H

#include "meta_store_client/meta_store_client.h"
#include "meta_store/server/src/lease_service_actor.h"

namespace functionsystem::meta_store {

class LeaseServicePassthroughActor : public LeaseServiceActor {
public:
    explicit LeaseServicePassthroughActor(const std::shared_ptr<MetaStoreClient> &etcdClient)
        : LeaseServiceActor(litebus::AID()), etcdClient_(etcdClient)
    {
    }

    ~LeaseServicePassthroughActor() override = default;

    void ReceiveGrant(const litebus::AID &from, std::string &&name, std::string &&msg) override;

    void ReceiveRevoke(const litebus::AID &from, std::string &&name, std::string &&msg) override;

    void ReceiveKeepAlive(const litebus::AID &from, std::string &&name, std::string &&msg) override;

private:
    void OnGrant(const litebus::Future<LeaseGrantResponse> &response, const std::string &id, const litebus::AID &aid);

    void OnRevoke(const litebus::Future<LeaseRevokeResponse> &response, const std::string &id, const litebus::AID &aid);

    void OnKeepAlive(const litebus::Future<LeaseKeepAliveResponse> &response, const std::string &id,
                     const litebus::AID &aid);

    std::shared_ptr<MetaStoreClient> etcdClient_;
};

}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_LEASE_SERVICE_PASSTHROUGH_ACTOR_H
