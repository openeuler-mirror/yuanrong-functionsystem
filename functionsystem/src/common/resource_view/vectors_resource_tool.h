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

#ifndef COMMON_RESOURCE_VIEW_VECTORS_RESOURCE_TOOL_H
#define COMMON_RESOURCE_VIEW_VECTORS_RESOURCE_TOOL_H

#include <set>

#include "common/constants/constants.h"
#include "common/status/status.h"
#include "common/logs/logging.h"
#include "resource_tool.h"
#include "resource_type.h"

namespace functionsystem::resource_view {
inline std::string VectorsValueToString(const Resource &resource)  // Hook
{
    ASSERT_FS(resource.type() == ValueType::Value_Type_VECTORS && resource.has_vectors());
    std::string output = "{" + resource.name() + ", vector:";
    for (auto &category : resource.vectors().values()) {
        output += "[" + category.first + ":";
        for (auto &vector : category.second.vectors()) {
            output += "(" + vector.first + ":";
            for (auto &value : vector.second.values()) {
                output += std::to_string(static_cast<int64_t>(value)) + ", ";
            }
            output += ")";
        }
        output += "]";
    }
    return output += "}";
}

inline bool VectorsValueIsEmpty(const Resource &resource)  // Hook
{
    if (!resource.has_vectors()) {
        YRLOG_ERROR("resource's vectors not exist.");
        return false;
    }

    if (resource.vectors().values().empty()) {  // map<string, Category> values = 1;
        return true;
    }

    for (const auto &category : resource.vectors().values()) {  // map<string, Category> values = 1;
        for (const auto &vector : category.second.vectors()) {  // map<string, Vector> vectors = 1;
            // CPU | Memory | HBM | Latency | Stream
            if (vector.second.values().empty()) {  // repeated double values = 1;
                YRLOG_ERROR("the {} vector is empty.", vector.first);
                return false;
            }
        }
        // The availability of the value is not verified.
    }

    return true;
}

inline bool VectorsValueValidate(const Resource &resource)  // Hook
{
    return VectorsValueIsEmpty(resource);
}

inline bool VectorsValueIsEqual(const Resource &left, const Resource &right)  // Hook
{
    // resource name is same, value type is set and set is exist.
    ASSERT_FS(left.name() == right.name() && left.type() == ValueType::Value_Type_VECTORS &&
              right.type() == ValueType::Value_Type_VECTORS && left.has_vectors() && right.has_vectors());
    if (left.vectors().values_size() != right.vectors().values_size()) {
        return false;
    }

    for (const auto &leftCategory : left.vectors().values()) {
        auto rightCategory = right.vectors().values().find(leftCategory.first);  // CPU | HBM ...
        if (rightCategory == right.vectors().values().end()) {
            return false;
        }

        if (leftCategory.second.vectors_size() != rightCategory->second.vectors_size()) {
            return false;
        }

        for (const auto &leftVector : leftCategory.second.vectors()) {
            auto rightVector = rightCategory->second.vectors().find(leftVector.first);  // uuid
            if (rightVector == rightCategory->second.vectors().end()) {
                return false;
            }

            if (leftVector.second.values_size() != rightVector->second.values_size()) {
                return false;
            }

            for (int i = 0; i < leftVector.second.values_size(); i++) {
                if (abs(leftVector.second.values(i) - rightVector->second.values(i)) >= EPSINON) {
                    return false;
                }
            }
        }
    }

    return true;
}

// example: {{ uid-0: [24, 24, 24, 24] }} + {{ uid-1: [8, 8] }} = {{ uid-0: [24, 24, 24, 24] }, { uid-1: [8, 8] }}
// example: {{ uid-0: [24, 24, 24, 24] }} + {{ uid-0: [8, 8, 8, 8] }} = {{ uid-0: [32, 32, 32, 32] }}
inline Resource VectorsValueAdd(const Resource &left, const Resource &right)  // Hook
{
    // resource name is same, value type is set and set is exist.
    ASSERT_FS(left.name() == right.name() && left.type() == ValueType::Value_Type_VECTORS &&
              right.type() == ValueType::Value_Type_VECTORS && left.has_vectors() && right.has_vectors());

    Resource result = left;
    // CPU | Memory | HBM | Latency | Stream
    for (const auto &category : right.vectors().values()) {
        auto baseCategory = result.mutable_vectors()->mutable_values()->find(category.first);
        if (baseCategory == result.mutable_vectors()->mutable_values()->end()) {
            // Scenario in which new resources are added. N -> N + 1
            (*result.mutable_vectors()->mutable_values())[category.first] = category.second;
            continue;
        }

        for (const auto &vector : category.second.vectors()) {
            auto baseVector = baseCategory->second.mutable_vectors()->find(vector.first);
            if (baseVector == baseCategory->second.mutable_vectors()->end()) {
                // Scenario in which new resources are added. N -> N + 1
                (*baseCategory->second.mutable_vectors())[vector.first] = vector.second;
                continue;
            }

            ASSERT_FS(baseVector->second.mutable_values()->size() == vector.second.values_size());
            for (int i = 0; i < vector.second.values_size(); i++) {
                (*baseVector->second.mutable_values())[i] = baseVector->second.values(i) + vector.second.values(i);
            }
        }
    }

    return result;
}

// example: {{ uid-0: [24, 24, 24, 24] }} - {{ uid-1: [8, 8] }} = {{ uid-0: [24, 24, 24, 24] }}
// example: {{ uid-0: [24, 24, 24, 24] }} - {{ uid-0: [8, 8, 8, 8] }} = {{ uid-0: [16, 16, 16, 16] }}
// example: {{ uid-0: [24, 24, 24, 24] }} - {{ uid-0: [24, 24, 24, 24] }, expired = true} = {}
inline Resource VectorsValueSub(const Resource &left, const Resource &right)  // Hook
{
    // resource name is same, value type is set and set is exist.
    ASSERT_FS(left.name() == right.name() && left.type() == ValueType::Value_Type_VECTORS &&
              right.type() == ValueType::Value_Type_VECTORS && left.has_vectors() && right.has_vectors());
    Resource result = left;
    // CPU | Memory | HBM | Latency | Stream
    for (const auto &category : right.vectors().values()) {
        auto baseCategory = result.mutable_vectors()->mutable_values()->find(category.first);
        if (baseCategory == result.mutable_vectors()->mutable_values()->end()) {
            continue;
        }

        for (const auto &vector : category.second.vectors()) {
            auto baseVector = baseCategory->second.mutable_vectors()->find(vector.first);
            if (baseVector == baseCategory->second.mutable_vectors()->end()) {
                continue;
            }

            if (right.expired()) {  // Scenario in which resource is deleted. N -> N - 1
                baseCategory->second.mutable_vectors()->erase(vector.first);
                continue;
            }

            ASSERT_FS(baseVector->second.mutable_values()->size() == vector.second.values_size());
            for (int i = 0; i < vector.second.values_size(); i++) {
                (*baseVector->second.mutable_values())[i] = baseVector->second.values(i) - vector.second.values(i);
            }
        }
    }

    return result;
}

inline bool VectorsValueLess(const Resource &left, const Resource &right)  // Hook
{
    // resource name is same, value type is set and set is exist.
    ASSERT_FS(left.name() == right.name() && left.type() == ValueType::Value_Type_VECTORS &&
              right.type() == ValueType::Value_Type_VECTORS && left.has_vectors() && right.has_vectors());
    YRLOG_WARN("Vectors resource does not support Less.");
    return false;
}

inline bool VectorsValueGreater(const Resource &left, const Resource &right)  // Hook
{
    // resource name is same, value type is set and set is exist.
    ASSERT_FS(left.name() == right.name() && left.type() == ValueType::Value_Type_VECTORS &&
              right.type() == ValueType::Value_Type_VECTORS && left.has_vectors() && right.has_vectors());
    YRLOG_WARN("Vectors resource does not support Greater.");
    return false;
}

}  // namespace functionsystem::resource_view

#endif  // COMMON_RESOURCE_VIEW_VECTORS_RESOURCE_TOOL_H
