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

#include "common/schedule_plugin/filter/resource_selector_filter/resource_selector_filter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../common/plugin_utils.h"
#include "common/resource_view/view_utils.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test::schedule_plugin::filter {
using namespace ::testing;
using namespace schedule_framework;
class ResourceSelectorFilterTest : public Test {};

/**
 * Description: Test ResourceSelectorFilter
 * 1. resourceSelector is not enabled  --> SUCCESS
 * 2. key is not match                 --> RESOURCE_NOT_ENOUGH
 * 3. key is match, value is not match --> RESOURCE_NOT_ENOUGH
 * 4. key and value match              --> SUCCESS
 */
TEST_F(ResourceSelectorFilterTest, ResourceSelectorFilter)
{
    resource_view::ResourceUnit unit;
    resource_view::InstanceInfo ins = GetInstance("instance1", "monopoly", 512, 500);
    functionsystem::schedule_plugin::filter::ResourceSelectorFilter filter;
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    unit = GetAgentResourceUnit(500, 512, 1);
    ins = GetInstance("instance1", "shared", 512, 500);

    // mock frag label and values
    auto valueC1 = ::resources::Value::Counter();
    (*valueC1.mutable_items())["value1"] = 1;
    auto valueC2 = ::resources::Value::Counter();
    (*valueC2.mutable_items())["value2"] = 1;
    (*unit.mutable_nodelabels())["label1"] = valueC1;
    (*unit.mutable_nodelabels())["label2"] = valueC2;

    // resourceSelector is not enabled  --> SUCCESS
    {
        auto res = filter.Filter(preAllocated, ins, unit);
        EXPECT_EQ(res.status.StatusCode(), StatusCode::SUCCESS);
    }

    // key is not match --> RESOURCE_NOT_ENOUGH
    {
        (*ins.mutable_scheduleoption()->mutable_resourceselector())["label3"] = "value3";
        auto res = filter.Filter(preAllocated, ins, unit);
        EXPECT_EQ(res.status.StatusCode(), StatusCode::RESOURCE_NOT_ENOUGH);
        EXPECT_STREQ(res.status.GetMessage().c_str(), "[Resource Require Label Not Found]");
    }

    // key is match, value is not match --> RESOURCE_NOT_ENOUGH
    {
        ins = GetInstance("instance1", "shared", 512, 500);
        (*ins.mutable_scheduleoption()->mutable_resourceselector())["label2"] = "value3";
        auto res = filter.Filter(preAllocated, ins, unit);
        EXPECT_EQ(res.status.StatusCode(), StatusCode::RESOURCE_NOT_ENOUGH);
        EXPECT_STREQ(res.status.GetMessage().c_str(), "[Resource Require Value Not Found]");
    }

    // key and value match --> SUCCESS
    {
        ins = GetInstance("instance1", "shared", 512, 500);
        (*ins.mutable_scheduleoption()->mutable_resourceselector())["label1"] = "value1";
        (*ins.mutable_scheduleoption()->mutable_resourceselector())["label2"] = "value2";
        auto res = filter.Filter(preAllocated, ins, unit);
        EXPECT_EQ(res.status.StatusCode(), StatusCode::SUCCESS);
    }

    // key is resource owner && unit does not have it --> SUCCESS
    {
        (*ins.mutable_scheduleoption()->mutable_resourceselector())[RESOURCE_OWNER_KEY] = DEFAULT_OWNER_VALUE;
        auto res = filter.Filter(preAllocated, ins, unit);
        EXPECT_EQ(res.status.StatusCode(), StatusCode::SUCCESS);
    }
}

}  // namespace functionsystem::test::schedule_plugin::filter