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

#include "busproxy/registry/service_registry.h"
#include "busproxy/startup/busproxy_startup.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "busproxy/registry/constants.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "mocks/mock_meta_store_client.h"
#include "mocks/mock_meta_storage_accessor.h"

namespace functionsystem::test {

class ServiceRegistryTest : public ::testing::Test {
public:
    void SetUp() override
    {
        client_ = std::make_unique<MockMetaStoreClient>("ip:port");
        serviceRegistry_ = std::make_unique<ServiceRegistry>();
        metaStorageAccessor_ = std::make_shared<MockMetaStorageAccessor>(std::move(client_));
        key = "/sn/business/yrk/tenant/0/function/function-task/version/$latest/defaultaz/node01";
        proxyMeta = { "node-1", "aid-1" };
        registerInfo = { key, proxyMeta };
    }

    void TearDown() override
    {
        client_ = nullptr;
        serviceRegistry_->Stop();
        serviceRegistry_ = nullptr;
        metaStorageAccessor_ = nullptr;
    }

protected:
    std::unique_ptr<MetaStoreClient> client_;
    std::unique_ptr<ServiceRegistry> serviceRegistry_;
    std::shared_ptr<MockMetaStorageAccessor> metaStorageAccessor_;
    std::string key;
    struct ProxyMeta proxyMeta;
    struct RegisterInfo registerInfo;
};

TEST_F(ServiceRegistryTest, BusProxyRegistryTestTtlValid)
{
    std::string jsonDump = nlohmann::json{ { "aid", proxyMeta.aid }, { "node", proxyMeta.node }, { "ak", proxyMeta.ak } }.dump();
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, jsonDump, 4000))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Return(litebus::Future<Status>(Status(StatusCode::SUCCESS))));
    serviceRegistry_->Init(metaStorageAccessor_, registerInfo, 4000);
    EXPECT_EQ(serviceRegistry_->Register(), Status(StatusCode::SUCCESS));
}

TEST_F(ServiceRegistryTest, BusProxyRegistryTestTtlInvalid)
{
    std::string jsonDump = nlohmann::json{ { "aid", proxyMeta.aid }, { "node", proxyMeta.node }, { "ak", proxyMeta.ak } }.dump();
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, jsonDump, DEFAULT_TTL))
        .WillRepeatedly(::testing::Return(litebus::Future<Status>(Status(StatusCode::SUCCESS))));
    serviceRegistry_->Init(metaStorageAccessor_, registerInfo, MAX_TTL + 1);
    EXPECT_EQ(serviceRegistry_->Register(), Status(StatusCode::SUCCESS));
}

TEST_F(ServiceRegistryTest, ProxyCapabilitiesAreReplacedThroughFailClosedLeaseRotation)
{
    ::testing::InSequence sequence;
    const auto legacyDump = Dump(proxyMeta);
    ProxyServiceMeta proxyService;
    proxyService.grpcAddress = "10.0.0.11:19090";
    proxyService.tcpTunnelAddress = "10.0.0.11:22775";
    proxyService.version = "phase3";
    proxyService.health = "healthy";
    proxyService.capabilities = { "faas.create", "faas.invoke", "faas.kill", "tcp.tunnel" };
    auto readyMeta = proxyMeta;
    readyMeta.proxyService = proxyService;

    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, legacyDump, DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Revoke(key))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, Dump(readyMeta), DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Revoke(key))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, legacyDump, DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Revoke(key))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));

    serviceRegistry_->Init(metaStorageAccessor_, registerInfo);
    ASSERT_TRUE(serviceRegistry_->Register().IsOk());
    EXPECT_TRUE(serviceRegistry_->ReplaceProxyService(proxyService).IsOk());
    EXPECT_TRUE(serviceRegistry_->ReplaceProxyService({}).IsOk());
}

