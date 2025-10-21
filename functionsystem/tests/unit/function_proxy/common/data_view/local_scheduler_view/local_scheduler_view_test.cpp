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

#include "function_proxy/common/data_view/local_scheduler_view/local_scheduler_view.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

namespace functionsystem::test {

class LocalSchedulerViewTest : public ::testing::Test {
public:
    void SetUp() override
    {
        localSchedulerView_ = std::make_shared<function_proxy::LocalSchedulerView>();
    }

    void TearDown() override
    {
        localSchedulerView_ = nullptr;
    }

protected:
    std::shared_ptr<function_proxy::LocalSchedulerView> localSchedulerView_;
};

TEST_F(LocalSchedulerViewTest, CRUDLocalSchedulerAID)
{
    const std::string proxyID = "proxyID";
    const std::string aidName = "localschedulerAID";

    auto localSchedulerAID = localSchedulerView_->Get(proxyID);
    EXPECT_TRUE(localSchedulerAID == nullptr);

    localSchedulerAID = std::make_shared<litebus::AID>(aidName);
    localSchedulerView_->Update(proxyID, localSchedulerAID);

    localSchedulerAID = localSchedulerView_->Get(proxyID);
    EXPECT_TRUE(localSchedulerAID != nullptr);
    EXPECT_STREQ(localSchedulerAID->Name().c_str(), aidName.c_str());

    localSchedulerView_->Delete(proxyID);

    localSchedulerAID = localSchedulerView_->Get(proxyID);
    EXPECT_TRUE(localSchedulerAID == nullptr);
}

}  // namespace functionsystem::test