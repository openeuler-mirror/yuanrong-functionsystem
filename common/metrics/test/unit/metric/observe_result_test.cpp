/*
* Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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