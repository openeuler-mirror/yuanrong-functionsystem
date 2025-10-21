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

#include <memory>

#include "metrics/api/null.h"
#include "metrics/api/provider.h"

using namespace observability::api::metrics;

namespace observability::test::api {
class MeterProviderTest : public ::testing::Test {};

TEST_F(MeterProviderTest, GetDefaultMeterProvider)
{
    auto provider = Provider::GetMeterProvider();
    EXPECT_NE(provider, nullptr);
}

TEST_F(MeterProviderTest, SetNullMeterProvider)
{
    auto mp = std::make_shared<NullMeterProvider>();
    Provider::SetMeterProvider(mp);
    auto provider = Provider::GetMeterProvider();
    EXPECT_EQ(provider, mp);
}

TEST_F(MeterProviderTest, ResetMeterProvider)
{
    std::shared_ptr<MeterProvider> mp;
    Provider::SetMeterProvider(mp);
    EXPECT_EQ(Provider::GetMeterProvider(), nullptr);
}

TEST_F(MeterProviderTest, SetMeterProviderDuplicate)
{
    auto mp1 = std::make_shared<NullMeterProvider>();
    Provider::SetMeterProvider(mp1);
    auto mp2 = std::make_shared<NullMeterProvider>();
    Provider::SetMeterProvider(mp2);
    auto provider = Provider::GetMeterProvider();
    EXPECT_EQ(provider, mp2);
}

}  // namespace observability::test::api