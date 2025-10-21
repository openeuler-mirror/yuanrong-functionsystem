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

#include "common/resource_view/view_utils.h"
#include "common/scheduler_framework/utils/label_affinity_selector.h"
#include "common/scheduler_framework/utils/label_affinity_utils.h"

namespace functionsystem::test {

using namespace ::testing;
class AffinityUtilsTest : public Test {};

TEST_F(AffinityUtilsTest, IsLabelInValuesTest)
{
    ::google::protobuf::Map<std::string, resource_view::ValueCounter> labels;
    ::google::protobuf::RepeatedPtrField<std::string> values;
    EXPECT_FALSE(IsLabelInValues(labels, "key", values));

    resource_view::ValueCounter valueCounter;
    (*valueCounter.mutable_items())["value"] = 1;
    labels["key"] = valueCounter;
    EXPECT_FALSE(IsLabelInValues(labels, "key", values));

    values.Add("value");
    EXPECT_TRUE(IsLabelInValues(labels, "key", values));
}

TEST_F(AffinityUtilsTest, IsMatchLabelExpressionTest)
{
    ::google::protobuf::Map<std::string, resource_view::ValueCounter> labels;
    affinity::LabelExpression expression = In("key", { "value" });
    expression.set_key("key");
    expression.mutable_op()->mutable_in()->add_values("value");
    EXPECT_FALSE(IsMatchLabelExpression(labels, expression));

    expression = NotIn("key", { "value" });
    EXPECT_TRUE(IsMatchLabelExpression(labels, expression));

    expression = Exist("key");
    EXPECT_FALSE(IsMatchLabelExpression(labels, expression));

    expression = NotExist("key");
    EXPECT_TRUE(IsMatchLabelExpression(labels, expression));
}

TEST_F(AffinityUtilsTest, IsResourceRequiredAffinityPassedTest)
{
    ::google::protobuf::Map<std::string, resource_view::ValueCounter> labels;
    resource_view::InstanceInfo instance;
    EXPECT_TRUE(IsResourceRequiredAffinityPassed("unitID", instance, labels));

    instance.mutable_scheduleoption()->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(
        Selector(false, {}));
    EXPECT_TRUE(IsResourceRequiredAffinityPassed("unitID", instance, labels));

    instance.clear_scheduleoption();
    instance.mutable_scheduleoption()->mutable_affinity()->mutable_resource()->mutable_requiredantiaffinity()->CopyFrom(
        Selector(false, {}));
    EXPECT_FALSE(IsResourceRequiredAffinityPassed("unitID", instance, labels));
}

TEST_F(AffinityUtilsTest, CalculateInstanceAffinityScoreTest)
{
    ::google::protobuf::Map<std::string, resource_view::ValueCounter> labels;
    resource_view::InstanceInfo instance;
    EXPECT_EQ(CalculateInstanceAffinityScore("unitID", instance, labels), 0);

    instance.mutable_scheduleoption()->mutable_affinity()->mutable_instance()->mutable_preferredaffinity()->CopyFrom(
        Selector(false, {}));

    instance.mutable_scheduleoption()
        ->mutable_affinity()
        ->mutable_instance()
        ->mutable_preferredantiaffinity()
        ->CopyFrom(Selector(false, {}));
    EXPECT_EQ(CalculateInstanceAffinityScore("unitID", instance, labels), 0);

    instance.clear_scheduleoption();
    instance.mutable_scheduleoption()->mutable_affinity()->mutable_instance()->mutable_requiredantiaffinity()->CopyFrom(
        Selector(false, {}));
    EXPECT_EQ(CalculateInstanceAffinityScore("unitID", instance, labels), 0);

    instance.clear_scheduleoption();
    instance.mutable_scheduleoption()->mutable_affinity()->mutable_instance()->mutable_requiredaffinity()->CopyFrom(
        Selector(false, {}));
    EXPECT_EQ(CalculateInstanceAffinityScore("unitID", instance, labels), 0);
}

TEST_F(AffinityUtilsTest, CalculateResourceAffinityScoreTest)
{
    ::google::protobuf::Map<std::string, resource_view::ValueCounter> labels;
    resource_view::InstanceInfo instance;
    EXPECT_EQ(CalculateResourceAffinityScore("unitID", instance, labels), 0);

    instance.mutable_scheduleoption()->mutable_affinity()->mutable_resource()->mutable_preferredaffinity()->CopyFrom(
        Selector(false, {}));

    instance.mutable_scheduleoption()
        ->mutable_affinity()
        ->mutable_resource()
        ->mutable_preferredantiaffinity()
        ->CopyFrom(Selector(false, {}));
    EXPECT_EQ(CalculateResourceAffinityScore("unitID", instance, labels), 0);

    instance.clear_scheduleoption();
    instance.mutable_scheduleoption()->mutable_affinity()->mutable_resource()->mutable_requiredantiaffinity()->CopyFrom(
        Selector(false, {}));
    EXPECT_EQ(CalculateResourceAffinityScore("unitID", instance, labels), 0);

    instance.clear_scheduleoption();
    instance.mutable_scheduleoption()->mutable_affinity()->mutable_resource()->mutable_requiredaffinity()->CopyFrom(
        Selector(false, {}));
    EXPECT_EQ(CalculateResourceAffinityScore("unitID", instance, labels), 0);

    EXPECT_FALSE(IsSelectorContainsLabel(Selector(false, { { Exist("key1") }, { Exist("key2") } }), "key"));
}

}  // namespace functionsystem::test