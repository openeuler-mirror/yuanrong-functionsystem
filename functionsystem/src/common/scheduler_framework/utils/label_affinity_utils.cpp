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
#include "label_affinity_utils.h"

#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"

namespace functionsystem {
using namespace schedule_framework;

// IN indicates that the value of the affinity label must be one of pod label value.
bool IsLabelInValues(const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels,
                     const std::string &key, const ::google::protobuf::RepeatedPtrField<std::string> &values)
{
    auto keyValue = labels.find(key);
    if (keyValue == labels.end()) {
        return false;
    }
    for (auto value : values) {
        if (keyValue->second.items().find(value) != keyValue->second.items().end()) {
            return true;
        }
    }
    return false;
}

bool IsLabelKeyExists(const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels,
                      const std::string &key)
{
    return labels.find(key) != labels.end();
}

bool IsMatchLabelExpression(const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels,
    const affinity::LabelExpression &expression)
{
    const auto &key = expression.key();
    const auto &op = expression.op();
    switch (op.LabelOperator_case()) {
        case affinity::LabelOperator::LabelOperatorCase::kIn: {
            return IsLabelInValues(labels, key, op.in().values());
        }
        case affinity::LabelOperator::LabelOperatorCase::kNotIn: {
            return !IsLabelInValues(labels, key, op.notin().values());
        }
        case affinity::LabelOperator::LabelOperatorCase::kExists: {
            return IsLabelKeyExists(labels, key);
        }
        case affinity::LabelOperator::LabelOperatorCase::kNotExist: {
            return !IsLabelKeyExists(labels, key);
        }
        default:
            return true;
    }
}

bool IsAffinityPriority(const affinity::Selector &selector)
{
    return selector.condition().orderpriority();
}

bool FilterRequired(const std::string &unitID, const affinity::Selector &selector,
                    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels, bool anti)
{
    bool required = true;
    for (const auto &subCondition : selector.condition().subconditions()) {
        for (const auto &require: subCondition.expressions()) {
            auto matched = IsMatchLabelExpression(labels, require);
            required = required && matched;
        }
    }
    if (anti) {
        required = !required;
    }
    return required;
}

bool RequiredFilter(const std::string &unitID, const affinity::Selector &selector,
                    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    return FilterRequired(unitID, selector, labels, false);
}

bool RequiredAntiFilter(const std::string &unitID, const affinity::Selector &selector,
                        const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    return FilterRequired(unitID, selector, labels, true);
}

bool IsResourceRequiredAffinityPassed(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                      const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    bool resourceRequiredAffinityPassed = true;
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_resource()) {
        return resourceRequiredAffinityPassed;
    }

    if (affinity.resource().has_requiredaffinity() && !IsAffinityPriority(affinity.resource().requiredaffinity())) {
        resourceRequiredAffinityPassed &= RequiredFilter(unitID, affinity.resource().requiredaffinity(), labels);
    }
    // Return early if Required affinity is not met
    if (!resourceRequiredAffinityPassed) {
        return resourceRequiredAffinityPassed;
    }

    if (affinity.resource().has_requiredantiaffinity() &&
        !IsAffinityPriority(affinity.resource().requiredantiaffinity())) {
        resourceRequiredAffinityPassed &= RequiredAntiFilter(unitID, affinity.resource().requiredantiaffinity(),
                                                             labels);
    }
    return resourceRequiredAffinityPassed;
}

int64_t GetAffinityScore(const std::string &unitID, const affinity::Selector &selector,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels, bool anti)
{
    for (const auto &subcondition : selector.condition().subconditions()) {
        bool isGroupSatified = true;
        for (const auto &expression : subcondition.expressions()) {
            auto matched = IsMatchLabelExpression(labels, expression);
            // withoutAnti is true, result is same as IsMatchLabelExpression, vice versa.
            isGroupSatified = isGroupSatified && matched;
        }
        if (anti) {
            isGroupSatified = !isGroupSatified;
        }
        // No matter whether priority-based scheduling or
        // equal-based scheduling is performed, label matching exits.
        // which is the highest score
        if (isGroupSatified) {
            // The group that returned first had a higher score than the group that returned later.
            return subcondition.weight();
        }
    }
    return 0;
}

