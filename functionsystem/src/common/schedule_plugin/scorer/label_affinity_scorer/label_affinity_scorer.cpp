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

#include "label_affinity_scorer.h"

#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/schedule_plugin/common/affinity_utils.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"

namespace functionsystem::schedule_plugin::score {
std::string LabelAffinityScorer::GetPluginName()
{
    if (isRelaxed_) {
        return RELAXED_LABEL_AFFINITY_SCORER_NAME;
    } else {
        return STRICT_LABEL_AFFINITY_SCORER_NAME;
    }
    return LABEL_AFFINITY_SCORER_NAME;
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
            YRLOG_DEBUG("resourceUnit({}) instance requiredaffinity score is 0", unitID);
        }
        totalScore += score;
    }

    if (affinity.instance().has_requiredantiaffinity() &&
        IsAffinityPriority(affinity.instance().requiredantiaffinity())) {
        auto score = AntiAffinityScorer(unitID, affinity.instance().requiredantiaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) instance requiredantiaffinity score is 0", unitID);
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
            YRLOG_DEBUG("resourceUnit({}) resource requiredaffinity score is 0", unitID);
        }
        totalScore += score * RESOURCE_AFFINITY_WEIGHT;
    }

    if (affinity.resource().has_requiredantiaffinity()
        && IsAffinityPriority(affinity.resource().requiredantiaffinity())) {
        auto score = AntiAffinityScorer(unitID, affinity.resource().requiredantiaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) resource requiredantiaffinity score is 0", unitID);
        }
        totalScore += score * RESOURCE_AFFINITY_WEIGHT;
    }

    YRLOG_DEBUG("resourceUnit({}), resource preferred score {}", unitID, totalScore);
    return totalScore;
}

int64_t CalculatePreemptAffinityScore(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                      const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    int64_t totalScore = 0;
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_inner() || !affinity.inner().has_preempt()) {
        return 0;
    }

    if (affinity.inner().preempt().has_preferredaffinity()) {
        auto score = AffinityScorer(unitID, affinity.inner().preempt().preferredaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) inner preempt preferredaffinity score is 0", unitID);
        }
        totalScore += score;
    }

    if (affinity.inner().preempt().has_preferredantiaffinity()) {
        auto score = AntiAffinityScorer(unitID, affinity.inner().preempt().preferredantiaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) inner preempt preferredantiaffinity score is 0", unitID);
        }
        totalScore += score;
    }

    return totalScore;
}

int64_t CalculateDataAffinityScore(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                   const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    int64_t score = 0;
    const auto &affinity = instance.scheduleoption().affinity();
    if (affinity.has_inner() && affinity.inner().has_data() && affinity.inner().data().has_preferredaffinity()) {
        score = AffinityScorer(unitID, affinity.inner().data().preferredaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) inner data preferredaffinity score is 0", unitID);
        }
    }
    return score;
}

int64_t CalculateTenantAffinityScore(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                     const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    int64_t score = 0;
    const auto &affinity = instance.scheduleoption().affinity();
    if (affinity.has_inner() && affinity.inner().has_tenant() && affinity.inner().tenant().has_preferredaffinity()) {
        score = AffinityScorer(unitID, affinity.inner().tenant().preferredaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) inner tenant preferredaffinity score is 0", unitID);
        }
    }
    return score;
}

int64_t CalculateGroupScheduleAffinityScore(
    const std::string &unitID, const resource_view::InstanceInfo &instance,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    int64_t totalScore = 0;
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_inner() || !affinity.inner().has_grouplb()) {
        return 0;
    }

    if (affinity.inner().grouplb().has_preferredaffinity()) {
        auto score = AffinityScorer(unitID, affinity.inner().grouplb().preferredaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) inner grouplb preferredaffinity score is 0", unitID);
        }
        return score;
    }

    if (affinity.inner().grouplb().has_preferredantiaffinity()) {
        auto score = AntiAffinityScorer(unitID, affinity.inner().grouplb().preferredantiaffinity(), labels);
        if (score == ZERO_SCORE) {
            YRLOG_DEBUG("resourceUnit({}) inner grouplb preferredantiaffinity score is 0", unitID);
        }
        return score;
    }

    return totalScore;
}

