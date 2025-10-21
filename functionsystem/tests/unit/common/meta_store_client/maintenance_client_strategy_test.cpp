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

#include <gtest/gtest.h>

#include "async/future.hpp"
#include "meta_store_client/maintenance/meta_store_maintenance_client_strategy.h"
#include "proto/pb/message_pb.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;
using namespace testing;
static MetaStoreTimeoutOption metaStoreTimeoutOpt = {
    .operationRetryIntervalLowerBound = 100,
    .operationRetryIntervalUpperBound = 200,
    .operationRetryTimes = 2,
    .grpcTimeout = 1,
};

class MockMaintenanceServiceActor : public litebus::ActorBase {
public:
    MockMaintenanceServiceActor() : ActorBase("MaintenanceServiceActor")
    {
    }

    MOCK_METHOD(::etcdserverpb::StatusResponse, HealthCheck, (const ::etcdserverpb::StatusRequest &));

    void HealthCheck(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        messages::MetaStoreRequest req;
        RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse HealthCheck MetaStoreResponse");

        ::etcdserverpb::StatusRequest request;
        messages::MetaStoreResponse res;
        res.set_responseid(req.requestid());
        res.set_responsemsg(HealthCheck(request).SerializeAsString());
        Send(from, "OnHealthCheck", res.SerializeAsString());
    }

protected:
    void Init() override
    {
        Receive("HealthCheck", &MockMaintenanceServiceActor::HealthCheck);
    }
};

class MaintenanceClientStrategyTest : public ::testing::Test {
public:
    MaintenanceClientStrategyTest() = default;

    ~MaintenanceClientStrategyTest() override = default;

    [[maybe_unused]] static void SetUpTestCase()
    {
        mockMaintenanceService_ = std::make_shared<MockMaintenanceServiceActor>();
        litebus::Spawn(mockMaintenanceService_);
    }

    [[maybe_unused]] static void TearDownTestCase()
    {
        litebus::Terminate(mockMaintenanceService_->GetAID());
        litebus::Await(mockMaintenanceService_->GetAID());
        mockMaintenanceService_ = nullptr;
    }

protected:
    void SetUp() override
    {
        client_ = MakeMetaStoreMaintenanceClientStrategy();
        litebus::Spawn(client_);
    }

    void TearDown() override
    {
        litebus::Terminate(client_->GetAID());
        litebus::Await(client_);
        client_ = nullptr;
    }

    static std::shared_ptr<MetaStoreMaintenanceClientStrategy> MakeMetaStoreMaintenanceClientStrategy()
    {
        auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        return std::make_shared<MetaStoreMaintenanceClientStrategy>(
            "meta_store_maintenance_client" + uuid.ToString(), "127.0.0.1:" + std::to_string(port),
            std::make_shared<MetaStoreDefaultExplorer>("127.0.0.1:" + std::to_string(port)), metaStoreTimeoutOpt);
    }

protected:
    std::shared_ptr<MetaStoreMaintenanceClientStrategy> client_ = nullptr;
    inline static std::shared_ptr<MockMaintenanceServiceActor> mockMaintenanceService_ = nullptr;
};

TEST_F(MaintenanceClientStrategyTest, HealthCheckTest)
{
    litebus::Future<::etcdserverpb::StatusRequest> req;
    ::etcdserverpb::StatusResponse ret;
    EXPECT_CALL(*mockMaintenanceService_, HealthCheck).WillOnce(DoAll(test::FutureArg<0>(&req), Return(ret)));
    auto resp = litebus::Async(client_->GetAID(), &MetaStoreMaintenanceClientStrategy::HealthCheck);
    ASSERT_AWAIT_READY(req);
    ASSERT_AWAIT_READY(resp);
    EXPECT_TRUE(resp.Get().status.IsOk());

    req = {};
    ret.mutable_errors()->Add("-1");
    EXPECT_CALL(*mockMaintenanceService_, HealthCheck).WillOnce(DoAll(test::FutureArg<0>(&req), Return(ret)));
    resp = litebus::Async(client_->GetAID(), &MetaStoreMaintenanceClientStrategy::HealthCheck);
    ASSERT_AWAIT_READY(req);
    ASSERT_AWAIT_READY(resp);
    EXPECT_TRUE(resp.Get().status.IsError());
    EXPECT_EQ(resp.Get().status.StatusCode(), StatusCode::FAILED);
}

TEST_F(MaintenanceClientStrategyTest, ReconnectTest)
{
    litebus::Promise<std::string> reconnected;
    auto callback = [&](const std::string &address) { reconnected.SetValue(address); };
    litebus::Async(client_->GetAID(), &MetaStoreMaintenanceClientStrategy::BindReconnectedCallBack, callback);
    litebus::Async(client_->GetAID(), &MetaStoreMaintenanceClientStrategy::Reconnected);
    ASSERT_AWAIT_READY(reconnected.GetFuture());
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    EXPECT_EQ(reconnected.GetFuture().Get(), "127.0.0.1:" + std::to_string(port));

    litebus::Async(client_->GetAID(), &MetaStoreMaintenanceClientStrategy::Exited, litebus::AID());
    litebus::Async(client_->GetAID(), &MetaStoreMaintenanceClientStrategy::TryReconnect);
    litebus::Async(client_->GetAID(), &MetaStoreMaintenanceClientStrategy::ReconnectSuccess);
    litebus::Async(client_->GetAID(), &MetaStoreMaintenanceClientStrategy::OnAddressUpdated, "");
}
}  // namespace functionsystem::meta_store::test
