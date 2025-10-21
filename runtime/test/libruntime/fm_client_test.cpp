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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <boost/beast/http.hpp>
#define private public
#include "src/libruntime/err_type.h"
#include "src/libruntime/fmclient/fm_client.h"

using namespace testing;
using namespace YR::Libruntime;
using namespace YR::utility;

class MockHttpClient : public HttpClient {
public:
    MockHttpClient() = default;
    ~MockHttpClient() = default;
    ErrorInfo Init(const ConnectionParam &param) override
    {
        return ErrorInfo();
    }

    void SubmitInvokeRequest(const http::verb &method, const std::string &target,
                             const std::unordered_map<std::string, std::string> &headers, const std::string &body,
                             const std::shared_ptr<std::string> requestId,
                             const HttpCallbackFunction &receiver) override
    {
        if (GLOBAL_SCHEDULER_QUERY_RESOURCES == target) {
            auto rsp = BuildQueryResponse();
            std::string rspBody;
            rsp.SerializeToString(&rspBody);
            receiver(rspBody, boost::beast::error_code(), 200);
        } else {
            std::cout << "unknown target:" << target;
        }
    }
    void RegisterHeartbeat(const std::string &jobID, int timeout) override {}

    bool Available() const override
    {
        return true;
    };

    bool IsActive() const override
    {
        return true;
    };

    bool IsConnActive() const override
    {
        return true;
    };
    ErrorInfo ReInit() override
    {
        return ErrorInfo();
    }

    QueryResourcesInfoResponse BuildQueryResponse()
    {
        QueryResourcesInfoResponse rsp;
        rsp.set_requestid("requestid");
        auto *resource = rsp.mutable_resource();
        resource->set_id("resourceid");
        resource->set_status(0);
        resources::ResourceUnit ru;
        ru.set_id("id");
        ru.set_status(0);

        resources::Resource r1;
        r1.set_type(ResourceType::Value_Type_SCALAR);
        r1.mutable_scalar()->set_value(400.00);
        ru.mutable_capacity()->mutable_resources()->insert({"CPU", r1});

        resources::Resource r2;
        r2.set_type(ResourceType::Value_Type_VECTORS);
        resources::Value_Vectors_Vector ve2;
        ve2.add_values(333.14);
        ve2.add_values(222.88);
        resources::Value_Vectors_Category c2;
        c2.mutable_vectors()->insert({"uuid", ve2});
        r2.mutable_vectors()->mutable_values()->insert({"ids", c2});
        ru.mutable_capacity()->mutable_resources()->insert({"NPU", r2});

        resource->mutable_fragment()->insert({"resource", ru});
        return rsp;
    }
};

class FmClientTest : public testing::Test {
public:
    FmClientTest(){};
    ~FmClientTest(){};
    void SetUp() override
    {
        Mkdir("/tmp/log");
        LogParam g_logParam = {
            .logLevel = "DEBUG",
            .logDir = "/tmp/log",
            .nodeName = "test-runtime",
            .modelName = "test",
            .maxSize = 100,
            .maxFiles = 1,
            .logFileWithTime = false,
            .logBufSecs = 30,
            .maxAsyncQueueSize = 1048510,
            .asyncThreadCount = 1,
            .alsoLog2Stderr = true,
        };
        InitLog(g_logParam);

        auto httpClient = std::make_shared<MockHttpClient>();
        httpClient->Init(ConnectionParam{"127.0.0.1", "8888"});
        std::shared_ptr<LibruntimeConfig> config = std::make_shared<LibruntimeConfig>();
        std::vector<std::string> functionMasters;
        functionMasters.push_back("192.168.0.1");
        functionMasters.push_back("127.0.0.1");
        config->functionMasters = functionMasters;
        fmClient_ = std::make_shared<FMClient>(config);
        fmClient_->httpClients_["127.0.0.1"] = httpClient;
    }
    void TearDown() override
    {
        fmClient_.reset();
    }
    std::shared_ptr<FMClient> fmClient_;
};

TEST_F(FmClientTest, TestGetResourcesSuccessfully)
{
    auto client = fmClient_->GetNextHttpClient();
    EXPECT_TRUE(client != nullptr);
    auto [err, res] = fmClient_->GetResources();
    EXPECT_TRUE(err.OK());
    auto unit = res.at(0);
    EXPECT_TRUE(unit.capacity["CPU"] == 400.00);
    EXPECT_TRUE(unit.capacity["NPU"] == 2.00);
    client = fmClient_->GetNextHttpClient();
    EXPECT_TRUE(client != nullptr);
}
