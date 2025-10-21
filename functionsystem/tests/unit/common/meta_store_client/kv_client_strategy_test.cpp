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

#include "meta_store_client/key_value/kv_client_strategy.h"

#include <gtest/gtest.h>

#include "async/future.hpp"
#include "common/etcd_service/etcd_service_driver.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/key_value/meta_store_kv_client_strategy.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::meta_store::test {
using namespace functionsystem::test;
const std::string ETCD_KV_CLIENT_NAME = "etcd_kv_client_";    // NOLINT
const std::string META_KV_CLIENT_NAME = "meta_kv_client_";    // NOLINT
static GrpcSslConfig sslConfig{};
static MetaStoreTimeoutOption metaStoreTimeoutOpt = {
    .operationRetryIntervalLowerBound = 100,
    .operationRetryIntervalUpperBound = 200,
    .operationRetryTimes = 2,
    .grpcTimeout = 1000,
};

void PrepareKVs(const std::shared_ptr<KvClientStrategy> &client);
void ClearKVs(const std::shared_ptr<KvClientStrategy> &client);
void PutTest(const std::shared_ptr<KvClientStrategy> &client);

void DeleteKeyValue(const std::shared_ptr<KvClientStrategy> &client);
void DeleteKeyValuePrevKv(const std::shared_ptr<KvClientStrategy> &client);
void DeleteKeyValuePrefix(const std::shared_ptr<KvClientStrategy> &client);
void DeleteKeyValuePrevPrefix(const std::shared_ptr<KvClientStrategy> &client);

void GetTest(const std::shared_ptr<KvClientStrategy> &client);

void TransactionTxn(const std::shared_ptr<meta_store::KvClientStrategy> &client);
void TransactionWithReqTxn(const std::shared_ptr<meta_store::KvClientStrategy> &client);
void TransactionTxnTest(const std::shared_ptr<meta_store::KvClientStrategy> &client);
void TransactionTxnElse(const std::shared_ptr<meta_store::KvClientStrategy> &client);

class EtcdKvClientStrategyTest : public ::testing::Test {
public:
    EtcdKvClientStrategyTest() = default;
    ~EtcdKvClientStrategyTest() override = default;
    [[maybe_unused]] static void SetUpTestCase()
    {
        etcdSrvDriver_ = std::make_unique<EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
    }

    [[maybe_unused]] static void TearDownTestCase()
    {
        etcdSrvDriver_->StopServer();
    }

protected:
    void SetUp() override
    {
        client_ = MakeEtcdKvClientStrategy();
        PrepareKVs(client_);
        litebus::Spawn(client_);
    }

    void TearDown() override
    {
        litebus::Terminate(client_->GetAID());
        litebus::Await(client_);
        client_ = MakeEtcdKvClientStrategy();  // create new client
        ClearKVs(client_);
    }

    std::shared_ptr<KvClientStrategy> MakeEtcdKvClientStrategy()
    {
        auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
        return std::make_shared<EtcdKvClientStrategy>(ETCD_KV_CLIENT_NAME + uuid.ToString(), metaStoreServerHost_,
                                                      metaStoreTimeoutOpt);
    }

protected:
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::string metaStoreServerHost_;

protected:
    std::shared_ptr<KvClientStrategy> client_ = nullptr;
};

class MetaStoreKvClientStrategyTest : public ::testing::Test {
public:
    MetaStoreKvClientStrategyTest() = default;
    ~MetaStoreKvClientStrategyTest() override = default;
    [[maybe_unused]] static void SetUpTestCase()
    {
        etcdSrvDriver_ = std::make_unique<EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        metaStoreServerHost_ = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost_);
    }

    [[maybe_unused]] static void TearDownTestCase()
    {
        etcdSrvDriver_->StopServer();
    }

protected:
    void SetUp() override
    {
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        metaStoreClinetAddr_ = "127.0.0.1:" + std::to_string(port);
        client_ = MakeMetaStoreKvClientStrategy();
        litebus::Spawn(client_);
        PrepareKVs(client_);
    }

    void TearDown() override
    {
        litebus::Terminate(client_->GetAID());
        litebus::Await(client_);
        client_ = MakeMetaStoreKvClientStrategy();  // create new client
        litebus::Spawn(client_);
        ClearKVs(client_);
        litebus::Terminate(client_->GetAID());
        litebus::Await(client_);
    }

    static std::shared_ptr<MetaStoreKvClientStrategy> MakeMetaStoreKvClientStrategy()
    {
        auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        return std::make_shared<MetaStoreKvClientStrategy>(META_KV_CLIENT_NAME + uuid.ToString(),
                                                           "127.0.0.1:" + std::to_string(port), metaStoreTimeoutOpt);
    }

