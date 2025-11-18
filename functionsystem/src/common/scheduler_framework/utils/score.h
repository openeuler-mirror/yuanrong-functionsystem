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

#ifndef COMMON_SCHEDULER_FRAMEWORK_UTILS_SCORE_H
#define COMMON_SCHEDULER_FRAMEWORK_UTILS_SCORE_H

#include <sstream>
#include <string>
#include <map>
#include <utility>
#include <vector>

namespace functionsystem::schedule_framework {
/**
 * Represents vector-based resource allocation result
 * type             - Type of resource (e.g. disk, cardType)
 * selectedIndices  - Device indices that were actually allocated(e.g. [4, 5])
 * allocationValues - Corresponding allocation values(Resource's name: Value.Vectors)(e.g. [0, 0, 100, 100]), where:
 *                    ==0 means not allocated
 *                    >0 indicates allocated amount
 * extendedInfo     - Additional resource info (e.g. heteroProductName, diskMountPoint)
 */
struct VectorResourceAllocation {
    std::string type;
    std::vector<int> selectedIndices;
    ::resources::Value_Vectors allocationValues;
    std::map<std::string, std::string> extendedInfo;
};

struct NodeScore {
    explicit NodeScore(int64_t score) : score(score) {}
    NodeScore(const std::string &name, int64_t score) : name(name), score(score) {}
    NodeScore(int64_t score, std::vector<int> realIDs) : realIDs(std::move(realIDs)), score(score) {}
    NodeScore(const std::string &name, int64_t score, std::vector<int> realIDs)
        : name(name), realIDs(std::move(realIDs)), score(score) {}

    std::string name;
    std::string heteroProductName;
    std::vector<int> realIDs = {};
    int64_t score;
    // Indicates the number of requests that can be scheduled in the current pod or unit.
    // -1 means no limited, etc: while label affinity matched, no matter how many instances can be scheduled if the
    // resource is allowed.
    // is assigned by framework. if returned by scored plugin, the value would be ignored.
    int32_t availableForRequest = 0;

    // Resource's name: Value.Vectors
    std::map<std::string, ::resources::Value_Vectors> allocatedVectors;
    // Vector-based resource allocation results
    std::vector<VectorResourceAllocation> vectorAllocations;

    bool operator<(const NodeScore& a) const
    {
        return score < a.score;
    }

    NodeScore& operator+=(const NodeScore& a)
    {
        this->score += a.score;
        // Currently, only heterogeneous plugins return the following information, which is aggregated in overwrite
        // mode --> will be merged into append mode below.
        if (!a.heteroProductName.empty()) {
            this->heteroProductName = a.heteroProductName;
        }
        if (!(a.realIDs.empty())) {
            this->realIDs = a.realIDs;
        }
        for (const auto &vector : a.allocatedVectors) {
            this->allocatedVectors[vector.first] = vector.second;
        }
        // Currently, only disk plugins return the vector allocation information, which is moved in append mode.
        if (!a.vectorAllocations.empty()) {
            this->vectorAllocations.insert(this->vectorAllocations.end(),
                                           std::make_move_iterator(a.vectorAllocations.begin()),
                                           std::make_move_iterator(a.vectorAllocations.end()));
        }
        // availableForRequest is assign separately
        return *this;
    }
};

using NodeScoreList = std::vector<NodeScore>;

struct PluginScore {
    PluginScore(const std::string &name, int64_t score) : name(name), score(score)
    {
    }

    std::string name;
    int64_t score;
};

struct NodePluginScores {
    explicit NodePluginScores(const std::string &id) : nodeName(id), totalScore(0)
    {
    }

    void AddPluginScore(const PluginScore &pluginScore)
    {
        scores.push_back(pluginScore);
    }

    std::string nodeName;
    std::vector<PluginScore> scores;
    int64_t totalScore;
};

static inline std::string DebugNodeScores(std::vector<NodeScore> &nodeScores)
{
    const size_t maxDisplay = 5;
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < nodeScores.size() && i < maxDisplay; ++i) {
        auto score = nodeScores[i];
        oss << "{id:" << score.name << " score:" << score.score << "}";
    }
    oss << "]";
    return oss.str();
}

}  // namespace functionsystem::schedule_framework

#endif  // COMMON_SCHEDULER_FRAMEWORK_UTILS_SCORE_H