int64_t CalculateInnerAffinityScore(const resource_view::ResourceUnit &resourceUnit,
                                    const resource_view::InstanceInfo &instance,
                                    const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext)
{
    int64_t totalScore = 0;
    auto unitId = resourceUnit.id();
    auto ownerId = resourceUnit.ownerid();

    // 1.calculate inner preempt affinity score
    auto localNodeLabels = GetLocalNodeLabels(resourceUnit, preContext);
    totalScore += CalculatePreemptAffinityScore(ownerId, instance, localNodeLabels);

    // 2.calculate inner data affinity score
    totalScore += CalculateDataAffinityScore(unitId, instance, resourceUnit.nodelabels());

    // 3.calculate inner Tenant affinity score
    // merge scheduled instance labels
    auto unitChildAgentLabels = resourceUnit.nodelabels() + preContext->allocatedLabels[unitId];
    totalScore += CalculateTenantAffinityScore(unitId, instance, unitChildAgentLabels);

    // 3.calculate inner Group Schedule affinity score
    totalScore += CalculateGroupScheduleAffinityScore(unitId, instance, unitChildAgentLabels);

    return totalScore;
}

int64_t LabelAffinityScorer::CalculatePreferredScore(
    const resource_view::ResourceUnit &resourceUnit,
    const resource_view::InstanceInfo &instance,
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext) const
{
    int64_t totalScore = 0;
    auto ownerId = resourceUnit.ownerid();
    auto unitId = resourceUnit.id();

    // 1.calculate instance-related affinity score
    if (IsNodeAffinityScope(instance)) {
        auto localNodeLabels = GetLocalNodeLabels(resourceUnit, preContext);
        totalScore += CalculateInstanceAffinityScore(unitId, instance, localNodeLabels);
    } else {
        // merge scheduled instance labels
        auto unitChildAgentLabels = resourceUnit.nodelabels() + preContext->allocatedLabels[unitId];
        totalScore += CalculateInstanceAffinityScore(unitId, instance, unitChildAgentLabels);
    }

    // 2.calculate resource-related affinity score
    totalScore += CalculateResourceAffinityScore(unitId, instance, resourceUnit.nodelabels());

    // 3.calculate inner-related affinity score
    totalScore += CalculateInnerAffinityScore(resourceUnit, instance, preContext);
    return totalScore;
}

schedule_framework::NodeScore LabelAffinityScorer::Score(
    const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
    const resource_view::InstanceInfo &instance,
    const resource_view::ResourceUnit &resourceUnit)
{
    schedule_framework::NodeScore nodeScore(0);

    if (!NeedAffinityScorer(instance)) {
        nodeScore.score = 1;
        return nodeScore;
    }

    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    if (preContext == nullptr || preContext->pluginCtx == nullptr || preContext->allLocalLabels.empty()) {
        YRLOG_WARN("{}|invalid context for LabelAffinityScorer", instance.requestid());
        return nodeScore;
    }
    int64_t score = 0;
    auto unitId = resourceUnit.id();
    auto &pluginCtx = *preContext->pluginCtx;
    auto &affinityCtx = *pluginCtx[LABEL_AFFINITY_PLUGIN].mutable_affinityctx();
    if (affinityCtx.scheduledscore().find(unitId) != affinityCtx.scheduledscore().end()) {
        score = affinityCtx.scheduledscore().at(unitId);
        YRLOG_DEBUG("{}|resourceUnit({}) from affinityCtx get score: {}", instance.requestid(), unitId, score);
        nodeScore.score = score;
        return nodeScore;
    }
    if (NeedOptimalAffinityCheck(isRelaxed_, affinityCtx.istopdownscheduling())) {
        score = affinityCtx.maxscore();
        YRLOG_DEBUG("{}|resourceUnit({}) has already met the optimal preferred score : {}",
                    instance.requestid(), unitId, score);
        nodeScore.score = score;
        (*affinityCtx.mutable_scheduledscore())[resourceUnit.id()] = score;
        return nodeScore;
    }
    score = CalculatePreferredScore(resourceUnit, instance, preContext);
    (*affinityCtx.mutable_scheduledscore())[resourceUnit.id()] = score;
    nodeScore.score = score;
    return nodeScore;
}

std::shared_ptr<schedule_framework::ScorePlugin> RelaxedLabelAffinityScorePolicyCreator()
{
    bool isRelaxed = true;
    return std::make_shared<LabelAffinityScorer>(isRelaxed);
}

std::shared_ptr<schedule_framework::ScorePlugin> StrictLabelAffinityScorePolicyCreator()
{
    bool isRelaxed = false;
    return std::make_shared<LabelAffinityScorer>(isRelaxed);
}

REGISTER_SCHEDULER_PLUGIN(RELAXED_LABEL_AFFINITY_SCORER_NAME, RelaxedLabelAffinityScorePolicyCreator);
REGISTER_SCHEDULER_PLUGIN(STRICT_LABEL_AFFINITY_SCORER_NAME, StrictLabelAffinityScorePolicyCreator);
}  // namespace functionsystem::schedule_plugin::score