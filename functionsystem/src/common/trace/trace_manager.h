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


#ifndef COMMON_TRACE_TRACE_MANAGER_H
#define COMMON_TRACE_TRACE_MANAGER_H

#define HAVE_ABSEIL
#include "common/logs/logging.h"
#include "common/proto/pb/posix_pb.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/trace_id.h"
#include "trace_actor.h"

namespace functionsystem {

namespace trace {

class TraceManager {
public:
    const static uint32_t TRACE_ID_LENGTH = 32;
    const static uint32_t SPAN_ID_LENGTH = 16;
    const static uint32_t TRACE_ID_BUF_LENGTH = 16;
    const static uint32_t SPAN_ID_BUF_LENGTH = 8;
    const static uint32_t TRACE_ID_PREFIX_LENGTH = 19;

    using OtelSpanSharedPtr = opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>;
    struct SpanParam {
        std::string spanName;
        std::string traceID;
        std::string spanID;
        std::string function;
        std::string instanceID;
    };

    static void BindTraceActor(const std::shared_ptr<TraceActor> &traceActor);

    static void BindModuleInfo(const std::string &hostID);

    static OtelSpanSharedPtr StartSpanWithRecord(SpanParam &&spanParam);

    static OtelSpanSharedPtr StartSpan(SpanParam &&spanParam);

    static void StopSpan(const std::string &traceID, const std::string &spanName,
                         const std::vector<std::string> &events = {});

    [[maybe_unused]] static std::string GetSpanIDFromStore(const std::string &traceID, const std::string &spanName);

    static void TraceIdStrToArr(std::string traceID, uint8_t (&arr)[TRACE_ID_BUF_LENGTH]);

    static void SpanIdStrToArr(const std::string &spanID, uint8_t (&arr)[SPAN_ID_BUF_LENGTH]);

    static std::string SpanIDToStr(const opentelemetry::trace::SpanId &spanId);

    static std::string TraceIDToStr(const opentelemetry::trace::TraceId &traceID);

    static void StartLocalSpanAndSet(const std::string &spanName, InvokeRequest *request);

    static void Clear();

    static opentelemetry::trace::StartSpanOptions BuildOptWithParent(const std::string &traceID,
                                                                     const std::string &spanID);

    static opentelemetry::trace::SpanId StartInvokeSpan(const std::string &spanName, const InvokeRequest &request);

    static opentelemetry::trace::SpanId StartCallSpan(const std::string &spanName, const std::string &instanceID,
                                                      const runtime::CallRequest &request);

    static OtelSpanSharedPtr StartInvokeLocalSpan(const std::string &spanName, const InvokeRequest &request);

    static opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> GetTracer(
        const std::string &name = "yuanrong", const std::string &version = "");

private:
    template <typename T>
    static opentelemetry::trace::SpanId StartReqSpan(const std::string &spanName, const std::string &instanceID,
                                                     const T &request);

    inline static std::map<std::string, OtelSpanSharedPtr> spanMap_;

    inline static std::shared_ptr<TraceActor> traceActor_{ nullptr };

    inline static std::string moduleName_;
    inline static std::string hostID_;
};

}  // namespace trace
}  // namespace functionsystem

#endif  // COMMON_TRACE_TRACE_MANAGER_H
