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
#include "common/schedule_plugin/scorer/default_scorer/default_scorer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../common/plugin_utils.h"
#include "common/resource_view/view_utils.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test::schedule_plugin::scorer {
using namespace ::testing;
using namespace schedule_framework;
class DefaultScorerTest : public Test {};

/**
 * Description: Test DefaultScorer
 * 1. return correct score
 */
TEST_F(DefaultScorerTest, DefaultScorer)
{
    resource_view::ResourceUnit unit;
    resource_view::InstanceInfo ins = GetInstance("instance1", "monopoly", 512, 500);
    functionsystem::schedule_plugin::scorer::DefaultScorer scorer;
    auto preAllocated = std::make_shared<PreAllocatedContext>();

    // not preContext
    {
        unit = GetAgentResourceUnit(1000, 1024, 1);
        ins = GetInstance("instance1", "shared", 512, 500);
        (*ins.mutable_resources()->mutable_resources())["ZeroResource"] =
            view_utils::GetNameResourceWithValue("ZeroResource", 0);  // test for required number is zero
        auto npuKey = resource_view::NPU_RESOURCE_NAME + "/" + "910" + "/" + resource_view::HETEROGENEOUS_MEM_KEY;
        (*ins.mutable_resources()->mutable_resources())[npuKey] =
            view_utils::GetNpuResource();  // test for HETERO_RESOURCE
        auto score = scorer.Score(preAllocated, ins, unit);
        EXPECT_EQ(50, score.score);
    }

    // with preContext
    {
        unit = GetAgentResourceUnit(1000, 1024, 1);
        ins = GetInstance("instance1", "monopoly", 512, 500);
        (*ins.mutable_resources()->mutable_resources())["ZeroResource"] =
            view_utils::GetNameResourceWithValue("ZeroResource", 0);  // test for required number is zero
        (*ins.mutable_resources()->mutable_resources())["CustomResource"] =
            view_utils::GetNameResourceWithValue("CustomResource", 1);  // test for CustomResource is 1
        auto npuKey = resource_view::NPU_RESOURCE_NAME + "/" + "910" + "/" + resource_view::HETEROGENEOUS_MEM_KEY;
        (*ins.mutable_resources()->mutable_resources())[npuKey] = view_utils::GetNpuResource();  // test for HETERO_RESOURCE

        resource_view::Resources rs = view_utils::GetCpuMemResources();
        rs.mutable_resources()->at(resource_view::CPU_RESOURCE_NAME).mutable_scalar()->set_value(100);
        rs.mutable_resources()->at(resource_view::MEMORY_RESOURCE_NAME).mutable_scalar()->set_value(100);
        preAllocated->allocated[unit.id()].resource = std::move(rs);

        auto score = scorer.Score(preAllocated, ins, unit);
        int64_t cpuScore = (1.0f - 500.0 / 900.0) * 100;
        int64_t memScore = (1.0f - 512.0 / 924.0) * 100;
        int64_t expectScore = (memScore + cpuScore) / 2;
        EXPECT_EQ(expectScore, score.score);
    }
}

}  // namespace functionsystem::test::schedule_plugin::scorer