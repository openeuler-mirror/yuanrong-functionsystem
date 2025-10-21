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

#ifndef TEST_UNIT_META_STORE_WATCH_STREAM_SRV_ACTOR_H
#define TEST_UNIT_META_STORE_WATCH_STREAM_SRV_ACTOR_H

#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "watch_service_actor.h"

namespace functionsystem::meta_store::test {
using Stream = ::grpc::ServerReaderWriter<::etcdserverpb::WatchResponse, ::etcdserverpb::WatchRequest>;

class EtcdWatchSrvActor : public WatchServiceActor {
public:
    EtcdWatchSrvActor(const std::string &actorName) : WatchServiceActor(actorName)
    {
    }

    bool Cancel(Stream *grpcStream, const ::etcdserverpb::WatchCancelRequest &request);

    bool Create(Stream *grpcStream, const ::etcdserverpb::WatchCreateRequest &request);

    void OnDelete(const ::mvccpb::KeyValue &prevKv) override;

    bool RemoveClient(Stream *grpcStream);

    bool Response(Stream *grpcStream);

    void OnDeleteList(std::shared_ptr<std::vector<::mvccpb::KeyValue>> kvs) override;

    void OnPut(const ::mvccpb::KeyValue &kv, const ::mvccpb::KeyValue &prevKv) override;

private:
    std::unordered_map<Stream *, int64_t> indexByClient_;
    std::unordered_map<Stream *, std::unordered_map<int64_t, ::etcdserverpb::WatchCreateRequest>> observers_;
};
}  // namespace functionsystem::meta_store::test

#endif  // TEST_UNIT_META_STORE_WATCH_STREAM_SRV_ACTOR_H
