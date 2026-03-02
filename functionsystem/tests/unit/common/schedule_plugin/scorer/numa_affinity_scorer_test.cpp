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

#include "common/schedule_plugin/scorer/numa_affinity_scorer/numa_affinity_scorer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/resource_type.h"
#include "common/resource_view/view_utils.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::schedule_plugin::score;
using namespace functionsystem::schedule_framework;

const std::string DEFAULT_NUMA_NODE_ID = "test_numa_node";

class NUMAAffinityScorerTest : public Test {};

// Invalid context (nullptr) -> returns empty allocation
TEST_F(NUMAAffinityScorerTest, InvalidContext) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto score = scorer.Score(nullptr, instance, unit);
    EXPECT_EQ(score.score, 0);
    EXPECT_TRUE(score.vectorAllocations.empty());
}

// No bind_resource in extension -> returns empty allocation
TEST_F(NUMAAffinityScorerTest, NoBindResource) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 0);
    EXPECT_TRUE(score.vectorAllocations.empty());
}

// bind_resource != "NUMA" -> returns empty allocation
TEST_F(NUMAAffinityScorerTest, BindResourceNotNUMA) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "GPU";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 0);
    EXPECT_TRUE(score.vectorAllocations.empty());
}

// CPU requirement <= 0 -> returns empty allocation
TEST_F(NUMAAffinityScorerTest, ZeroCpuRequirement) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(0);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 0);
    EXPECT_TRUE(score.vectorAllocations.empty());
}

// bind_resource=NUMA, valid CPU, valid NUMA resources -> vectorAllocations populated
TEST_F(NUMAAffinityScorerTest, ScoreWithNUMAAllocation) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_strategy"] = "BIND_Spread";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);

    EXPECT_EQ(score.score, 0);
    EXPECT_EQ(score.name, DEFAULT_NUMA_NODE_ID);
    EXPECT_EQ(score.vectorAllocations.size(), 1);
    EXPECT_EQ(score.vectorAllocations[0].type, resource_view::NUMA_RESOURCE_NAME);
    EXPECT_EQ(score.vectorAllocations[0].selectedIndices.size(), 1);
    EXPECT_EQ(score.vectorAllocations[0].selectedIndices[0], 0);
}

// BIND_Pack: multiple nodes selected when one node not enough
TEST_F(NUMAAffinityScorerTest, BIND_PackSelectsSingleNode) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_strategy"] = "BIND_Pack";

    // Node0: 800, Node1: 400 - Pack should pick node with more (node0)
    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 800.0, 400.0 }, 1200.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);

    EXPECT_EQ(score.vectorAllocations.size(), 1);
    EXPECT_EQ(score.vectorAllocations[0].selectedIndices.size(), 2);
}

// BIND_Spread: nodes spread when multiple needed
TEST_F(NUMAAffinityScorerTest, BIND_SpreadSelectsMultipleNodes) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(1500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_strategy"] = "BIND_Spread";

    // Node0: 800, Node1: 800 - Spread needs both for 1500
    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 800.0, 800.0 }, 1600.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);

    EXPECT_EQ(score.vectorAllocations.size(), 1);
    EXPECT_EQ(score.vectorAllocations[0].selectedIndices.size(), 2);
    EXPECT_THAT(score.vectorAllocations[0].selectedIndices, UnorderedElementsAre(0, 1));
}

// Pre-allocated scenario: effectiveAllocatable = allocatable - allocated
TEST_F(NUMAAffinityScorerTest, PreAllocatedContext) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_strategy"] = "BIND_Spread";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources allocated;
    (*allocated.mutable_resources())[resource_view::CPU_RESOURCE_NAME] =
        view_utils::GetNameResourceWithValue(view_utils::RESOURCE_CPU_NAME, 500);
    (*allocated.mutable_resources())[resource_view::MEMORY_RESOURCE_NAME] =
        view_utils::GetNameResourceWithValue(view_utils::RESOURCE_MEM_NAME, 512);
    (*allocated.mutable_resources())[resource_view::NUMA_RESOURCE_NAME] =
        view_utils::GetNUMAResource({ 500.0, 0.0 }, DEFAULT_NUMA_NODE_ID);
    preAllocated->allocated[unit.id()].resource = std::move(allocated);

    auto score = scorer.Score(preAllocated, instance, unit);

    EXPECT_EQ(score.vectorAllocations.size(), 1);
    EXPECT_EQ(score.vectorAllocations[0].selectedIndices.size(), 1);
    // effectiveAllocatable NUMA: [500, 1000], BIND_Spread picks node1 (max) first for 500
    EXPECT_EQ(score.vectorAllocations[0].selectedIndices[0], 1);
}

// BIND_None defaults to BIND_Spread
TEST_F(NUMAAffinityScorerTest, BIND_NoneDefaultsToSpread) {
    NUMAAffinityScorer scorer;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_strategy"] = "BIND_None";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);

    EXPECT_EQ(score.vectorAllocations.size(), 1);
    EXPECT_EQ(score.vectorAllocations[0].selectedIndices.size(), 1);
}

// GetPluginName
TEST_F(NUMAAffinityScorerTest, GetPluginName) {
    NUMAAffinityScorer scorer;
    EXPECT_EQ(scorer.GetPluginName(), "NUMAAffinityScorer");
}

}  // namespace functionsystem::test
