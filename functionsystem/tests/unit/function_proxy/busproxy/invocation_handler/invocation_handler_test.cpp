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

#include "busproxy/invocation_handler/invocation_handler.h"

#include <gtest/gtest.h>

#include "common/constants/actor_name.h"
#include "common/etcd_service/etcd_service_driver.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "meta_store_client/meta_store_client.h"
#ifdef OBSERVABILITY
#include "common/trace/trace_actor.h"
#endif
#include "function_proxy/common/observer/observer_actor.h"
#include "mocks/mock_data_observer.h"
#include "mocks/mock_instance_proxy_wrapper.h"
#include "mocks/mock_shared_client.h"
#include "utils/future_test_helper.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

using ::testing::_;
using ::testing::Return;

class InvocationHandlerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        etcdSrvDriver_ = std::make_unique<meta_store::test::EtcdServiceDriver>();
        int metaStoreServerPort = functionsystem::test::FindAvailablePort();
        std::string metaStoreServerHost = "127.0.0.1:" + std::to_string(metaStoreServerPort);
        etcdSrvDriver_->StartServer(metaStoreServerHost);
        auto metaClient = MetaStoreClient::Create({ .etcdAddress = metaStoreServerHost });
        auto metaStorageAccessor = std::make_shared<MetaStorageAccessor>(metaClient);
        auto address = litebus::GetLitebusAddress();
        expectedAid_.SetUrl(address.ip + ":" + std::to_string(address.port));
        InvocationHandler::BindUrl(address.ip + ":" + std::to_string(address.port));
#ifdef OBSERVABILITY
        traceActor_ = std::make_shared<TraceActor>(address.ip + ":" + std::to_string(address.port) + "/v1/traces",
                                                   "InvocationHandlerTest");
        litebus::Spawn(traceActor_);
#endif
        instanceProxy_ = std::make_shared<MockInstanceProxy>();
        InvocationHandler::BindInstanceProxy(instanceProxy_);
        MemoryControlConfig config;
        memoryMonitor_ = std::make_shared<MemoryMonitor>(config);
        InvocationHandler::BindMemoryMonitor(memoryMonitor_);
    }

    void TearDown() override
    {
#ifdef OBSERVABILITY
        litebus::Terminate(traceActor_->GetAID());
        litebus::Await(traceActor_);
#endif
        InvocationHandler::UnBindInstanceProxy();
        instanceProxy_ = nullptr;
        memoryMonitor_ = nullptr;
        mockObserver_ = nullptr;
        etcdSrvDriver_->StopServer();
        etcdSrvDriver_ = nullptr;
    }

protected:
    std::shared_ptr<function_proxy::ObserverActor> observerActor_;
#ifdef OBSERVABILITY
    std::shared_ptr<TraceActor> traceActor_;
#endif
    std::shared_ptr<MockInstanceProxy> instanceProxy_;
    litebus::AID expectedAid_;
    std::shared_ptr<MemoryMonitor> memoryMonitor_;
    std::shared_ptr<MockDataObserver> mockObserver_;
    std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
};

TEST_F(InvocationHandlerTest, Invoke)
{
    std::unique_ptr<runtime_rpc::StreamingMessage> request = std::make_unique<runtime_rpc::StreamingMessage>();
    request->mutable_invokereq()->set_instanceid("to");
    std::shared_ptr<runtime_rpc::StreamingMessage> response = std::make_shared<runtime_rpc::StreamingMessage>();
    response->mutable_callrsp()->set_code(common::ErrorCode::ERR_NONE);
    expectedAid_.SetName("from");
    EXPECT_CALL(*instanceProxy_, Call(expectedAid_, _, _, _, _)).WillOnce(Return(response));
    auto responseFuture = InvocationHandler::Invoke("from", std::move(request));
    ASSERT_AWAIT_READY(responseFuture);
    EXPECT_EQ(responseFuture.Get()->invokersp().code(), common::ErrorCode::ERR_NONE);
}

litebus::Future<std::pair<bool, std::shared_ptr<runtime_rpc::StreamingMessage>>> AdapterTrue(
    const std::string &, std::shared_ptr<functionsystem::CallResult> &)
{
    std::shared_ptr<runtime_rpc::StreamingMessage> response = std::make_shared<runtime_rpc::StreamingMessage>();
    response->mutable_callresultack()->set_code(common::ErrorCode::ERR_NONE);
    return std::make_pair<bool, std::shared_ptr<runtime_rpc::StreamingMessage>>(true, std::move(response));
}

litebus::Future<std::pair<bool, std::shared_ptr<runtime_rpc::StreamingMessage>>> AdapterFalse(
    const std::string &, std::shared_ptr<functionsystem::CallResult> &)
{
    std::shared_ptr<runtime_rpc::StreamingMessage> response = std::make_shared<runtime_rpc::StreamingMessage>();
    response->mutable_callresultack()->set_code(static_cast<common::ErrorCode>(StatusCode::LS_REQUEST_NOT_FOUND));
    return std::make_pair<bool, std::shared_ptr<runtime_rpc::StreamingMessage>>(false, std::move(response));
}

TEST_F(InvocationHandlerTest, CallResultAdapter)
{
    std::shared_ptr<runtime_rpc::StreamingMessage> response = std::make_shared<runtime_rpc::StreamingMessage>();
    response->mutable_callresultack()->set_code(common::ErrorCode::ERR_NONE);
    // with @initcall return true
    InvocationHandler::RegisterCreateCallResultReceiver(AdapterTrue);
    std::unique_ptr<runtime_rpc::StreamingMessage> request = std::make_unique<runtime_rpc::StreamingMessage>();
    request->mutable_callresultreq()->set_instanceid("LocalInstanceActor");
    request->mutable_callresultreq()->set_requestid("request@initcall");
    auto responseFuture = InvocationHandler::CallResultAdapter("from", std::move(request));
    ASSERT_AWAIT_READY(responseFuture);
    EXPECT_EQ(responseFuture.Get()->callresultack().code(), common::ERR_NONE);

    // without @initcall
    expectedAid_.SetName("from");
    std::unique_ptr<runtime_rpc::StreamingMessage> request1 = std::make_unique<runtime_rpc::StreamingMessage>();
    request1->mutable_callresultreq()->set_instanceid("LocalInstanceActor");
    request1->mutable_callresultreq()->set_requestid("request");
    EXPECT_CALL(*instanceProxy_, CallResult(expectedAid_, "from", "LocalInstanceActor", _, _)).WillOnce(Return(response));
    responseFuture = InvocationHandler::CallResultAdapter("from", std::move(request1));
    ASSERT_AWAIT_READY(responseFuture);
    EXPECT_EQ(responseFuture.Get()->callresultack().code(), common::ERR_NONE);

    // with @initcall return false
    InvocationHandler::RegisterCreateCallResultReceiver(AdapterFalse);
    std::unique_ptr<runtime_rpc::StreamingMessage> request2 = std::make_unique<runtime_rpc::StreamingMessage>();
    request2->mutable_callresultreq()->set_instanceid("LocalInstanceActor");
    request2->mutable_callresultreq()->set_requestid("request@initcall");
    responseFuture = InvocationHandler::CallResultAdapter("from", std::move(request2));
    ASSERT_AWAIT_READY(responseFuture);
    EXPECT_EQ(responseFuture.Get()->callresultack().code(), common::ERR_INNER_COMMUNICATION);
}

}  // namespace functionsystem::test