private:
    inline static std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
    inline static std::string metaStoreServerHost_;
    std::string metaStoreClinetAddr_;

protected:
    std::shared_ptr<KvClientStrategy> client_ = nullptr;
};

void PrepareKVs(const std::shared_ptr<KvClientStrategy> &client)
{
    PutOption op = { .leaseId = 0, .prevKv = false };
    client->Put("llt/sn/workers/xxx", "1.0", op).Get();
    client->Put("llt/sn/workers/yyy", "1.0", op).Get();
    client->Put("llt/sn/workers/zzz", "1.0", op).Get();
    client->Put("llt/sn/proxy/zzz", "1.0", op).Get();
}

void ClearKVs(const std::shared_ptr<KvClientStrategy> &client)
{
    DeleteOption op = { .prevKv = false, .prefix = true };
    client->Delete("llt/sn/workers/", op).Get();  // delete all llt
}

void PutTest(const std::shared_ptr<KvClientStrategy> &client)
{
    PutOption op = { .leaseId = 0, .prevKv = false };
    auto response = client->Put("llt/sn/workers/xxx", "2.0", op).Get();
    EXPECT_TRUE(response->prevKv.key().empty());
    EXPECT_TRUE(response->prevKv.value().empty());

    op.prevKv = true;  // return prev key-value
    response = client->Put("llt/sn/workers/xxx", "3.0", op).Get();
    EXPECT_EQ(response->prevKv.key(), "llt/sn/workers/xxx");
    EXPECT_EQ(response->prevKv.value(), "2.0");
}

void DeleteKeyValue(const std::shared_ptr<KvClientStrategy> &client)
{
    DeleteOption op = { false, false };
    auto response = client->Delete("llt/sn/workers/xxx", op).Get();
    EXPECT_EQ(response->deleted, 1);
    EXPECT_EQ(response->prevKvs.size(), static_cast<uint32_t>(0));
}

void DeleteKeyValuePrevKv(const std::shared_ptr<KvClientStrategy> &client)
{
    DeleteOption op = { .prevKv = true, .prefix = false };
    auto response = client->Delete("llt/sn/workers/xxx", op).Get();
    EXPECT_EQ(response->deleted, 1);
    EXPECT_EQ(response->prevKvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(response->prevKvs[0].key(), "llt/sn/workers/xxx");
    EXPECT_EQ(response->prevKvs[0].value(), "1.0");
}

void DeleteKeyValuePrefix(const std::shared_ptr<KvClientStrategy> &client)
{
    DeleteOption op = { false, true };
    auto response = client->Delete("llt/sn/workers/", op).Get();
    EXPECT_EQ(response->deleted, 3);
    EXPECT_TRUE(response->prevKvs.empty());
}

void DeleteKeyValuePrevPrefix(const std::shared_ptr<KvClientStrategy> &client)
{
    DeleteOption op = { true, true };
    auto response = client->Delete("llt/sn/workers/", op).Get();
    EXPECT_EQ(response->deleted, 3);
    EXPECT_EQ(response->prevKvs.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(response->prevKvs[0].key(), "llt/sn/workers/xxx");
    EXPECT_EQ(response->prevKvs[0].value(), "1.0");
}

void GetTest(const std::shared_ptr<KvClientStrategy> &client)
{
    GetOption op = { false, false, false, 0, SortOrder::DESCEND, SortTarget::MODIFY };
    auto response = client->Get("llt/sn/workers/xxx", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(response->kvs[0].key(), "llt/sn/workers/xxx");
    EXPECT_EQ(response->kvs[0].value(), "1.0");

    op.prefix = false, op.keysOnly = false, op.countOnly = true;
    response = client->Get("llt/sn/workers/xxx", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(0));
    EXPECT_EQ(response->count, 1);

    op.prefix = true, op.keysOnly = false, op.countOnly = true;
    response = client->Get("llt/sn/workers/", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(0));
    EXPECT_EQ(response->count, 3);

    op.prefix = true, op.keysOnly = false, op.countOnly = false;
    response = client->Get("llt/sn/workers/", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(response->kvs[0].key(), "llt/sn/workers/zzz");  // DESCEND by MODIFY
    EXPECT_EQ(response->kvs[0].value(), "1.0");
    EXPECT_EQ(response->count, 3);
    op.prefix = true, op.keysOnly = true, op.countOnly = false, op.sortTarget = SortTarget::KEY;
    response = client->Get("llt/sn/workers/", op).Get();
    EXPECT_EQ(response->kvs.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(response->kvs[0].key(), "llt/sn/workers/zzz");  // DESCEND by MODIFY
    EXPECT_TRUE(response->kvs[0].value().empty());
}

void TransactionTxn(const std::shared_ptr<meta_store::KvClientStrategy> &client)
{
    auto transaction = std::make_unique<meta_store::TxnTransaction>(client->GetAID());
    transaction->If(TxnCompare::OfValue("llt/sn/workers/xxx", CompareOperator::EQUAL, "1.0"));
    DeleteOption delOption = { true, false };
    transaction->Then(TxnOperation::Create("llt/sn/workers/xxx", delOption));
    PutOption putOption = { .leaseId = 0, .prevKv = true };
    transaction->Then(TxnOperation::Create("llt/sn/workers/yyy", "2.0", putOption));
    GetOption getOption = { true, false, false, 0, SortOrder::DESCEND, SortTarget::KEY };
    transaction->Then(TxnOperation::Create("llt/sn/workers/", getOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/zzz", "2.0", putOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/yyy", delOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/", getOption));
    std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();
    EXPECT_NE(txnResponse, nullptr);
    EXPECT_TRUE(txnResponse->success);
    EXPECT_EQ(txnResponse->responses.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[0].response).prevKvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[0].response).prevKvs[0].key(), "llt/sn/workers/xxx");
    EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[1].response).prevKv.key(), "llt/sn/workers/yyy");
    EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[1].response).prevKv.value(), "1.0");
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs.size(), static_cast<uint32_t>(2));
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].key(), "llt/sn/workers/zzz");
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].value(), "1.0");
}

