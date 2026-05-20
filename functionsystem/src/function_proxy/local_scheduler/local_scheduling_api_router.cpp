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

#include "local_sched_driver.h"

namespace functionsystem::local_scheduler {
namespace {
const std::string UPDATE_LOCAL_SCHEDULING_STATUS_URL = "/localschedulingstatus";

std::string GetLocalSchedulingStatusLabel(resource_view::UnitStatus status)
{
    return status == resource_view::UnitStatus::EVICTING ? "evicting" : "normal";
}

std::string BuildUpdateLocalSchedulingStatusBody(const std::string &status, const std::string &message)
{
    return "{\"status\":\"" + status + "\",\"message\":\"" + message + "\"}";
}
}  // namespace

void LocalSchedulingApiRouter::InitUpdateSchedulingStatusHandler(
    const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
{
    auto updateSchedulingStatusHandler =
        [resourceViewMgr](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        if (resourceViewMgr == nullptr) {
            return HttpResponse(litebus::http::ResponseCode::SERVICE_UNAVAILABLE);
        }
        resource_view::UnitStatus targetStatus = resource_view::UnitStatus::NORMAL;
        if (request.method == "POST") {
            targetStatus = resource_view::UnitStatus::EVICTING;
        } else if (request.method != "DELETE") {
            return GenerateHttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED,
                                        BuildUpdateLocalSchedulingStatusBody("unknown",
                                                                            "only POST and DELETE are supported"));
        }

        const auto statusLabel = GetLocalSchedulingStatusLabel(targetStatus);
        return resourceViewMgr->UpdateAllUnitStatus(targetStatus).Then(
            [statusLabel](const Status &status) -> litebus::Future<HttpResponse> {
                if (!status.IsOk()) {
                    return GenerateHttpResponse(litebus::http::ResponseCode::SERVICE_UNAVAILABLE,
                                                BuildUpdateLocalSchedulingStatusBody(statusLabel,
                                                                                    status.ToString()));
                }
                return GenerateHttpResponse(litebus::http::ResponseCode::OK,
                                            BuildUpdateLocalSchedulingStatusBody(statusLabel, "success"));
            });
    };
    RegisterHandler(UPDATE_LOCAL_SCHEDULING_STATUS_URL, updateSchedulingStatusHandler);
}
}  // namespace functionsystem::local_scheduler
