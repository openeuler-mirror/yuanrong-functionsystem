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

#ifndef UT_MOCKS_MOCK_ETCD_WATCH_SERVICE_H
#define UT_MOCKS_MOCK_ETCD_WATCH_SERVICE_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "async/async.hpp"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::test {
class MockEtcdWatchActor : public litebus::ActorBase {
public:
    MockEtcdWatchActor() : litebus::ActorBase("mock_etcd_watch_actor")
    {
    }

    MOCK_METHOD(void, Create, (const ::etcdserverpb::WatchCreateRequest &request));
    MOCK_METHOD(void, Cancel, (const ::etcdserverpb::WatchCancelRequest &request));

    bool Response(const ::etcdserverpb::WatchResponse &response)
    {
        return stream_->Write(response);
    }

    void BindStream(::grpc::ServerReaderWriter<::etcdserverpb::WatchResponse, ::etcdserverpb::WatchRequest> *stream)
    {
        stream_ = stream;
    }
    ::grpc::ServerReaderWriter<::etcdserverpb::WatchResponse, ::etcdserverpb::WatchRequest> *stream_{};
};

class MockEtcdWatchService final : public etcdserverpb::Watch::Service {
public:
    MockEtcdWatchService() = default;

    ~MockEtcdWatchService() override = default;

    void BindActor(const std::shared_ptr<MockEtcdWatchActor> &actor)
    {
        actor_ = actor;
    }

    ::grpc::Status Watch(
        ::grpc::ServerContext *context,
        ::grpc::ServerReaderWriter<::etcdserverpb::WatchResponse, ::etcdserverpb::WatchRequest> *stream) override
    {
        isRunning_ = true;
        actor_->BindStream(stream);

        ::etcdserverpb::WatchRequest request;
        while (isRunning_ && stream->Read(&request)) {
            switch (request.request_union_case()) {
                case ::etcdserverpb::WatchRequest::RequestUnionCase::kCreateRequest:
                    litebus::Async(actor_->GetAID(), &MockEtcdWatchActor::Create, request.create_request());
                    break;
                case ::etcdserverpb::WatchRequest::RequestUnionCase::kCancelRequest:
                    litebus::Async(actor_->GetAID(), &MockEtcdWatchActor::Cancel, request.cancel_request());
                    break;
                case etcdserverpb::WatchRequest::kProgressRequest:
                case etcdserverpb::WatchRequest::REQUEST_UNION_NOT_SET:
                    break;
            }
        }
        return ::grpc::Status::OK;
    }

    void ShutdownWatch()
    {
        isRunning_ = false;
    }

private:
    bool isRunning_ = false;
    std::shared_ptr<MockEtcdWatchActor> actor_;
};
}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_ETCD_WATCH_SERVICE_H