void TransactionWithReqTxn(const std::shared_ptr<meta_store::KvClientStrategy> &client){
    auto transaction = std::make_unique<meta_store::TxnTransaction>(client->GetAID());
    transaction->If(TxnCompare::OfValue("llt/sn/workers/xxx", CompareOperator::EQUAL, "1.0"));
    DeleteOption delOption = { true, false };
    transaction->Then(TxnOperation::Create("llt/sn/workers/xxx", delOption));
    PutOption putOption = { .leaseId = 0, .prevKv = true };
    transaction->Then(TxnOperation::Create("llt/sn/workers/yyy", "2.0", putOption));
    GetOption getOption = { true, false, false, 0, SortOrder::DESCEND, SortTarget::KEY };
    transaction->Then(TxnOperation::Create("llt/sn/workers/", getOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/zzz", "2.0", putOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/yyy", delOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/", getOption));

    ::etcdserverpb::TxnRequest request;
    client->BuildTxnRequest(request, transaction->compares, transaction->thenOps, transaction->elseOps);

    auto response = client->CommitWithReq(request, true);
    ASSERT_AWAIT_READY(response);

    auto txnResponse = std::make_shared<TxnResponse>() ;
    client->Convert(response.Get(), txnResponse);
    EXPECT_NE(txnResponse, nullptr);
    EXPECT_TRUE(txnResponse->success);
    EXPECT_EQ(txnResponse->responses.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[0].response).prevKvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[0].response).prevKvs[0].key(), "llt/sn/workers/xxx");
    EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[1].response).prevKv.key(), "llt/sn/workers/yyy");
    EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[1].response).prevKv.value(), "1.0");
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs.size(), static_cast<uint32_t>(2));
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].key(), "llt/sn/workers/zzz");
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].value(), "1.0");
}

void TransactionTxnTest(const std::shared_ptr<meta_store::KvClientStrategy> &client)
{
    auto transaction = std::make_unique<meta_store::TxnTransaction>(client->GetAID());
    transaction->If(TxnCompare::OfVersion("llt/sn/workers/xxx", CompareOperator::EQUAL, 1));
    transaction->If(TxnCompare::OfCreateVersion("llt/sn/workers/xxx", CompareOperator::GREATER, 1));
    transaction->If(TxnCompare::OfModifyVersion("llt/sn/workers/xxx", CompareOperator::LESS, 100));
    transaction->If(TxnCompare::OfLease("llt/sn/workers/xxx", CompareOperator::EQUAL, 1));
    std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();
    EXPECT_FALSE(txnResponse->success);
}

