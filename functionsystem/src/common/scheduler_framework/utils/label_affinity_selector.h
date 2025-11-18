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

#ifndef COMMON_SCHEDULER_FRAMEWORK_UTILS_LABEL_AFFINITY_SELECTOR_H
#define COMMON_SCHEDULER_FRAMEWORK_UTILS_LABEL_AFFINITY_SELECTOR_H

#include <string>
#include <vector>
#include "common/logs/logging.h"
#include "common/proto/pb/posix_pb.h"

namespace functionsystem {
const int MAX_PRIORITY_SCORE = 100;
const int PRIORITY_DECREMENT = 10;

inline affinity::LabelExpression In(const std::string &key, const std::vector<std::string> &values)
{
    auto expression = affinity::LabelExpression();
    expression.set_key(key);
    for (auto value : values) {
        expression.mutable_op()->mutable_in()->add_values(value);
    }
    return expression;
}

inline affinity::LabelExpression NotIn(const std::string &key, const std::vector<std::string> &values)
{
    auto expression = affinity::LabelExpression();
    expression.set_key(key);
    for (auto value : values) {
        expression.mutable_op()->mutable_notin()->add_values(value);
    }
    return expression;
}

inline affinity::LabelExpression Exist(const std::string &key)
{
    auto expression = affinity::LabelExpression();
    expression.set_key(key);
    expression.mutable_op()->mutable_exists();
    return expression;
}

inline affinity::LabelExpression NotExist(const std::string &key)
{
    auto expression = affinity::LabelExpression();
    expression.set_key(key);
    expression.mutable_op()->mutable_notexist();
    return expression;
}

inline affinity::Selector Selector(bool isOrderPriority,
                                   const std::vector<std::vector<affinity::LabelExpression>> &labels)
{
    auto selector = affinity::Selector();
    selector.mutable_condition()->set_orderpriority(isOrderPriority);
    for (auto i = 0; i < static_cast<int>(labels.size()); i++) {
        auto label = labels[i];
        auto expressGroup = selector.mutable_condition()->add_subconditions();
        for (auto express : label) {
            *expressGroup->add_expressions() = express;
            YRLOG_DEBUG("group add express: key {}, op {}", express.key(), express.op().ShortDebugString());
        }
        expressGroup->set_weight(isOrderPriority ? MAX_PRIORITY_SCORE - (PRIORITY_DECREMENT * i) : MAX_PRIORITY_SCORE);
        YRLOG_DEBUG("group set_weight {}",
                    isOrderPriority ? MAX_PRIORITY_SCORE - (PRIORITY_DECREMENT * i) : MAX_PRIORITY_SCORE);
    }
    return selector;
}

inline bool IsSelectorContainsLabel(const affinity::Selector &selector, const std::string &key)
{
    for (auto subcondition : selector.condition().subconditions()) {
        for (auto expression : subcondition.expressions()) {
            if (expression.key() == key) {
                YRLOG_WARN("selector contains label key({}) selector({})", key, selector.ShortDebugString());
                return true;
            }
        }
    }
    return false;
}

inline void EraseLabelFromLabels(::google::protobuf::RepeatedPtrField<std::string> &labels, const std::string &key)
{
    for (auto label = labels.begin(); label != labels.end();) {
        if (*label == key) {
            label = labels.erase(label);
        } else {
            ++label;
        }
    }
}

inline void EraseLabelFromSelector(affinity::Selector &selector, const std::string &key)
{
    auto &subConditions = *selector.mutable_condition()->mutable_subconditions();
    bool isPriority = selector.condition().orderpriority();
    int index = 0;
    for (auto subCondition = subConditions.begin(); subCondition != subConditions.end();) {
        auto expressions = subCondition->mutable_expressions();
        for (auto it = expressions->begin(); it != expressions->end();) {
            if (it->key() == key) {
                it = expressions->erase(it);
                YRLOG_WARN("erase key({}) from PreferredSelector", key);
            } else {
                ++it;
            }
        }
        if (expressions->size() == 0) {
            subCondition = subConditions.erase(subCondition);
            continue;
        }
        subCondition->set_weight(isPriority ? MAX_PRIORITY_SCORE - index * PRIORITY_DECREMENT : MAX_PRIORITY_SCORE);
        index++;
        ++subCondition;
    }
}

}  // namespace functionsystem

#endif  // COMMON_SCHEDULER_FRAMEWORK_UTILS_LABEL_AFFINITY_SELECTOR_H
