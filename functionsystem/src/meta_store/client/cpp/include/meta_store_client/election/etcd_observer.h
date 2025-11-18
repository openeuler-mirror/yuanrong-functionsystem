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

#ifndef COMMON_META_STORE_CLIENT_ELECTION_ETCD_OBSERVER_H
#define COMMON_META_STORE_CLIENT_ELECTION_ETCD_OBSERVER_H

#include "meta_store_client/meta_store_struct.h"
#include "common/rpc/client/grpc_client.h"
#include "etcd/server/etcdserver/api/v3election/v3electionpb/v3election.grpc.pb.h"
#include "observer.h"

namespace functionsystem {

class EtcdObserver : public Observer {
public:
    EtcdObserver(std::string name, std::function<void(LeaderResponse)> callback,
                 const std::shared_ptr<::grpc::Channel> &channel, const std::string &etcdTablePrefix);

    ~EtcdObserver() override;

    Status Start();

    void Shutdown() noexcept override;

private:
    std::shared_ptr<::grpc::Channel> channel_;

    // read observe stream by loop
    std::unique_ptr<std::thread> observeThread_;

    std::unique_ptr<::grpc::ClientContext> observeContext_;
    std::unique_ptr<::grpc::ClientReader<v3electionpb::LeaderResponse>> observeReader_;

    std::atomic<bool> running_{ false };

    Status Init();
    void OnObserve();
    void Stop() noexcept;
};

}  // namespace functionsystem

#endif  // COMMON_META_STORE_CLIENT_ELECTION_ETCD_OBSERVER_H
