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

#include "function_proxy/common/data_view/proxy_view/proxy_view.h"

#include <gtest/gtest.h>

namespace functionsystem::test {

class ProxyViewTest : public ::testing::Test {
public:
    void SetUp() override
    {
        proxyView_ = std::make_shared<ProxyView>();
    }

    void TearDown() override
    {
        proxyView_ = nullptr;
    }

protected:
    std::shared_ptr<ProxyView> proxyView_;
};

TEST_F(ProxyViewTest, CRUDProxy)
{
    std::string busproxyA = "proxy_A";

    auto getProxyAClient = proxyView_->Get(busproxyA);
    EXPECT_TRUE(getProxyAClient == nullptr);

    int32_t cnt = 0;
    auto updateCallbackFunc = [&cnt](std::shared_ptr<proxy::Client>) { cnt++; };
    proxyView_->SetUpdateCbFunc(busproxyA, updateCallbackFunc);
    proxyView_->SetUpdateCbFunc(busproxyA, updateCallbackFunc);
    auto proxyClientA = std::make_shared<proxy::Client>(litebus::AID(busproxyA));
    proxyView_->Update(busproxyA, proxyClientA);
    EXPECT_EQ(cnt, 2);

    getProxyAClient = proxyView_->Get(busproxyA);
    EXPECT_TRUE(getProxyAClient != nullptr);

    proxyView_->Delete(busproxyA);

    getProxyAClient = proxyView_->Get(busproxyA);
    EXPECT_TRUE(getProxyAClient == nullptr);
}

}  // namespace functionsystem::test