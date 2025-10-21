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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

}  // namespace functionsystem::test