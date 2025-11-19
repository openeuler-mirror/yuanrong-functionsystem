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


#include "disk_scorer.h"

#include "common/logs/logging.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"

namespace functionsystem::schedule_plugin::score {
bool IsVectorAllocationValid(const schedule_framework::NodeScore &score)
{
    if (score.vectorAllocations.empty() || score.vectorAllocations[0].selectedIndices.empty()) {
        return false;
    }
    for (const auto &index : score.vectorAllocations[0].selectedIndices) {
        if (index == INVALID_INDEX) {
            return false;
        }
    }
    return true;
}
/**
 * Updates disk resource allocation in the NodeScore.
 * Example transformation (when diskIndex=2 and diskReq=100):
 *   Before: allocCategory.vectors["disk"].values = [0, 0, 0, 0]
 *   After : allocCategory.vectors["disk"].values = [0, 0, 100, 0]
 */
void UpdateDiskAllocation(double diskReq, const resource_view::Category &diskCategory,
                          schedule_framework::NodeScore &score)
{
    if (!IsVectorAllocationValid(score)) {
        YRLOG_WARN("Disk score is empty when updating disk allocation.");
        return;
    }
    auto &vectorAllocation = score.vectorAllocations[0];
    vectorAllocation.type = resource_view::DISK_RESOURCE_NAME;
    auto diskIndex = vectorAllocation.selectedIndices[0];
    auto &allocCategory = (*vectorAllocation.allocationValues.mutable_values())[vectorAllocation.type];
    for (const auto& [nodeId, vectorValue] : diskCategory.vectors()) {
        const int size = vectorValue.values_size();
        ASSERT_FS(size > diskIndex);
        for (int i = 0; i < size; i++) {
            (*allocCategory.mutable_vectors())[nodeId].add_values(diskIndex == i ? diskReq : 0);
        }
    }
}

void SetDiskMountPoint(const resource_view::Resource &diskResource, schedule_framework::NodeScore &score)
{
    if (!IsVectorAllocationValid(score)) {
        YRLOG_WARN("Disk score is empty when set disk mountPoint.");
        return;
    }
    auto &vectorAllocation = score.vectorAllocations[0];
    auto diskIndex = vectorAllocation.selectedIndices[0];

    ASSERT_FS(diskResource.extensions_size() >= diskIndex + 1);
    vectorAllocation.extendedInfo[resource_view::DISK_MOUNT_POINT] =
        diskResource.extensions(diskIndex).disk().mountpoints();
}

void CalcMaxDiskScore(double diskReq, const resource_view::Category &diskCategory, schedule_framework::NodeScore &score)
{
    float maxScore = INVALID_SCORE;
    score.vectorAllocations.clear();
    schedule_framework::VectorResourceAllocation vectorAllocation;
    vectorAllocation.selectedIndices = { INVALID_INDEX };

    for (const auto& [nodeId, vectorValue] : diskCategory.vectors()) {
        const auto &values = vectorValue.values();
        const int size = values.size();
        for (int i = 0; i < size; i++) {
            auto diskAvailable = values[i];
            if (!resource_view::IsRequestSatisfiable(diskReq, diskAvailable)) {
                continue;
            }
            float currentScore = (double(BASE_SCORE_FACTOR) - diskReq / diskAvailable) * double(DEFAULT_SCORE);
            if (currentScore > maxScore) {
                maxScore = currentScore;
                vectorAllocation.selectedIndices.clear();
                vectorAllocation.selectedIndices.push_back(i);
            }
            YRLOG_DEBUG("node {} disk index{} | disk req {}, disk avail {}, disk score {}",
                        nodeId, i, diskReq, diskAvailable, currentScore);
        }
    }
    score.score = int64_t(maxScore);
    if (fabs(maxScore - INVALID_SCORE) > EPSINON) {
        score.vectorAllocations.emplace_back(std::move(vectorAllocation));
    }
}

void CalcDiskScore(const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext,
                   const resource_view::InstanceInfo &instance,
                   const resource_view::ResourceUnit &resourceUnit,
                   schedule_framework::NodeScore &score)
{
    std::vector<float> scores;
    if (!resource_view::HasDiskResource(instance)) {
        return;
    }
    if (!resource_view::HasDiskResource(resourceUnit)) {
        YRLOG_WARN("{}|No disk resource found in resourceunit({})", instance.requestid(), resourceUnit.id());
        return;
    }

    auto resourcesAvailable = resourceUnit.allocatable();
    if (auto iter(preContext->allocated.find(resourceUnit.id())); iter != preContext->allocated.end()) {
        resourcesAvailable = resourceUnit.allocatable() - iter->second.resource;
    }

    auto diskReq = instance.resources().resources().at(resource_view::DISK_RESOURCE_NAME).scalar().value();

    auto diskResource = resourcesAvailable.resources().at(resource_view::DISK_RESOURCE_NAME);
    auto diskCategory = diskResource.vectors().values().at(resource_view::DISK_RESOURCE_NAME);
    if (diskCategory.vectors().empty()) {
        YRLOG_WARN("{}|Empty disk in resourceunit({}).", resourceUnit.id(), instance.requestid());
        return;
    }

    CalcMaxDiskScore(diskReq, diskCategory, score);
    UpdateDiskAllocation(diskReq, diskCategory, score);
    SetDiskMountPoint(diskResource, score);
}

std::string DiskScorer::GetPluginName()
{
    return DISK_SCORER_NAME;
}

schedule_framework::NodeScore DiskScorer::Score(
    const std::shared_ptr<schedule_framework::ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
    const resource_view::ResourceUnit &resourceUnit)
{
    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    schedule_framework::NodeScore nodeScore(0);
    if (preContext == nullptr) {
        YRLOG_WARN("{}|Invalid context for DiskScorer", instance.requestid());
        return nodeScore;
    }

    CalcDiskScore(preContext, instance, resourceUnit, nodeScore);
    return nodeScore;
}

std::shared_ptr<schedule_framework::ScorePlugin> DiskScorePolicyCreator()
{
    return std::make_shared<DiskScorer>();
}

REGISTER_SCHEDULER_PLUGIN(DISK_SCORER_NAME, DiskScorePolicyCreator);
}  // namespace functionsystem::schedule_plugin::score