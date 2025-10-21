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

#include "meta_storage_accessor/meta_storage_accessor.h"

#include <gtest/gtest.h>

#include <memory>

#include "mocks/mock_meta_store_client.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

using ::testing::Return;

class MetaStorageAccessorTest : public ::testing::Test {
public:
    void TearDown() override
    {
        // litebus::TerminateAll();
    }
};

static void MockMetaStoreClientPutWithLease(std::unique_ptr<MockMetaStoreClient> &mockMetaStoreClient,
                                            const std::string &key, const std::string &value, const int64_t leaseID,
                                            const int ttl)
{
    auto putResponse = std::make_shared<PutResponse>();
    putResponse->prevKv.set_key(key);
    putResponse->prevKv.set_value(value);

    EXPECT_CALL(*mockMetaStoreClient, Put).WillOnce(Return(putResponse));
    litebus::Future<LeaseGrantResponse> leaseGrantResponseFuture;
    leaseGrantResponseFuture.SetValue({ Status::OK(), ResponseHeader(), leaseID, ttl });
    EXPECT_CALL(*mockMetaStoreClient, Grant).WillOnce(Return(leaseGrantResponseFuture));
    litebus::Future<LeaseKeepAliveResponse> leaseKeepAliveResponseFuture;
    leaseKeepAliveResponseFuture.SetValue({ Status::OK(), ResponseHeader(), leaseID, ttl });
    EXPECT_CALL(*mockMetaStoreClient, KeepAliveOnce).WillRepeatedly(Return(leaseKeepAliveResponseFuture));
}

static void MockMetaStoreClientPutWithLeaseTimeout(std::unique_ptr<MockMetaStoreClient> &mockMetaStoreClient,
                                                   const std::string &key, const std::string &value,
                                                   const int64_t leaseID1, const int64_t leaseID2, const int ttl)
{
    auto putResponse = std::make_shared<PutResponse>();
    putResponse->prevKv.set_key(key);
    putResponse->prevKv.set_value(value);

    EXPECT_CALL(*mockMetaStoreClient, Put).WillOnce(Return(putResponse)).WillOnce(Return(putResponse));
    litebus::Future<LeaseGrantResponse> leaseGrantResponseFuture1;
    leaseGrantResponseFuture1.SetValue({ Status::OK(), ResponseHeader(), leaseID1, ttl });
    litebus::Future<LeaseGrantResponse> leaseGrantResponseFuture2;
    leaseGrantResponseFuture2.SetValue({ Status::OK(), ResponseHeader(), leaseID2, ttl });
    EXPECT_CALL(*mockMetaStoreClient, Grant)
        .WillOnce(Return(leaseGrantResponseFuture1))
        .WillOnce(Return(leaseGrantResponseFuture2));
    litebus::Future<LeaseKeepAliveResponse> leaseKeepAliveResponseFutureSuccess1;
    leaseKeepAliveResponseFutureSuccess1.SetValue({ Status::OK(), ResponseHeader(), leaseID1, ttl });
    litebus::Future<LeaseKeepAliveResponse> leaseKeepAliveResponseFutureSuccess2;
    leaseKeepAliveResponseFutureSuccess2.SetValue({ Status::OK(), ResponseHeader(), leaseID2, ttl });
    litebus::Future<LeaseKeepAliveResponse> leaseKeepAliveResponseFutureFailed;
    leaseKeepAliveResponseFutureFailed.SetValue({ Status::OK(), ResponseHeader(), leaseID1, 0 });
    EXPECT_CALL(*mockMetaStoreClient, KeepAliveOnce)
        .WillOnce(Return(leaseKeepAliveResponseFutureSuccess1))
        .WillOnce(Return(leaseKeepAliveResponseFutureSuccess1))
        .WillOnce(Return(leaseKeepAliveResponseFutureFailed))
        .WillRepeatedly(Return(leaseKeepAliveResponseFutureSuccess2));
}

TEST_F(MetaStorageAccessorTest, PutWithoutLease)
{
    const std::string testKey = "test key";
    const std::string testValue = "test value";

    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    std::string address = "127.0.0.1:" + std::to_string(port);
    std::unique_ptr<MockMetaStoreClient> mockMetaStoreClient = std::make_unique<MockMetaStoreClient>(address);

    auto putResponse = std::make_shared<PutResponse>();
    putResponse->prevKv.set_key(testKey);
    putResponse->prevKv.set_value(testValue);

    EXPECT_CALL(*mockMetaStoreClient, Put).WillOnce(Return(putResponse));

    MetaStorageAccessor accessor{ std::move(mockMetaStoreClient) };

    auto status = accessor.Put(testKey, testValue).Get();
    EXPECT_TRUE(status.IsOk());
}

