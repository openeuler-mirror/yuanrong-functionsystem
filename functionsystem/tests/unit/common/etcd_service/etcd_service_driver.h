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

#ifndef FUNCTION_MASTER_META_STORE_META_STORE_SERVICE_H
#define FUNCTION_MASTER_META_STORE_META_STORE_SERVICE_H

#include "kv_service_accessor_actor.h"
#include "kv_service_actor.h"
#include "lease_service_actor.h"
#include "grpcpp/server.h"

namespace functionsystem::meta_store::test {
class EtcdServiceDriver {
public:
    void StartServer(const std::string &address, const std::string &prefix = "");

    void StopServer();

private:
    std::shared_ptr<::grpc::Server> server_ = nullptr;

    std::shared_ptr<meta_store::KvServiceActor> kvActor_;
    std::shared_ptr<meta_store::KvServiceAccessorActor> kvAccessorActor_;
    std::shared_ptr<meta_store::LeaseServiceActor> leaseActor_;
};
}  // namespace functionsystem::meta_store::test

#endif  // FUNCTION_MASTER_META_STORE_META_STORE_SERVICE_H
