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

#include "common/schedule_plugin/scorer/default_heterogeneous_scorer/default_heterogeneous_scorer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"
#include "common/resource_view/resource_tool.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::schedule_plugin::score;
using namespace functionsystem::schedule_framework;

void AddPreAllocated(const resource_view::InstanceInfo &ins,
                     const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                     const std::string &selected,
                     schedule_framework::NodeScore &score)
{
    auto backupIns = ins;
    const auto &required = ins.resources().resources();
    for (auto &req : required) {
        auto resourceNameFields = litebus::strings::Split(req.first, "/");
        if (resourceNameFields.size() == HETERO_RESOURCE_FIELD_NUM) {
            backupIns.mutable_resources()->mutable_resources()->erase(req.first);
        }
    }
    auto *resources = backupIns.mutable_resources()->mutable_resources();
    for (const auto &allocated : score.allocatedVectors) {
        auto *vectors = (*resources)[allocated.first].mutable_vectors();
        (*resources)[allocated.first].set_name(allocated.first);
        (*resources)[allocated.first].set_type(resource_view::ValueType::Value_Type_VECTORS);
        for (const auto &value : allocated.second.values()) {
            (*vectors->mutable_values())[value.first] = value.second;
        }
    }
    (*backupIns.mutable_schedulerchain()->Add()) = selected;
    backupIns.set_unitid(selected);
    context->allocated[selected].resource = context->allocated[selected].resource.resources().size() == 0
                                                ? backupIns.resources()
                                                : context->allocated[selected].resource + backupIns.resources();
    context->allocatedLabels[selected] = context->allocatedLabels[selected] + ToLabelKVs(ins.labels());
    context->preAllocatedSelectedFunctionAgentMap[ins.instanceid()] = selected;
    context->preAllocatedSelectedFunctionAgentSet.insert(selected);
}

class DefaultHeterogeneousScorerTest : public Test {};

// Score heterogeneous(hbm+latency+stream) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringHBMAndLatencyAndStreamInHeteroPod) {
    auto unit = view_utils::Get1DResourceUnitWithSpecificNpuNumber({15,20,40,0,20,30,0,0}, "NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    // 1.Non-regex
    auto instance = view_utils::Get1DInstanceWithNpuResource(30, 20, 1, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 33); // int(30/30*100 + 0 + 0) / 3;
    EXPECT_EQ(score.realIDs[0], 5);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    // 2.regex
    instance = view_utils::Get1DInstanceWithNpuResource(30, 20, 1, "NPU/Ascend910.*");
    preAllocated = std::make_shared<PreAllocatedContext>();
    score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 33); // int(30/30*100 + 0 + 0) / 3;
    EXPECT_EQ(score.realIDs[0], 5);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringCountInHeteroPod) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    // 1.Non-regex
    auto instance = view_utils::Get1DInstanceWithNpuResource(6, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    // 2.regex
    instance = view_utils::Get1DInstanceWithNpuResource(6, "NPU/Ascend910.*");
    preAllocated = std::make_shared<PreAllocatedContext>();
    score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");
}

TEST(DefaultHeterogeneousScorerTest, SelectsPhysicalIdsFromGpuCountVector)
{
    auto unit = view_utils::Get1DResourceUnitWithGpuCount({ 1, 0, 1, 1 });
    auto instance = view_utils::Get1DInstanceWithNpuResource(2, "GPU/A10");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    DefaultHeterogeneousScorer scorer;

    auto score = scorer.Score(preAllocated, instance, unit);

    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.heteroProductName, "GPU/A10");
    EXPECT_EQ(score.realIDs, (std::vector<int>{ 0, 2 }));
    ASSERT_NE(score.allocatedVectors.find("GPU/A10"), score.allocatedVectors.end());
    const auto &vectors = score.allocatedVectors.at("GPU/A10")
                              .values()
                              .at(resource_view::HETEROGENEOUS_CARDNUM_KEY)
                              .vectors();
    ASSERT_EQ(vectors.size(), size_t{1});
    const auto &values = vectors.begin()->second.values();
    EXPECT_EQ(std::vector<double>(values.begin(), values.end()), (std::vector<double>{ 1, 0, 1, 0 }));
}

TEST(DefaultHeterogeneousScorerTest, DoesNotReusePreallocatedGpuCountVector)
{
    auto unit = view_utils::Get1DResourceUnitWithGpuCount({ 1, 0, 1, 1 });
    auto first = view_utils::Get1DInstanceWithNpuResource(2, "GPU/A10");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    DefaultHeterogeneousScorer scorer;

    auto firstScore = scorer.Score(preAllocated, first, unit);
    AddPreAllocated(first, preAllocated, unit.id(), firstScore);

    auto second = view_utils::Get1DInstanceWithNpuResource(1, "GPU/A10");
    auto secondScore = scorer.Score(preAllocated, second, unit);
    EXPECT_EQ(secondScore.realIDs, (std::vector<int>{ 3 }));
}

