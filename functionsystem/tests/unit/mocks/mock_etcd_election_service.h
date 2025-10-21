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

#ifndef UT_MOCKS_MOCK_ETCD_ELECTION_SERVICE_H
#define UT_MOCKS_MOCK_ETCD_ELECTION_SERVICE_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "etcd/server/etcdserver/api/v3election/v3electionpb/v3election.grpc.pb.h"

namespace functionsystem::test {
class MockEtcdElectionService final : public v3electionpb::Election::Service {
public:
    MockEtcdElectionService() = default;
    ~MockEtcdElectionService() override = default;

    MOCK_METHOD(::grpc::Status, Campaign,
                (::grpc::ServerContext *, const ::v3electionpb::CampaignRequest *, ::v3electionpb::CampaignResponse *),
                (override));
    MOCK_METHOD(::grpc::Status, Leader,
                (::grpc::ServerContext *, const ::v3electionpb::LeaderRequest *, ::v3electionpb::LeaderResponse *),
                (override));
    MOCK_METHOD(::grpc::Status, Resign,
                (::grpc::ServerContext *, const ::v3electionpb::ResignRequest *, ::v3electionpb::ResignResponse *),
                (override));

    ::grpc::Status Observe(::grpc::ServerContext *context, const ::v3electionpb::LeaderRequest *request,
                           ::grpc::ServerWriter< ::v3electionpb::LeaderResponse> *writer) override
    {
        isRunning_ = true;
        while (isRunning_) {
            std::lock_guard<std::mutex> lock{ mut_ };
            if (hasNewMsg_) {
                writer->Write(msg_);
                hasNewMsg_ = false;
            }
        }
        return ::grpc::Status::OK;
    }

    void ObserveEvent(const ::v3electionpb::LeaderResponse &response)
    {
        std::lock_guard<std::mutex> lock{ mut_ };
        msg_ = response;
        hasNewMsg_ = true;
    }

    void ShutdownObserver()
    {
        isRunning_ = false;
    }

private:
    bool isRunning_ = false;
    bool hasNewMsg_ = false;
    ::v3electionpb::LeaderResponse msg_;
    std::mutex mut_;
};
}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_ETCD_ELECTION_SERVICE_H
