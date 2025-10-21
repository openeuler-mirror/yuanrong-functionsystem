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

#ifndef FUNCTION_MASTER_META_STORE_ETCD_WATCH_SERVICE_H
#define FUNCTION_MASTER_META_STORE_ETCD_WATCH_SERVICE_H

#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "etcd_watch_srv_actor.h"
#include "kv_service_actor.h"

namespace functionsystem::meta_store::test {
class EtcdWatchService final : public etcdserverpb::Watch::Service {
public:
    EtcdWatchService() = delete;

    explicit EtcdWatchService(std::shared_ptr<KvServiceActor> actor);

    ~EtcdWatchService() override;

    ::grpc::Status Watch(::grpc::ServerContext *context, Stream *stream) override;

private:
    std::shared_ptr<KvServiceActor> kvActor_;
    std::shared_ptr<EtcdWatchSrvActor> streamActor_;
};  // class EtcdWatchService
}  // namespace functionsystem::meta_store::test
#endif  // FUNCTION_MASTER_META_STORE_ETCD_WATCH_SERVICE_H