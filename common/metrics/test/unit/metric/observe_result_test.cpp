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

#include "sdk/include/observer_result_t.h"

namespace observability::test {

class ObserveResultTest : public ::testing::Test {

protected:
    void SetUp() override
    {
        observeResultPtr = std::make_shared<observability::metrics::ObserverResultT<double>>();
    }

    void TearDown() override
    {
        observeResultPtr = nullptr;
    }

    std::shared_ptr<observability::metrics::ObserverResultT<double>> observeResultPtr;

};

TEST_F(ObserveResultTest, SetValue)
{
    double value = 0.99;
    observeResultPtr->Observe(value);
    EXPECT_EQ(observeResultPtr->Value(), value);
}

}