void TransactionTxnElse(const std::shared_ptr<meta_store::KvClientStrategy> &client)
{
    auto transaction = std::make_unique<meta_store::TxnTransaction>(client->GetAID());
    transaction->If(TxnCompare::OfValue("llt/sn/workers/xxx", CompareOperator::EQUAL, "2.0"));
    DeleteOption delOption = { true, false };
    transaction->Then(TxnOperation::Create("llt/sn/workers/xxx", delOption));
    PutOption putOption = { .leaseId = 0, .prevKv = true };
    transaction->Then(TxnOperation::Create("llt/sn/workers/yyy", "2.0", putOption));
    GetOption getOption = { true, false, false, 0, SortOrder::DESCEND, SortTarget::KEY };
    transaction->Then(TxnOperation::Create("llt/sn/workers/", getOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/zzz", "2.0", putOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/yyy", delOption));
    transaction->Else(TxnOperation::Create("llt/sn/workers/", getOption));
    std::shared_ptr<TxnResponse> txnResponse = transaction->Commit().Get();
    EXPECT_FALSE(txnResponse->success);
    EXPECT_EQ(txnResponse->responses.size(), static_cast<uint32_t>(3));
    EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[0].response).prevKv.key(), "llt/sn/workers/zzz");
    EXPECT_EQ(std::get<PutResponse>(txnResponse->responses[0].response).prevKv.value(), "1.0");
    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[1].response).prevKvs.size(), static_cast<uint32_t>(1));
    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[1].response).prevKvs[0].key(), "llt/sn/workers/yyy");
    EXPECT_EQ(std::get<DeleteResponse>(txnResponse->responses[1].response).prevKvs[0].value(), "1.0");
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs.size(), static_cast<uint32_t>(2));
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].key(), "llt/sn/workers/zzz");
    EXPECT_EQ(std::get<GetResponse>(txnResponse->responses[2].response).kvs[0].value(), "2.0");
}

void WatchTest(const std::shared_ptr<KvClientStrategy> &client)
{
    auto observer = [](const std::vector<WatchEvent> &events, bool) -> bool { return true; };
    auto syncer = []() -> litebus::Future<SyncResult> { return SyncResult{ Status::OK(), 0 }; };
    WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
    auto watcher = client->Watch("llt/sn/workers", option, observer, syncer, nullptr);
    ASSERT_AWAIT_READY(watcher);
}

void GetAndWatchTest(const std::shared_ptr<KvClientStrategy> &client)
{
    litebus::Promise<bool> promise;
    ObserverFunction observer = [&](const std::vector<WatchEvent> &events, bool synced) -> bool {
        EXPECT_EQ(synced, true);
        EXPECT_EQ(events.size(), static_cast<uint32_t>(4));
        EXPECT_EQ(events[0].eventType, EVENT_TYPE_PUT);
        EXPECT_EQ(events[0].kv.key(), "llt/sn/workers/vvv");
        EXPECT_EQ(events[0].kv.value(), "2.0");
        promise.SetValue(true);
        return true;
    };
    {
        PutOption putOption = { .leaseId = 0, .prevKv = false };
        auto putResponse = client->Put("llt/sn/workers/vvv", "1.0", putOption).Get();  // await
        EXPECT_EQ(putResponse->status, Status::OK());
    }
    {
        PutOption putOption = { .leaseId = 0, .prevKv = false };
        auto putResponse = client->Put("llt/sn/workers/vvv", "2.0", putOption).Get();  // await
        EXPECT_EQ(putResponse->status, Status::OK());
    }
    auto syncer = []() -> litebus::Future<SyncResult> { return SyncResult{ Status::OK(), 0 }; };
    WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
    auto watcher = client->GetAndWatch("llt/sn/workers", option, observer, syncer, nullptr).Get();  // await
    ASSERT_AWAIT_READY(promise.GetFuture());

    watcher->Close();
    ASSERT_AWAIT_TRUE([&]() -> bool {
        return (client->readyRecords_.find(watcher->GetWatchId()) == client->readyRecords_.end());
    });  // wait for cancel success
}

