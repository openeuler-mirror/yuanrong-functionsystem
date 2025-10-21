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

#include "metrics/plugin/dynamic_load.h"

#include <gtest/gtest.h>

namespace observability::test::plugin {

class LoadFactoryTest : public ::testing::Test {};

TEST_F(LoadFactoryTest, LoadPluginFail)
{
    std::string error;
    auto factory = observability::plugin::metrics::LoadFactory("invalid plugin", error);
    EXPECT_EQ(factory, nullptr);
    std::cout << "error: " << error << std::endl;
    EXPECT_FALSE(error.empty());
}

}  // namespace observability::test::plugin