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

#include "default_heterogeneous_scorer.h"

#include <bits/std_abs.h>

#include <algorithm>
#include <cstdlib>
#include <map>

#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/scala_resource_tool.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"
#include "common/utils/struct_transfer.h"
#include "utils/string_utils.hpp"

namespace functionsystem::schedule_plugin::score {

const double NUM_THRESHOLD = 1 - EPSINON;

std::string DefaultHeterogeneousScorer::GetPluginName()
{
    return DEFAULT_HETEROGENEOUS_SCORER_NAME;
}

void OnCalcHeterogeneousCardNumScore(const std::string cardType, const resources::Resources &available,
                                     const resources::Resources &capacity, double reqVal,
                                     schedule_framework::NodeScore &score)
{
    const auto &avaVectors =
        available.resources().at(cardType).vectors().values().at(resource_view::HETEROGENEOUS_MEM_KEY).vectors();
    const auto &capVectors =
        capacity.resources().at(cardType).vectors().values().at(resource_view::HETEROGENEOUS_MEM_KEY).vectors();

    // require resource < 1, only place on a hetero device
    // require resource >= 1, will place on cnt * reqVal(=1) device to satisfy require resource
    int cnt;
    if (reqVal < NUM_THRESHOLD) {
        cnt = 1;
    } else {
        cnt = reqVal > INT_MAX ? INT_MAX : static_cast<int>(reqVal);
        reqVal = 1;
    }

    auto &vectors = score.allocatedVectors[cardType];
    auto &cg = (*vectors.mutable_values())[resource_view::HETEROGENEOUS_MEM_KEY];

    for (auto &[uuid, availVec] : avaVectors) {
        if (capVectors.count(uuid) == 0 || capVectors.at(uuid).values().size() != availVec.values_size()) {
            YRLOG_DEBUG("can not find capacity or size is not equal to avail for : {}", uuid);
            continue;
        }
        const auto &capValues = capVectors.at(uuid).values();
        for (int i = 0; i < availVec.values_size(); i++) {
            int req = capValues.at(i) * reqVal;
            // rg resource cap maybe 0 because it only requires part of device, we can not schedule to this device
            // avail >= cap * req, use this device and occupy req resource
            if (cnt > 0 && capValues.at(i) > EPSINON
                && (availVec.values().at(i) > req || abs(availVec.values().at(i) - req) < EPSINON)) {
                (*cg.mutable_vectors())[uuid].add_values(req);
                score.realIDs.push_back(i);  // realIDs here is in ascend order
                cnt--;
                continue;
            }
            // avail < cap * req, set to 0, mean we don't use this device;
            (*cg.mutable_vectors())[uuid].add_values(0);
        }
        if (cnt == 0) {
            break;
        }
        cg.Clear();
        score.realIDs.clear();
    }
    score.heteroProductName = cardType;
    score.score = DEFAULT_SCORE;
}

void CalcHeterogeneousCardNumScore(const std::string cardType, const resources::Resources &available,
                                   const resources::Resources &capacity, double reqVal,
                                   schedule_framework::NodeScore &score)
{
    if (!HasHeteroResourceInResources(available, cardType, resource_view::HETEROGENEOUS_MEM_KEY)
        && !HasHeteroResourceInResources(capacity, cardType, resource_view::HETEROGENEOUS_MEM_KEY)) {
        YRLOG_WARN("HBM: Not Found.");
        return;
    }

    OnCalcHeterogeneousCardNumScore(cardType, available, capacity, reqVal, score);
}

std::vector<float> CalcHeterogeneousHbmScore(const std::string cardType,
                                             const resources::Resources &available,
                                             int reqVal)
{
    std::vector<float> hbmScores;
    auto resourceType = resource_view::HETEROGENEOUS_MEM_KEY;
    if (!HasHeteroResourceInResources(available, cardType, resourceType)) {
        YRLOG_WARN("{}: Not Found.", resourceType);
        return hbmScores;
    }
    auto category =
            available.resources().at(cardType).vectors().values().at(resource_view::HETEROGENEOUS_MEM_KEY);
    if (category.vectors().empty()) {
        YRLOG_WARN("The hbm in the resource view is empty.");
        return hbmScores;
    }
    for (auto &pair : category.vectors()) {
        for (int i = 0; i < pair.second.values_size(); i++) {
            if (reqVal > static_cast<int>(pair.second.values().at(i))) {
                hbmScores.push_back(INVALID_SCORE);
                continue;
            }
            float score = (float(reqVal) / float(pair.second.values().at(i))) * float(DEFAULT_SCORE);
            hbmScores.push_back(score);
            YRLOG_DEBUG("node {} device{}|Hbm req {}, Hbm avail {}, Hbm score {}", pair.first, i, float(reqVal),
                        float(pair.second.values().at(i)), score);
        }
    }
    return hbmScores;
}

std::vector<float> CalcHeterogeneousLatencyScore(const resources::Resources &available, const std::string cardType)
{
    std::vector<float> latencyScores;
    auto resourceType = resource_view::HETEROGENEOUS_LATENCY_KEY;
    if (!HasHeteroResourceInResources(available, cardType, resourceType)) {
        YRLOG_WARN("{}: Not Found.", resourceType);
        return latencyScores;
    }
    auto category =
        available.resources().at(cardType).vectors().values().at(resourceType);
    if (category.vectors().empty()) {
        YRLOG_WARN("The latency in the resource view is empty.");
        return latencyScores;
    }
    google::protobuf::RepeatedField<double> repeatedField;
    for (auto &pair: category.vectors()) {
        repeatedField.Add(pair.second.values().begin(), pair.second.values().end());
    }
    auto curMaxLatency = -*std::min_element(repeatedField.begin(), repeatedField.end());
    int idx = 0;
    for (auto &pair: category.vectors()) {
        for (auto &cardResource: pair.second.values()) {
            auto curAvail = -cardResource;
            float score = (curAvail / float(curMaxLatency + MIN_SCORE_THRESHOLD)) * float(DEFAULT_SCORE);
            latencyScores.push_back(score);
            YRLOG_DEBUG("node {} device{}|Latency max {}, device cur latency {}, score is {}",
                        pair.first, idx++, curMaxLatency, curAvail, score);
        }
    }
    return latencyScores;
}

std::vector<float> CalcHeterogeneousStreamScore(const resources::Resources &available,
                                                const std::string cardType, int reqVal)
{
    std::vector<float> streamScores;
    auto resourceType = resource_view::HETEROGENEOUS_STREAM_KEY;
    if (!HasHeteroResourceInResources(available, cardType, resourceType)) {
        YRLOG_WARN("{}: Not Found.", resourceType);
        return streamScores;
    }
    auto category = available.resources().at(cardType).vectors().values().at(resource_view::HETEROGENEOUS_STREAM_KEY);
    if (category.vectors().empty()) {
        YRLOG_WARN("The stream in the resource view is empty.");
        return streamScores;
    }

    google::protobuf::RepeatedField<double> repeatedField;
    for (auto &pair : category.vectors()) {
        repeatedField.Add(pair.second.values().begin(), pair.second.values().end());
    }
    auto curMaxAvailStream = *std::max_element(repeatedField.begin(), repeatedField.end());
    if (abs(curMaxAvailStream) <= 1e-15) {
        return streamScores;
    }
    int idx = 0;
    for (auto &pair : category.vectors()) {
        for (auto &cardResource : pair.second.values()) {
            if (reqVal > static_cast<int>(cardResource)) {
                streamScores.push_back(INVALID_SCORE);
                idx++;
                continue;
            }
            float score = (BASE_SCORE_FACTOR - (float(cardResource) / float(curMaxAvailStream))) * float(DEFAULT_SCORE);
            streamScores.push_back(score);
            YRLOG_DEBUG("node {} device{}|stream avail {}, max stream avail {}, stream req {}, score is {}", pair.first,
                        idx++, cardResource, curMaxAvailStream, reqVal, score);
        }
    }
    return streamScores;
}

void Padding(const resource_view::InstanceInfo &instance, const resources::Resources &available,
             int deviceID, schedule_framework::NodeScore &score)
{
    for (auto &req : instance.resources().resources()) {
        auto cardTypeRegex = GetHeteroCardTypeFromResName(req.first);
        if (cardTypeRegex.empty()) {
            continue;
        }

        auto cardType = GetResourceCardTypeByRegex(available, cardTypeRegex);
        if (cardType.empty()) {
            YRLOG_WARN("{}|no available card type for regex({}).", instance.requestid(), cardTypeRegex);
            continue;
        }

        auto resourceType = litebus::strings::Split(req.first, "/")[RESOURCE_IDX];
        if (!available.resources().contains(cardType)) {
            continue;
        }
        auto& category = available.resources().at(cardType).vectors().values();
        if (!category.contains(resourceType)) {
            continue;
        }

        auto &vectors = score.allocatedVectors[cardType];
        int tmp = deviceID;
        for (auto &pair : category.at(resourceType).vectors()) {
            if (pair.second.values().size() - 1 < tmp) { // in domain, find the internal ID for the selected deviceID
                tmp -= pair.second.values().size();
                auto &cg = (*vectors.mutable_values())[resourceType];
                for (int i = 0; i < pair.second.values().size(); i++) {
                    (*cg.mutable_vectors())[pair.first].add_values(0);
                }
            } else {
                // in local, it should directly go into this branch
                auto &cg = (*vectors.mutable_values())[resourceType];
                for (int i = 0; i < pair.second.values().size(); i++) {
                    (*cg.mutable_vectors())[pair.first].add_values(tmp == i ? req.second.scalar().value() : 0);
                }
            }
        }
    }
}

float CalculateFinalScore(float hbmScore, float latencyScore, float streamScore)
{
    if (hbmScore < 0 || latencyScore < 0 || streamScore < 0) {
        return INVALID_SCORE;
    }
    return (hbmScore + latencyScore + streamScore) / HETEROGENEOUS_RESOURCE_REQUIRED_COUNT;
}

void CalcHeterogeneousScore(const resource_view::InstanceInfo &instance, const resources::Resources &available,
                            const resource_view::ResourceUnit &resourceUnit, schedule_framework::NodeScore &score)
{
    std::vector<float> hbmScores;
    std::vector<float> latencyScores;
    std::vector<float> streamScores;
    std::string cardType = "";

    for (auto &req : instance.resources().resources()) {
        auto cardTypeRegex = GetHeteroCardTypeFromResName(req.first);
        if (cardTypeRegex.empty()) {
            continue;
        }

        cardType = GetResourceCardTypeByRegex(available, cardTypeRegex);
        if (cardType.empty()) {
            YRLOG_WARN("{}|no available card type for regex({}).", instance.requestid(), cardTypeRegex);
            continue;
        }

        auto resourceType = litebus::strings::Split(req.first, "/")[RESOURCE_IDX];
        if (resourceType == resource_view::HETEROGENEOUS_MEM_KEY) {
            hbmScores =
                CalcHeterogeneousHbmScore(cardType, available, static_cast<int>(req.second.scalar().value()));
        } else if (resourceType == resource_view::HETEROGENEOUS_LATENCY_KEY) {
            latencyScores = CalcHeterogeneousLatencyScore(available, cardType);
        } else if (resourceType == resource_view::HETEROGENEOUS_STREAM_KEY) {
            streamScores =
                CalcHeterogeneousStreamScore(available, cardType, static_cast<int>(req.second.scalar().value()));
        } else if (resourceType == resource_view::HETEROGENEOUS_CARDNUM_KEY) {
            CalcHeterogeneousCardNumScore(cardType, available, resourceUnit.capacity(), req.second.scalar().value(),
                                          score);
            return;
        } else {
            YRLOG_WARN("Unknown hetero resource: {}. Only support HBM, Latency, Stream and CardNum now.", resourceType);
        }
    }

    if (hbmScores.size() != latencyScores.size() || hbmScores.size() != streamScores.size()) {
        YRLOG_ERROR("Not all three are configured: HBM, latency, and stream");
        return;
    }

    float maxScore = INVALID_SCORE;
    score.realIDs = {0};
    for (size_t i = 0; i < hbmScores.size(); i++) {
        float finalScore = CalculateFinalScore(hbmScores[i], latencyScores[i], streamScores[i]);
        if (finalScore > maxScore) {
            maxScore = finalScore;
            score.realIDs[0] = static_cast<int>(i);
            YRLOG_INFO("switch to deviceID {} with maxScore {}", i, finalScore);
        }
    }
    score.score = int64_t(maxScore);
    score.heteroProductName = cardType;
    Padding(instance, available, score.realIDs[0], score);
    YRLOG_INFO("{}|allocate cardType-{} deviceID-{} in {} with heterogeneous score {}. hbm {}, latency {}, stream {}. ",
               instance.requestid(), cardType, score.realIDs[0], resourceUnit.id(), maxScore,
               hbmScores[score.realIDs[0]], latencyScores[score.realIDs[0]], streamScores[score.realIDs[0]]);
}


bool HasHeterogeneousResources(const resources::Resources& resources)
{
    for (const auto& [resourceName, _] : resources.resources()) {
        [[maybe_unused]] const auto& unused = _;
        if (resourceName != resource_view::CPU_RESOURCE_NAME && resourceName != resource_view::MEMORY_RESOURCE_NAME) {
            return true;
        }
    }
    return false;
}

schedule_framework::NodeScore DefaultHeterogeneousScorer::Score(
    const std::shared_ptr<schedule_framework::ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
    const resource_view::ResourceUnit &resourceUnit)
{
    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    schedule_framework::NodeScore nodeScore(0);
    if (preContext == nullptr) {
        YRLOG_WARN("invalid context for DefaultHeterogeneousScorer");
        return nodeScore;
    }
    auto available = resourceUnit.allocatable();
    if (auto iter(preContext->allocated.find(resourceUnit.id())); iter != preContext->allocated.end()) {
        available = resourceUnit.allocatable() - iter->second.resource;
    }

    bool hasHeteroReq = resource_view::HasHeterogeneousResource(instance);
    if (hasHeteroReq) {
        CalcHeterogeneousScore(instance, available, resourceUnit, nodeScore);
        return nodeScore;
    }

    bool isUnitWithHeteroRes = HasHeterogeneousResources(available);
    if (!isUnitWithHeteroRes) {
        nodeScore.score = DEFAULT_SCORE;
    }
    return nodeScore;
}
std::shared_ptr<schedule_framework::ScorePlugin> DefaultHeterogeneousScorePolicyCreator()
{
    return std::make_shared<DefaultHeterogeneousScorer>();
}

REGISTER_SCHEDULER_PLUGIN(DEFAULT_HETEROGENEOUS_SCORER_NAME, DefaultHeterogeneousScorePolicyCreator);
}  // namespace functionsystem::schedule_plugin::score