TEST_F(ServiceRegistryTest, ProxyCapabilitiesStayFailClosedWhenReplacementPutFails)
{
    ::testing::InSequence sequence;
    const auto legacyDump = Dump(proxyMeta);
    ProxyServiceMeta proxyService;
    proxyService.grpcAddress = "10.0.0.11:19090";
    proxyService.capabilities = { "faas.invoke" };
    auto readyMeta = proxyMeta;
    readyMeta.proxyService = proxyService;

    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, legacyDump, DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Revoke(key))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, Dump(readyMeta), DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status(StatusCode::FAILED))));
    EXPECT_CALL(*metaStorageAccessor_, Revoke(key))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));

    serviceRegistry_->Init(metaStorageAccessor_, registerInfo);
    ASSERT_TRUE(serviceRegistry_->Register().IsOk());
    EXPECT_TRUE(serviceRegistry_->ReplaceProxyService(proxyService).IsError());
}

TEST_F(ServiceRegistryTest, DelayedDeleteRestoresCurrentProxyCapabilities)
{
    const auto legacyDump = Dump(proxyMeta);
    ProxyServiceMeta proxyService;
    proxyService.grpcAddress = "10.0.0.11:19090";
    proxyService.tcpTunnelAddress = "10.0.0.11:22775";
    proxyService.capabilities = { "faas.invoke", "tcp.tunnel" };
    auto readyMeta = proxyMeta;
    readyMeta.proxyService = proxyService;
    const auto readyDump = Dump(readyMeta);

    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, legacyDump, DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Revoke(key))
        .Times(2)
        .WillRepeatedly(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, readyDump, DEFAULT_TTL))
        .Times(2)
        .WillRepeatedly(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Get(key))
        .WillOnce(::testing::Return(litebus::Option<std::string>{}));

    serviceRegistry_->Init(metaStorageAccessor_, registerInfo);
    ASSERT_TRUE(serviceRegistry_->Register().IsOk());
    ASSERT_TRUE(serviceRegistry_->ReplaceProxyService(proxyService).IsOk());
    EXPECT_TRUE(serviceRegistry_->Restore().IsOk());
}

TEST_F(ServiceRegistryTest, DelayedDeleteDoesNotRewriteCurrentRegistration)
{
    const auto legacyDump = Dump(proxyMeta);
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, legacyDump, DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Get(key))
        .WillOnce(::testing::Return(litebus::Option<std::string>{ legacyDump }));

    serviceRegistry_->Init(metaStorageAccessor_, registerInfo);
    ASSERT_TRUE(serviceRegistry_->Register().IsOk());
    EXPECT_TRUE(serviceRegistry_->Restore().IsOk());
}

TEST_F(ServiceRegistryTest, StopPreventsDeleteRecoveryFromRevivingRegistration)
{
    const auto legacyDump = Dump(proxyMeta);
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, legacyDump, DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Revoke(key))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Get(key)).Times(0);

    serviceRegistry_->Init(metaStorageAccessor_, registerInfo);
    ASSERT_TRUE(serviceRegistry_->Register().IsOk());
    ASSERT_TRUE(serviceRegistry_->Stop().Get().IsOk());
    EXPECT_TRUE(serviceRegistry_->Restore().IsOk());
}

TEST_F(ServiceRegistryTest, RestoreTimeoutDoesNotBlockStop)
{
    ::testing::InSequence sequence;
    const auto legacyDump = Dump(proxyMeta);
    litebus::Promise<Status> pendingPut;
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, legacyDump, DEFAULT_TTL))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));
    EXPECT_CALL(*metaStorageAccessor_, Get(key))
        .WillOnce(::testing::Return(litebus::Option<std::string>{}));
    EXPECT_CALL(*metaStorageAccessor_, PutWithLease(key, legacyDump, DEFAULT_TTL))
        .WillOnce(::testing::Return(pendingPut.GetFuture()));
    EXPECT_CALL(*metaStorageAccessor_, Revoke(key))
        .WillOnce(::testing::Return(litebus::Future<Status>(Status::OK())));

    serviceRegistry_->Init(metaStorageAccessor_, registerInfo);
    ASSERT_TRUE(serviceRegistry_->Register().IsOk());
    const auto started = std::chrono::steady_clock::now();
    EXPECT_TRUE(serviceRegistry_->Restore().IsError());
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(3));
    EXPECT_TRUE(serviceRegistry_->Stop().Get().IsOk());
}