TEST_F(EtcdKvClientStrategyTest, EtcdCancelTest)
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<EtcdKvClientStrategy>(ETCD_KV_CLIENT_NAME + uuid.ToString(), metaStoreServerHost_,
                                                         metaStoreTimeoutOpt);
    litebus::Spawn(client);

    GetOption opts;
    opts.prefix = true;

    litebus::Promise<bool> promise;
    ObserverFunction observer = [&](const std::vector<WatchEvent> &events, bool synced) -> bool { return true; };
    litebus::Promise<bool> promise1;
    auto syncer1 = [&]() -> litebus::Future<SyncResult> {
        promise1.SetValue(true);
        return SyncResult{ Status::OK(), 1000 + 1 };
    };

    litebus::Promise<bool> promise2;
    auto syncer2 = [&]() -> litebus::Future<SyncResult> {
        promise2.SetValue(false);
        return SyncResult{ Status(StatusCode::FAILED, "mock sync failed"), 0 };
    };

    WatchOption option = { .prefix = true, .prevKv = true, .revision = 0 };
    auto watcher1 = client->GetAndWatch("llt/sn/workers1", option, observer, syncer1, nullptr).Get();  // await
    auto watcher2 = client->GetAndWatch("llt/sn/workers2", option, observer, syncer2, nullptr).Get();  // await

    ASSERT_AWAIT_TRUE([&]() -> bool { return watcher2->GetWatchId() != -1; });  // wait for watch create successfully

    std::shared_ptr<WatchResponse> rsp = std::make_shared<WatchResponse>();
    rsp->set_watch_id(watcher1->GetWatchId());
    rsp->set_compact_revision(1000);

    auto cancelStatus = client->Cancel(rsp);
    EXPECT_EQ(cancelStatus, Status::OK());

    ASSERT_AWAIT_READY(promise1.GetFuture());
    ASSERT_TRUE(promise1.GetFuture().Get());

    rsp->set_watch_id(watcher2->GetWatchId());
    cancelStatus = client->Cancel(rsp);
    EXPECT_EQ(cancelStatus, Status::OK());

    ASSERT_AWAIT_READY(promise2.GetFuture());
    ASSERT_FALSE(promise2.GetFuture().Get());

    EXPECT_EQ(watcher1->GetWatchId(), -1);
    EXPECT_EQ(watcher2->GetWatchId(), -1);

    sleep(1);  // watch for reconnect successfully
    auto records = client->GetRecords();
    EXPECT_EQ(records[0]->option.revision, 1001);
    EXPECT_GT(records[1]->option.revision, 0);

    // test cancel event revision is samll than watcher revision, go to reconnect branch
    rsp->set_watch_id(records[0]->watcher->GetWatchId());
    cancelStatus = client->Cancel(rsp);
    EXPECT_EQ(cancelStatus, Status::OK());
    EXPECT_EQ(cancelStatus.GetMessage(), "[try to reconnect all watcher]");

    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(EtcdKvClientStrategyTest, PutTest)  // NOLINT
{
    PutTest(client_);
}

TEST_F(EtcdKvClientStrategyTest, DeleteKeyValue)  // NOLINT
{
    DeleteKeyValue(client_);
}

TEST_F(EtcdKvClientStrategyTest, DeleteKeyValuePrevKv)  // NOLINT
{
    DeleteKeyValuePrevKv(client_);
}

TEST_F(EtcdKvClientStrategyTest, DeleteKeyValuePrefix)  // NOLINT
{
    DeleteKeyValuePrefix(client_);
}

TEST_F(EtcdKvClientStrategyTest, DeleteKeyValuePrevPrefix)  // NOLINT
{
    DeleteKeyValuePrevPrefix(client_);
}

TEST_F(EtcdKvClientStrategyTest, GetTest)  // NOLINT
{
    GetTest(client_);
}

TEST_F(EtcdKvClientStrategyTest, TransactionTxn)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<meta_store::EtcdKvClientStrategy>(
        "KvClientActor_" + uuid.ToString(), metaStoreServerHost_, metaStoreTimeoutOpt, sslConfig);
    auto aid = litebus::Spawn(client);
    TransactionTxn(client);
    litebus::Terminate(aid);
    litebus::Await(aid);
}