TEST_F(MetaStorageAccessorTest, PutWithLease)
{
    const std::string testKey = "testKey";
    const std::string testValue = "testValue";
    const int64_t leaseID = 1;
    const int ttl = 100;

    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    std::string address = "127.0.0.1:" + std::to_string(port);
    std::unique_ptr<MockMetaStoreClient> mockMetaStoreClient = std::make_unique<MockMetaStoreClient>(address);

    MockMetaStoreClientPutWithLease(mockMetaStoreClient, testKey, testValue, leaseID, ttl);

    MetaStorageAccessor accessor{ std::move(mockMetaStoreClient) };
    auto status = accessor.PutWithLease(testKey, testValue, ttl).Get();
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_TRUE(status.IsOk());
}

TEST_F(MetaStorageAccessorTest, PutWithLeaseTimeout)
{
    const std::string testKey = "testKey";
    const std::string testValue = "testValue";
    const int64_t leaseID1 = 1;
    const int64_t leaseID2 = 2;
    const int ttl = 100;

    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    std::string address = "127.0.0.1:" + std::to_string(port);
    std::unique_ptr<MockMetaStoreClient> mockMetaStoreClient = std::make_unique<MockMetaStoreClient>(address);

    MockMetaStoreClientPutWithLeaseTimeout(mockMetaStoreClient, testKey, testValue, leaseID1, leaseID2, ttl);

    MetaStorageAccessor accessor{ std::move(mockMetaStoreClient) };
    auto status = accessor.PutWithLease(testKey, testValue, ttl).Get();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(status.IsOk());
}

TEST_F(MetaStorageAccessorTest, Get)
{
    const std::string testKey = "test key";
    const std::string testValue = "test value";

    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    std::string address = "127.0.0.1:" + std::to_string(port);
    std::unique_ptr<MockMetaStoreClient> mockMetaStoreClient = std::make_unique<MockMetaStoreClient>(address);

    litebus::Future<std::shared_ptr<GetResponse>> getResponseFuture;
    KeyValue kv;
    kv.set_key(testKey);
    kv.set_value(testValue);
    auto output = std::make_shared<GetResponse>();
    output->status = Status::OK();
    output->header = ResponseHeader();
    output->count = 1;
    output->kvs = { kv };
    getResponseFuture.SetValue(output);
    EXPECT_CALL(*mockMetaStoreClient, Get).WillOnce(Return(getResponseFuture));

    MetaStorageAccessor accessor{ std::move(mockMetaStoreClient) };
    auto result = accessor.Get(testKey);
    EXPECT_EQ(result.Get(), testValue);
}

TEST_F(MetaStorageAccessorTest, Delete)
{
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    std::string address = "127.0.0.1:" + std::to_string(port);
    std::unique_ptr<MockMetaStoreClient> mockMetaStoreClient = std::make_unique<MockMetaStoreClient>(address);

    auto deleteResponse = std::make_shared<DeleteResponse>();
    EXPECT_CALL(*mockMetaStoreClient, Delete).WillOnce(Return(deleteResponse));

    MetaStorageAccessor accessor{ std::move(mockMetaStoreClient) };
    auto status = accessor.Delete("key").Get();
    EXPECT_TRUE(status.IsOk());
}

TEST_F(MetaStorageAccessorTest, RevokeInvalidKey)
{
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    std::string address = "127.0.0.1:" + std::to_string(port);
    std::unique_ptr<MockMetaStoreClient> mockMetaStoreClient = std::make_unique<MockMetaStoreClient>(address);

    MetaStorageAccessor accessor{ std::move(mockMetaStoreClient) };
    auto status = accessor.Revoke("key").Get();
    EXPECT_EQ(status.StatusCode(), StatusCode::BP_LEASE_ID_NOT_FOUND);
}

TEST_F(MetaStorageAccessorTest, RevokeValidKey)
{
    const std::string testKey = "test key";
    const std::string testValue = "test value";
    const int64_t leaseID = 1;
    const int ttl = 100;

    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    std::string address = "127.0.0.1:" + std::to_string(port);
    std::unique_ptr<MockMetaStoreClient> mockMetaStoreClient = std::make_unique<MockMetaStoreClient>(address);

    MockMetaStoreClientPutWithLease(mockMetaStoreClient, testKey, testValue, leaseID, ttl);

    litebus::Future<LeaseRevokeResponse> leaseRevokeResponseFuture;
    leaseRevokeResponseFuture.SetValue({});
    EXPECT_CALL(*mockMetaStoreClient, Revoke).WillOnce(Return(leaseRevokeResponseFuture));

    MetaStorageAccessor accessor{ std::move(mockMetaStoreClient) };
    auto status = accessor.PutWithLease(testKey, testValue, ttl).Get();
    EXPECT_TRUE(status.IsOk());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto result = accessor.Revoke(testKey).Get();
    EXPECT_TRUE(result.IsOk());
}

}  // namespace functionsystem::test