TEST(ServiceRegistryDumpTest, BusProxyRegistryDumpIncludesProxyServiceWhenProvided)
{
    ProxyMeta proxyMeta{ "node-1", "aid-1" };
    proxyMeta.proxyService.grpcAddress = "10.0.0.11:19090";
    proxyMeta.proxyService.tcpTunnelAddress = "10.0.0.11:22775";
    proxyMeta.proxyService.version = "phase3";
    proxyMeta.proxyService.health = "healthy";
    proxyMeta.proxyService.capabilities = { "faas.create", "faas.invoke", "faas.kill", "tcp.tunnel" };

    auto dumped = nlohmann::json::parse(Dump(proxyMeta));

    EXPECT_EQ(dumped.at("aid"), proxyMeta.aid);
    EXPECT_EQ(dumped.at("node"), proxyMeta.node);
    ASSERT_TRUE(dumped.contains("proxyService"));
    EXPECT_EQ(dumped.at("proxyService").at("grpcAddress"), "10.0.0.11:19090");
    EXPECT_EQ(dumped.at("proxyService").at("tcpTunnelAddress"), "10.0.0.11:22775");
    EXPECT_EQ(dumped.at("proxyService").at("version"), "phase3");
    EXPECT_EQ(dumped.at("proxyService").at("health"), "healthy");
    EXPECT_THAT(dumped.at("proxyService").at("capabilities").get<std::vector<std::string>>(),
                ::testing::ElementsAre("faas.create", "faas.invoke", "faas.kill", "tcp.tunnel"));
}

TEST(ServiceRegistryDumpTest, GetServiceRegistryInfoCarriesProxyServiceWhenProvided)
{
    ProxyServiceMeta proxyService;
    proxyService.grpcAddress = "10.0.0.11:19090";
    proxyService.tcpTunnelAddress = "10.0.0.11:22775";
    proxyService.version = "phase3";
    proxyService.health = "healthy";
    proxyService.capabilities = { "faas.create", "faas.invoke", "faas.kill", "tcp.tunnel" };

    auto registerInfo = function_proxy::GetServiceRegistryInfo(
        "node-1", litebus::AID("function_proxy", "10.0.0.11:24032"), proxyService);

    EXPECT_EQ(registerInfo.key, BUSPROXY_PATH_PREFIX + "/0/node/node-1");
    EXPECT_EQ(registerInfo.meta.node, "node-1");
    EXPECT_EQ(registerInfo.meta.proxyService.grpcAddress, "10.0.0.11:19090");
    EXPECT_EQ(registerInfo.meta.proxyService.tcpTunnelAddress, "10.0.0.11:22775");
    EXPECT_THAT(registerInfo.meta.proxyService.capabilities,
                ::testing::ElementsAre("faas.create", "faas.invoke", "faas.kill", "tcp.tunnel"));

    auto dumped = nlohmann::json::parse(Dump(registerInfo.meta));
    ASSERT_TRUE(dumped.contains("proxyService"));
    EXPECT_EQ(dumped.at("proxyService").at("grpcAddress"), "10.0.0.11:19090");
}

TEST(ProxyServiceReadinessTest, GrpcRemainsHiddenUntilAllModuleAndDispatcherGatesPass)
{
    EXPECT_FALSE(BusproxyStartup::IsProxyServiceReady({}));
    EXPECT_FALSE(BusproxyStartup::IsProxyServiceReady({ true, false, false, false }));
    EXPECT_FALSE(BusproxyStartup::IsProxyServiceReady({ true, true, false, false }));
    EXPECT_FALSE(BusproxyStartup::IsProxyServiceReady({ true, true, true, false }));
    EXPECT_TRUE(BusproxyStartup::IsProxyServiceReady({ true, true, true, true }));
}

TEST(ProxyServiceReadinessTest, TunnelOnlyServiceDoesNotRequireGrpcDispatcher)
{
    EXPECT_TRUE(BusproxyStartup::IsProxyServiceReady({ true, true, true, false }, false));
    EXPECT_FALSE(BusproxyStartup::IsProxyServiceReady({ false, true, true, true }, false));
    EXPECT_FALSE(BusproxyStartup::IsProxyServiceReady({ true, false, true, true }, false));
    EXPECT_FALSE(BusproxyStartup::IsProxyServiceReady({ true, true, false, true }, false));
}

}  // namespace functionsystem::test