TEST_F(EtcdKvClientStrategyTest, TransactionWithReqTxn)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<meta_store::EtcdKvClientStrategy>(
        "KvClientActor_" + uuid.ToString(), metaStoreServerHost_, metaStoreTimeoutOpt, sslConfig);
    auto aid = litebus::Spawn(client);
    TransactionWithReqTxn(client);
    litebus::Terminate(aid);
    litebus::Await(aid);
}

TEST_F(EtcdKvClientStrategyTest, TransactionTxnTest)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<meta_store::EtcdKvClientStrategy>(
        "KvClientActor_" + uuid.ToString(), metaStoreServerHost_, metaStoreTimeoutOpt, sslConfig);
    litebus::Spawn(client);
    TransactionTxnTest(client);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(EtcdKvClientStrategyTest, TransactionTxnElse)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<meta_store::EtcdKvClientStrategy>(
        "KvClientActor_" + uuid.ToString(), metaStoreServerHost_, metaStoreTimeoutOpt, sslConfig);
    litebus::Spawn(client);
    TransactionTxnElse(client);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(EtcdKvClientStrategyTest, WatchTest)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto kvClientActor = std::make_shared<EtcdKvClientStrategy>("KvClientActor_" + uuid.ToString(),
                                                                metaStoreServerHost_, metaStoreTimeoutOpt);
    bool ret = kvClientActor->ReconnectWatch();
    EXPECT_TRUE(ret);

    bool canceled = kvClientActor->TryErr();
    EXPECT_TRUE(canceled);
}

TEST_F(EtcdKvClientStrategyTest, GetAndWatchTest)  // NOLINT
{
    GetAndWatchTest(client_);
}

TEST_F(MetaStoreKvClientStrategyTest, PutTest)  // NOLINT
{
    PutTest(client_);
}

TEST_F(MetaStoreKvClientStrategyTest, DeleteKeyValue)  // NOLINT
{
    DeleteKeyValue(client_);
}

TEST_F(MetaStoreKvClientStrategyTest, DeleteKeyValuePrevKv)  // NOLINT
{
    DeleteKeyValuePrevKv(client_);
}

TEST_F(MetaStoreKvClientStrategyTest, DeleteKeyValuePrefix)  // NOLINT
{
    DeleteKeyValuePrefix(client_);
}

TEST_F(MetaStoreKvClientStrategyTest, DeleteKeyValuePrevPrefix)  // NOLINT
{
    DeleteKeyValuePrevPrefix(client_);
}

TEST_F(MetaStoreKvClientStrategyTest, GetTest)  // NOLINT
{
    GetTest(client_);
}

TEST_F(MetaStoreKvClientStrategyTest, TransactionTxn)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<meta_store::MetaStoreKvClientStrategy>("KvClientActor_" + uuid.ToString(),
                                                                          metaStoreClinetAddr_, metaStoreTimeoutOpt);
    litebus::Spawn(client);
    TransactionTxn(client);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(MetaStoreKvClientStrategyTest, TransactionWithReqTxn)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<meta_store::MetaStoreKvClientStrategy>("KvClientActor_" + uuid.ToString(),
                                                                          metaStoreClinetAddr_, metaStoreTimeoutOpt);
    litebus::Spawn(client);
    TransactionWithReqTxn(client);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(MetaStoreKvClientStrategyTest, TransactionTxnTest)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<meta_store::MetaStoreKvClientStrategy>("KvClientActor_" + uuid.ToString(),
                                                                          metaStoreClinetAddr_, metaStoreTimeoutOpt);
    litebus::Spawn(client);
    TransactionTxnTest(client);
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(MetaStoreKvClientStrategyTest, TransactionTxnElse)  // NOLINT
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto client = std::make_shared<meta_store::MetaStoreKvClientStrategy>("KvClientActor_" + uuid.ToString(),
                                                                          metaStoreClinetAddr_, metaStoreTimeoutOpt);
    litebus::Spawn(client);
    TransactionTxnElse(client);
    litebus::Async(client_->GetAID(), &meta_store::MetaStoreKvClientStrategy::OnAddressUpdated, "");
    litebus::Terminate(client->GetAID());
    litebus::Await(client);
}

TEST_F(MetaStoreKvClientStrategyTest, WatchTest)  // NOLINT
{
    WatchTest(client_);
}

TEST_F(MetaStoreKvClientStrategyTest, GetAndWatchTest)  // NOLINT
{
    GetAndWatchTest(client_);
}
}  // namespace functionsystem::meta_store::test