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
#include "affinity_utils.h"

#include "common/resource_view/resource_tool.h"
#include "common/resource_view/resource_type.h"

namespace functionsystem::schedule_plugin {
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

bool FilterRequiredWithoutPriority(const std::string &unitID, const affinity::Selector &selector,
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

bool FilterRequiredWithPriority(const std::string &unitID, const affinity::Selector &selector,
                                const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels,
                                bool anti)
{
    int64_t score = GetAffinityScore(unitID, selector, labels, anti);
    return score != 0;
}

bool RequiredAffinityFilter(const std::string &unitID, const affinity::Selector &selector,
                            const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    if (IsAffinityPriority(selector)) {
        return FilterRequiredWithPriority(unitID, selector, labels, false);
    } else {
        return FilterRequiredWithoutPriority(unitID, selector, labels, false);
    }
}

bool RequiredAntiAffinityFilter(const std::string &unitID, const affinity::Selector &selector,
                                const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    if (IsAffinityPriority(selector)) {
        return FilterRequiredWithPriority(unitID, selector, labels, true);
    } else {
        return FilterRequiredWithoutPriority(unitID, selector, labels, true);
    }
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

bool IsNodeAffinityScope(const resource_view::InstanceInfo &instance)
{
    return instance.scheduleoption().affinity().instance().scope() == affinity::NODE;
}

// Only preferred affinity, preferred anti-affinity need to be scored
bool NeedAffinityScorer(const resource_view::InstanceInfo &instance)
{
    const auto &affinity = instance.scheduleoption().affinity();

    // 1.check instance-related affinity
    if (affinity.has_instance()) {
        if (affinity.instance().has_requiredaffinity() && IsAffinityPriority(affinity.instance().requiredaffinity())) {
            return true;
        }
        if (affinity.instance().has_requiredantiaffinity() &&
            IsAffinityPriority(affinity.instance().requiredantiaffinity())) {
            return true;
        }
        if (affinity.instance().has_preferredaffinity() || affinity.instance().has_preferredantiaffinity()) {
            return true;
        }
    }

    // 2.check resource-related affinity
    if (affinity.has_resource()) {
        if (affinity.resource().has_requiredaffinity() && IsAffinityPriority(affinity.resource().requiredaffinity())) {
            return true;
        }
        if (affinity.resource().has_requiredantiaffinity() &&
            IsAffinityPriority(affinity.resource().requiredantiaffinity())) {
            return true;
        }
        if (affinity.resource().has_preferredaffinity() || affinity.resource().has_preferredantiaffinity()) {
            return true;
        }
    }

    // 3.check inner-related affinity
    if (affinity.has_inner()) {
        if (affinity.inner().has_tenant() || affinity.inner().has_data() || affinity.inner().has_preempt()
            || affinity.inner().has_grouplb()) {
            return true;
        }
    }

    return false;
}

// isTopdownScheduling: root domain --> domain --> local
bool NeedOptimalAffinityCheck(bool isRelaxed, bool isTopdownScheduling)
{
    return !isRelaxed && !isTopdownScheduling;
}

::google::protobuf::Map<std::string, resource_view::ValueCounter> GetLocalNodeLabels(
    const resource_view::ResourceUnit &resourceUnit,
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx)
{
    if (ctx->allLocalLabels.empty()) {
        return {};
    }
    if (ctx->schedulerLevel == resource_view::SCHEDULER_LEVEL::LOCAL) {
        return ctx->allLocalLabels.begin()->second;
    } else {
        auto ownerId = resourceUnit.ownerid();
        return ctx->allLocalLabels[ownerId];
    }
}

}  // namespace functionsystem::schedule_plugin