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

#ifndef COMMON_TRACE_CREATE_TRACE_HELPER_H
#define COMMON_TRACE_CREATE_TRACE_HELPER_H

#include <memory>

#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/runtime_launcher_interface.grpc.pb.h"
#include "common/trace/trace_manager.h"
#include "function_agent/common/types.h"

namespace functionsystem::trace {

inline constexpr char kCodeDownloadSpanName[] = "yr.instance.download_code";
inline constexpr char kCreateSandboxSpanName[] = "yr.instance.create_sandbox";

inline void StartCodeDownloadSpan(const std::shared_ptr<messages::DeployInstanceRequest> &request)
{
    if (request == nullptr || request->requestid().empty()) {
        return;
    }

    TraceManager::SpanParam param;
    param.spanName = kCodeDownloadSpanName;
    param.spanKey = request->requestid();
    param.traceID = request->traceid();
    param.traceParent = TraceManager::GetTraceParentFromOptions(
        request->createoptions(), &request->scheduleoption().extension());
    param.instanceID = request->instanceid();

    auto span = TraceManager::GetInstance().StartSpanWithRecord(std::move(param));
    TraceManager::PropagateSpanToOptions(
        span, request->mutable_createoptions(), request->mutable_scheduleoption()->mutable_extension());
}

inline void StopCodeDownloadSpan(const std::shared_ptr<messages::DeployInstanceRequest> &request,
                                 const function_agent::DeployResult &result)
{
    if (request == nullptr || request->requestid().empty()) {
        return;
    }

    AttributesVector attrs;
    if (result.status.IsError()) {
        attrs.emplace_back("yr.download.error_code", static_cast<int64_t>(result.status.StatusCode()));
        if (!result.status.RawMessage().empty()) {
            attrs.emplace_back("yr.download.error_message", result.status.RawMessage());
        }
    }
    TraceManager::GetInstance().StopSpan(kCodeDownloadSpanName, request->requestid(), attrs);
}

inline void StartSandboxCreateSpan(const std::shared_ptr<messages::StartInstanceRequest> &request)
{
    if (request == nullptr || request->runtimeinstanceinfo().requestid().empty()) {
        return;
    }

    const auto &info = request->runtimeinstanceinfo();
    TraceManager::SpanParam param;
    param.spanName = kCreateSandboxSpanName;
    param.spanKey = info.requestid();
    param.traceID = info.traceid();
    param.traceParent = TraceManager::GetTraceParentFromOptions(
        request->scheduleoption().extension(), &info.deploymentconfig().deployoptions());
    param.instanceID = info.instanceid();

    auto span = TraceManager::GetInstance().StartSpanWithRecord(std::move(param));
    TraceManager::PropagateSpanToOptions(
        span,
        request->mutable_scheduleoption()->mutable_extension(),
        request->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->mutable_deployoptions());
}

inline void StopSandboxCreateSpan(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                  const runtime::v1::StartResponse &response)
{
    if (request == nullptr || request->runtimeinstanceinfo().requestid().empty()) {
        return;
    }

    AttributesVector attrs;
    if (!response.id().empty()) {
        attrs.emplace_back("yr.sandbox_id", response.id());
    }
    if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        attrs.emplace_back("yr.sandbox.code", static_cast<int64_t>(response.code()));
        if (!response.message().empty()) {
            attrs.emplace_back("yr.sandbox.message", response.message());
        }
    }
    TraceManager::GetInstance().StopSpan(kCreateSandboxSpanName, request->runtimeinstanceinfo().requestid(), attrs);
}

}  // namespace functionsystem::trace

#endif  // COMMON_TRACE_CREATE_TRACE_HELPER_H