int64_t AffinityScorer(const std::string &unitID, const affinity::Selector &selector,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    return GetAffinityScore(unitID, selector, labels, false);
}

int64_t AntiAffinityScorer(const std::string &unitID, const affinity::Selector &selector,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    return GetAffinityScore(unitID, selector, labels, true);
}

int64_t CalculateInstanceAffinityScore(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                       const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    int64_t totalScore = 0;
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_instance()) {
        return totalScore;
    }

    if (affinity.instance().has_preferredaffinity()) {
        auto score = AffinityScorer(unitID, affinity.instance().preferredaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) instance preferredaffinity score is 0", unitID);
        }
        totalScore += score;
    }

    if (affinity.instance().has_preferredantiaffinity()) {
        auto score = AntiAffinityScorer(unitID, affinity.instance().preferredantiaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) instance preferredantiaffinity score is 0", unitID);
        }
        totalScore += score;
    }

    if (affinity.instance().has_requiredaffinity() && IsAffinityPriority(affinity.instance().requiredaffinity())) {
        auto score = AffinityScorer(unitID, affinity.instance().requiredaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) instance requiredaffinity score is 0, "
                        "since it is a required affinity with configured priority, the returned score is -1", unitID);
            return REQUIRED_AFFINITY_PRIORITY_NOT_MET;
        }
        totalScore += score;
    }

    if (affinity.instance().has_requiredantiaffinity() &&
        IsAffinityPriority(affinity.instance().requiredantiaffinity())) {
        auto score = AntiAffinityScorer(unitID, affinity.instance().requiredantiaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) instance requiredantiaffinity score is 0, "
                        "since it is a required affinity with configured priority, the returned score is -1", unitID);
            return REQUIRED_AFFINITY_PRIORITY_NOT_MET;
        }
        totalScore += score;
    }

    YRLOG_DEBUG("resourceUnit({}), instance preferred result score {}", unitID, totalScore);
    return totalScore;
}

int64_t CalculateResourceAffinityScore(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                       const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    int64_t totalScore = 0;
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_resource()) {
        return totalScore;
    }

    if (affinity.resource().has_preferredaffinity()) {
        auto score = AffinityScorer(unitID, affinity.resource().preferredaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) resource preferredaffinity score is 0", unitID);
        }
        totalScore += score;
    }

    if (affinity.resource().has_preferredantiaffinity()) {
        auto score = AntiAffinityScorer(unitID, affinity.resource().preferredantiaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) resource preferredantiaffinity score is 0", unitID);
        }
        totalScore += score;
    }

    if (affinity.resource().has_requiredaffinity() && IsAffinityPriority(affinity.resource().requiredaffinity())) {
        auto score = AffinityScorer(unitID, affinity.resource().requiredaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) resource requiredaffinity score is 0, "
                        "since it is a required affinity with configured priority, the returned score is -1", unitID);
            return REQUIRED_AFFINITY_PRIORITY_NOT_MET;
        }
        totalScore += score;
    }

    if (affinity.resource().has_requiredantiaffinity() &&
        IsAffinityPriority(affinity.resource().requiredantiaffinity())) {
        auto score = AntiAffinityScorer(unitID, affinity.resource().requiredantiaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) resource requiredantiaffinity score is 0, "
                        "since it is a required affinity with configured priority, the returned score is -1", unitID);
            return REQUIRED_AFFINITY_PRIORITY_NOT_MET;
        }
        totalScore += score;
    }

    YRLOG_DEBUG("resourceUnit({}), resource preferred score {}", unitID, totalScore);
    return totalScore;
}
}  // namespace functionsystem