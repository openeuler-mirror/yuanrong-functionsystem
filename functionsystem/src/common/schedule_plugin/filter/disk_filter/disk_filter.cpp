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

#include "disk_filter.h"

#include "common/logs/logging.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/resource_view/resource_tool.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/scala_resource_tool.h"
#include "common/schedule_plugin/common/plugin_register.h"

namespace functionsystem::schedule_plugin::filter {
Status CheckDiskResource(const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext,
                         const resource_view::InstanceInfo &instance,
                         const resource_view::ResourceUnit &resourceUnit)
{
    if (!resource_view::HasDiskResource(instance)) {
        return Status::OK();
    }

    if (!resource_view::HasDiskResource(resourceUnit)) {
        return Status(DISK_SCHEDULE_FAILED, "disk: Not Enough");
    }

    auto resourcesAvailable = resourceUnit.allocatable();
    if (auto iter(preContext->allocated.find(resourceUnit.id())); iter != preContext->allocated.end()) {
        resourcesAvailable = resourceUnit.allocatable() - iter->second.resource;
        if (!resource_view::IsValid(resourcesAvailable)) {
            return Status(DISK_SCHEDULE_FAILED, "Invalid Resourceunit");
        }
    }

    auto diskReq = instance.resources().resources().at(resource_view::DISK_RESOURCE_NAME);
    if (resource_view::ScalaValueIsEmpty(diskReq)) {
        YRLOG_DEBUG("{}|Invalid zero disk request.", instance.requestid());
        return Status(StatusCode::PARAMETER_ERROR, "Invalid disk value");
    }

    auto diskAvailable = resourcesAvailable.resources().at(resource_view::DISK_RESOURCE_NAME)
        .vectors().values().at(resource_view::DISK_RESOURCE_NAME);
    if (resource_view::IsVectorsAvailable(diskAvailable, diskReq)) {
        YRLOG_DEBUG("{}|disk available (avail={}, req={}, unit={})", instance.requestid(),
                    diskAvailable.ShortDebugString(), diskReq.scalar().value(), resourceUnit.id());
        return Status::OK();
    }

    YRLOG_DEBUG("{}|Disk insufficient (avail={}, req={}, unit={})", instance.requestid(),
                diskAvailable.ShortDebugString(), diskReq.scalar().value(), resourceUnit.id());
    return Status(DISK_SCHEDULE_FAILED, "disk: Not Enough");
}

std::string DiskFilter::GetPluginName()
{
    return DISK_FILTER_NAME;
}

schedule_framework::Filtered DiskFilter::Filter(
    const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
    const resource_view::InstanceInfo &instance, const resource_view::ResourceUnit &resourceUnit)
{
    schedule_framework::Filtered result{};
    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    if (preContext == nullptr) {
        YRLOG_WARN("Invalid context for DiskFilter");
        return schedule_framework::Filtered{ Status(StatusCode::PARAMETER_ERROR, "Invalid context"), true, -1 };
    }

    if (!resource_view::HasDiskResource(instance)) {
        result.status = Status::OK();
        return result;
    }

    result.status = CheckDiskResource(preContext, instance, resourceUnit);
    if (!result.status.IsOk()) {
        YRLOG_DEBUG("{} filtered by heterogeneous plugin", resourceUnit.id());
        return result;
    }
    result.availableForRequest = 1;
    return result;
}

std::shared_ptr<schedule_framework::FilterPlugin> DiskFilterPolicyCreator()
{
    return std::make_shared<DiskFilter>();
}

REGISTER_SCHEDULER_PLUGIN(DISK_FILTER_NAME, DiskFilterPolicyCreator);
}  // namespace functionsystem::schedule_plugin::filter