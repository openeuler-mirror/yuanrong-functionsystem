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
    bool operator<(const NodeScore& a) const
    {
        return score < a.score;
    }

    NodeScore& operator+=(const NodeScore& a)
    {
        this->score += a.score;
        // Currently, only heterogeneous plugins return the following information, which is aggregated in overwrite
        // mode
        if (!(a.realIDs.empty())) {
            this->realIDs = a.realIDs;
        }
        for (const auto &vector : a.allocatedVectors) {
            this->allocatedVectors[vector.first] = vector.second;
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
