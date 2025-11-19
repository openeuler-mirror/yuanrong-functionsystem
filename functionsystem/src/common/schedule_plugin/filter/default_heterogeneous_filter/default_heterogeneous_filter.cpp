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

#include "default_heterogeneous_filter.h"

#include <algorithm>
#include <map>

#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/scala_resource_tool.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"
#include "common/utils/struct_transfer.h"
#include "utils/string_utils.hpp"

namespace functionsystem::schedule_plugin::filter {

const double NUM_THRESHOLD = 1 - EPSINON;
const double MIN_NUM_THRESHOLD = 0.0001;
const double REQUIRE_FACTOR = 1;

std::string FindCardNumKey(const resource_view::InstanceInfo &instance)
{
    for (const auto &req : instance.resources().resources()) {
        auto resourceNameFields = litebus::strings::Split(req.first, "/");
        // heterogeneous resource name is like: NPU/310/memory or GPU/cuda/count...
        if (resourceNameFields.size() != HETERO_RESOURCE_FIELD_NUM ||
            resourceNameFields[RESOURCE_IDX] != resource_view::HETEROGENEOUS_CARDNUM_KEY) {
            continue;
        }
        return req.first;
    }
    return "";
}

bool IsResourceAvailable(const Category &availableResource, const resources::Resource &req)
{
    int reqVal = static_cast<int>(req.scalar().value());
    for (const auto &pair : availableResource.vectors()) {
        for (const auto &cardResource : pair.second.values()) {
            if (reqVal <= static_cast<int>(cardResource)) {
                return true;
            }
        }
    }
    return false;
}

int CountAvailableCards(const resources::Resource &availableResource, const resources::Resource &capacityResource,
                        const std::string &resourceType, double req)
{
    int cnt = 0;
    // pair.first : uuid; pair.second : actual available resources
    auto &capacity = capacityResource.vectors().values().at(resourceType).vectors();
    for (const auto &[uuid, availVec]  : availableResource.vectors().values().at(resourceType).vectors()) {
        auto capIter = capacity.find(uuid);
        if (capIter == capacity.end() || capIter->second.values().size() != availVec.values().size()) {
            YRLOG_WARN("can not find capacity or size is not equal to avail for {}.", uuid);
            continue;
        }
        const auto& capValues = capIter->second.values();
        for (int i = 0; i < availVec.values().size(); ++i) {
            const double required = capValues.at(i) * req;
            // rg resource cap maybe 0 because it only requires part of device, need to filter it
            if (capValues.at(i) > EPSINON && availVec.values().at(i) > required - EPSINON) {
                ++cnt;
            }
        }
    }
    return cnt;
}

std::string DefaultHeterogeneousFilter::GetPluginName()
{
    return DEFAULT_HETEROGENEOUS_FILTER_NAME;
}

schedule_framework::Filtered DefaultHeterogeneousFilter::Filter(
    const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
    const resource_view::InstanceInfo &instance, const resource_view::ResourceUnit &resourceUnit)
{
    schedule_framework::Filtered result{};
    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    if (preContext == nullptr) {
        YRLOG_WARN("invalid context for DefaultHeterogeneousFilter");
        return schedule_framework::Filtered{
            Status(StatusCode::PARAMETER_ERROR, "Invalid context"), true, 0};
    }
    if (!resource_view::HasHeterogeneousResource(instance)) {
        result.status = Status::OK();
        return result;
    }
    result.status = Filtering(instance, preContext, resourceUnit);
    if (!result.status.IsOk()) {
        YRLOG_DEBUG("{} filtered by heterogeneous plugin", resourceUnit.id());
        return result;
    }
    result.availableForRequest = 1;
    return result;
}

Status DefaultHeterogeneousFilter::Filtering(const resource_view::InstanceInfo &instance,
                                             const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext,
                                             const resources::ResourceUnit &unit) const
{
    auto available = unit.allocatable();
    if (auto iter(preContext->allocated.find(unit.id())); iter != preContext->allocated.end()) {
        available = unit.allocatable() - iter->second.resource;
        if (!resource_view::IsValid(available)) {
            YRLOG_WARN("Invalid available resource is found during heterogeneous filter");
            return Status(HETEROGENEOUS_SCHEDULE_FAILED, "Invalid Resource");
        }
    }
    auto status = CheckAndCompareForCardResource(instance, unit, available);
    if (!status.IsOk()) {
        YRLOG_WARN("CheckAndCompareForCardResource error during heterogeneous filter");
        return status;
    }
    status = CheckAndCompareForCardNum(instance, unit, available);
    if (!status.IsOk()) {
        YRLOG_WARN("CheckAndCompareForCardNum error during heterogeneous filter");
        return status;
    }
    return Status::OK();
}

Status DefaultHeterogeneousFilter::CheckAndCompareForCardResource(const resource_view::InstanceInfo &instance,
    const resource_view::ResourceUnit &resourceUnit, const resources::Resources &available) const
{
    for (const auto &req : instance.resources().resources()) {
        auto cardTypeRegex = GetHeteroCardTypeFromResName(req.first);
        if (cardTypeRegex.empty()) {
            continue;
        }

        auto resourceType = litebus::strings::Split(req.first, "/")[RESOURCE_IDX];
        // Skip latency and card number checks
        if (resourceType == resource_view::HETEROGENEOUS_LATENCY_KEY ||
            resourceType == resource_view::HETEROGENEOUS_CARDNUM_KEY) {
            continue;
        }

        if (resource_view::ScalaValueIsEmpty(req.second)) {
            YRLOG_DEBUG("{}|{} in the request is 0.", instance.requestid(), resourceType);
            return Status(StatusCode::PARAMETER_ERROR, "Invalid " + resourceType + " Value");
        }

        auto cardType = GetResourceCardTypeByRegex(available, cardTypeRegex);
        if (cardType.empty()) {
            YRLOG_WARN("{}|no available card type for regex({}).", instance.requestid(), cardTypeRegex);
            return Status(HETEROGENEOUS_SCHEDULE_FAILED, "Card Type: Not Found");
        }

        if (!HasHeteroResourceInResources(available, cardType, resourceType)) {
            YRLOG_WARN("{}|no available {} {} in unit({}) for schedule.", instance.requestid(), cardType, resourceType,
                       resourceUnit.id());
            return Status(HETEROGENEOUS_SCHEDULE_FAILED, resourceType + ": Not Found");
        }
        auto availableResource = available.resources().at(cardType).vectors().values().at(resourceType);
        if (IsResourceAvailable(availableResource, req.second)) {
            YRLOG_DEBUG("{}|{}.{} available({}) >= req({}) in unit({})", instance.requestid(), cardType, resourceType,
                        availableResource.ShortDebugString(), req.second.scalar().value(), resourceUnit.id());
            continue;
        }
        YRLOG_WARN("{}|{}.{} available({}) < req({}) in unit({}) for schedule.", instance.requestid(), cardType,
                   resourceType, availableResource.ShortDebugString(), req.second.scalar().value(), resourceUnit.id());
        return Status(HETEROGENEOUS_SCHEDULE_FAILED, resourceType + ": Not Enough");
    }

    return Status::OK();
}

Status DefaultHeterogeneousFilter::CheckAndCompareForCardNum(const resource_view::InstanceInfo &instance,
                                                             const resource_view::ResourceUnit &resourceUnit,
                                                             const resources::Resources &available) const
{
    std::string cardNumKey = FindCardNumKey(instance);
    if (cardNumKey.empty()) {
        return Status::OK();
    }

    auto &reqResource = instance.resources().resources().at(cardNumKey);
    double reqResourceValue = reqResource.scalar().value();
    if (reqResourceValue < MIN_NUM_THRESHOLD
        || (reqResourceValue > NUM_THRESHOLD && abs(reqResourceValue - std::round(reqResourceValue)) > EPSINON)) {
        YRLOG_WARN(
            "{}|specified quantity {} is invalid because quantity >1 must be whole numbers or can not less than {}",
            instance.requestid(), reqResourceValue, MIN_NUM_THRESHOLD);
        return Status(PARAMETER_ERROR,
                      "specified quantity " + std::to_string(reqResourceValue)
                          + " is invalid because quantity >1 must be whole numbers or can not less than 0.0001");
    }
    auto reqNum = reqResourceValue > INT_MAX ? INT_MAX : static_cast<int>(std::ceil(reqResourceValue));
    auto cardTypeRegex = GetHeteroCardTypeFromResName(reqResource.name());
    if (cardTypeRegex.empty()) {
        return Status::OK();
    }

    auto cardType = GetResourceCardTypeByRegex(available, cardTypeRegex);
    if (cardType.empty()) {
        YRLOG_WARN("{}|no available card type for regex({}).", instance.requestid(), cardTypeRegex);
        return Status(HETEROGENEOUS_SCHEDULE_FAILED, "Card Type Not Found");
    }

    auto avail = available.resources().find(cardType);
    auto capacity = resourceUnit.capacity().resources().find(cardType);
    if (avail == available.resources().end() || capacity == resourceUnit.capacity().resources().end()
        || avail->second.vectors().values().find(resource_view::HETEROGENEOUS_MEM_KEY)
               == avail->second.vectors().values().end()
        || capacity->second.vectors().values().find(resource_view::HETEROGENEOUS_MEM_KEY)
               == capacity->second.vectors().values().end()) {
        YRLOG_WARN("{}|no available {} {} in unit({}) for schedule.", instance.requestid(),
                   cardType, resource_view::HETEROGENEOUS_MEM_KEY, resourceUnit.id());
        return Status(HETEROGENEOUS_SCHEDULE_FAILED, "HBM: Not Found");
    }

    int cnt = CountAvailableCards(avail->second, capacity->second, resource_view::HETEROGENEOUS_MEM_KEY,
                                  reqResourceValue < NUM_THRESHOLD ? reqResourceValue : REQUIRE_FACTOR);
    if (cnt >= reqNum) {
        YRLOG_DEBUG("{}|{}.{} available {}({}) >= req({}) in unit({})", instance.requestid(),
                    cardType, resource_view::HETEROGENEOUS_CARDNUM_KEY,
                    avail->second.vectors().values().at(resource_view::HETEROGENEOUS_MEM_KEY).ShortDebugString(),
                    cnt, reqNum, resourceUnit.id());
        return Status::OK();
    }
    YRLOG_WARN("{}|{}.{} is insufficient: available {}({}) < req({}) in unit({}) for schedule.", instance.requestid(),
               cardType, resource_view::HETEROGENEOUS_CARDNUM_KEY,
               avail->second.vectors().values().at(resource_view::HETEROGENEOUS_MEM_KEY).ShortDebugString(),
               cnt, reqNum, resourceUnit.id());
    return Status(HETEROGENEOUS_SCHEDULE_FAILED, "card count: Not Enough");
}
std::shared_ptr<schedule_framework::FilterPlugin> DefaultHeterogeneousFilterPolicyCreator()
{
    return std::make_shared<DefaultHeterogeneousFilter>();
}

REGISTER_SCHEDULER_PLUGIN(DEFAULT_HETEROGENEOUS_FILTER_NAME, DefaultHeterogeneousFilterPolicyCreator);
}  // namespace functionsystem::schedule_plugin::filter