TEST(DefaultHeterogeneousScorerTest, SelectsGpuCountVectorByRegex)
{
    auto unit = view_utils::Get1DResourceUnitWithGpuCount({ 0, 1 }, "GPU/H100-80GB-HBM3");
    auto instance = view_utils::Get1DInstanceWithNpuResource(1, "GPU/.+");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    DefaultHeterogeneousScorer scorer;

    auto score = scorer.Score(preAllocated, instance, unit);

    EXPECT_EQ(score.heteroProductName, "GPU/H100-80GB-HBM3");
    EXPECT_EQ(score.realIDs, (std::vector<int>{ 1 }));
}

TEST(DefaultHeterogeneousScorerTest, PrefersHbmWhenCountVectorAlsoExists)
{
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    const auto &hbmVectors = unit.capacity()
                                 .resources()
                                 .at("NPU/Ascend910B")
                                 .vectors()
                                 .values()
                                 .at(resource_view::HETEROGENEOUS_MEM_KEY)
                                 .vectors();
    const auto &uuid = hbmVectors.begin()->first;
    auto addCountVector = [&uuid](resources::Resource *resource) {
        auto &count = (*resource->mutable_vectors()->mutable_values())
                          [resource_view::HETEROGENEOUS_CARDNUM_KEY];
        auto &values = (*count.mutable_vectors())[uuid];
        for (int i = 0; i < 8; ++i) {
            values.mutable_values()->Add(1);
        }
    };
    addCountVector(&unit.mutable_capacity()->mutable_resources()->at("NPU/Ascend910B"));
    addCountVector(&unit.mutable_allocatable()->mutable_resources()->at("NPU/Ascend910B"));
    auto instance = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    DefaultHeterogeneousScorer scorer;

    auto score = scorer.Score(preAllocated, instance, unit);

    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs, (std::vector<int>{ 0 }));
    ASSERT_NE(score.allocatedVectors.find("NPU/Ascend910B"), score.allocatedVectors.end());
    const auto &allocated = score.allocatedVectors.at("NPU/Ascend910B").values();
    EXPECT_NE(allocated.find(resource_view::HETEROGENEOUS_MEM_KEY), allocated.end());
    EXPECT_EQ(allocated.find(resource_view::HETEROGENEOUS_CARDNUM_KEY), allocated.end());
}

// Score non-heterogeneous requests for pod without heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestNonHeteroScoringInNonHeteroPod) {
    auto instance = view_utils::Get1DInstance();
    auto unit = view_utils::Get1DResourceUnit();

    DefaultHeterogeneousScorer scorer;
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
}

// Score non-heterogeneous requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestNonHeteroScoringInHeteroPod) {
    auto instance = view_utils::Get1DInstance();
    auto unit = view_utils::Get1DResourceUnitWithNpu();

    DefaultHeterogeneousScorer scorer;
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 0);
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase1) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 0);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 1);
}


// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase2) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.7, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 1);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 0);
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase3) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.7, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 1);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(0.6, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 2);
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase4) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.4, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 0);

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 0);
}

// Score heterogeneous(count) requests for pod with heterogeneous resources
TEST(DefaultHeterogeneousScorerTest, TestHeteroScoringFracCountCase5) {
    auto unit = view_utils::Get1DResourceUnitWithNpu("NPU/Ascend910B");
    DefaultHeterogeneousScorer scorer;

    auto instance = view_utils::Get1DInstanceWithNpuResource(0.3, "NPU/Ascend910B");
    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto score = scorer.Score(preAllocated, instance, unit);
    EXPECT_EQ(score.score, 100);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.heteroProductName, "NPU/Ascend910B");

    AddPreAllocated(instance, preAllocated, unit.id(), score);
    auto instance2 = view_utils::Get1DInstanceWithNpuResource(0.8, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance2, unit);
    EXPECT_EQ(score.realIDs[0], 1);
    EXPECT_EQ(score.realIDs.size(), size_t{1});

    AddPreAllocated(instance2, preAllocated, unit.id(), score);
    auto instance3 = view_utils::Get1DInstanceWithNpuResource(5, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance3, unit);
    EXPECT_EQ(score.realIDs[0], 2);
    EXPECT_EQ(score.realIDs.size(), size_t{5});

    AddPreAllocated(instance3, preAllocated, unit.id(), score);
    auto instance4 = view_utils::Get1DInstanceWithNpuResource(0.5, "NPU/Ascend910B");
    score = scorer.Score(preAllocated, instance4, unit);
    EXPECT_EQ(score.realIDs[0], 0);
    EXPECT_EQ(score.realIDs.size(), size_t{1});
}

}
