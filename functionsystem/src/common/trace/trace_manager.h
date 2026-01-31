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
#include <string>
#include <vector>
#include <mutex>
#include "common/logs/logging.h"
#include "common/proto/pb/posix_pb.h"
#include "common/utils/singleton.h"
#include "common/trace/trace_struct.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_options.h"

namespace functionsystem {
namespace trace {

class TraceManager : public Singleton<TraceManager> {
public:
    using OtelSpan = opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>;

    struct SpanParam {
        std::string spanName;
        std::string traceID;
        std::string spanID;
        std::string function;
        std::string instanceID;
    };

    // Initialize and shutdown
    void InitTrace(const std::string &serviceName, const std::string &hostID, const bool &enableTrace,
                   const std::string &traceConfig);
    void ShutDown();

    void SetAttr(const std::string &attr, const std::string &value);

    // Basic span creation
    OtelSpan StartSpan(const std::string &name, const opentelemetry::common::KeyValueIterable &attributes,
                      const opentelemetry::trace::SpanContextKeyValueIterable &links,
                      const opentelemetry::trace::StartSpanOptions &startSpanOptions);

    OtelSpan StartSpan(const std::string &name,
                      const opentelemetry::trace::StartSpanOptions &startSpanOptions);

    OtelSpan StartSpan(const std::string &name,
                      std::vector<std::pair<const std::string, const opentelemetry::common::AttributeValue>> attrs,
                      const opentelemetry::trace::StartSpanOptions &startSpanOptions);

    OtelSpan StartSpan(const std::string &name, const std::string &traceID, const std::string &spanID,
                      std::vector<std::pair<const std::string, const opentelemetry::common::AttributeValue>> attrs);

    // Span creation with params
    OtelSpan StartSpan(SpanParam &&spanParam);
    OtelSpan StartSpanWithRecord(SpanParam &&spanParam);

    // Span management
    void StopSpan(const std::string &traceID, const std::string &spanName,
                  std::vector<std::pair<const std::string, const opentelemetry::common::AttributeValue>> attrs = {},
                  const std::vector<std::string> &events = {});

    std::string GetSpanIDFromStore(const std::string &traceID, const std::string &spanName);
    void Clear();

    // Request-specific span creation
    opentelemetry::trace::SpanId StartInvokeSpan(const std::string &spanName, const InvokeRequest &request);
    opentelemetry::trace::SpanId StartCallSpan(const std::string &spanName, const std::string &instanceID,
                                               const runtime::CallRequest &request);
    OtelSpan StartInvokeLocalSpan(const std::string &spanName, const InvokeRequest &request);
    void StartLocalSpanAndSet(const std::string &spanName, InvokeRequest *request);

    // Utility methods
    static std::string SpanIDToStr(const opentelemetry::trace::SpanId &spanId);
    static std::string TraceIDToStr(const opentelemetry::trace::TraceId &traceID);
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> GetTracer(
        const std::string &name = "yuanrong", const std::string &version = "");

private:
    template <typename T>
    opentelemetry::trace::SpanId StartReqSpan(const std::string &spanName, const std::string &instanceID,
                                              const T &request);

    std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> InitOtlpGrpcExporter(const OtelGrpcExporterConfig &conf);
    std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> InitLogFileExporter();
    opentelemetry::trace::StartSpanOptions BuildOptWithParent(const std::string &traceID, const std::string &spanID);

    bool enableTrace_{ false };
    std::map<std::string, std::string> attribute_;
    std::map<std::string, OtelSpan> spanMap_;
    std::mutex spanMapMutex_;
    std::string hostID_;
};

}  // namespace trace
}  // namespace functionsystem

#endif  // COMMON_TRACE_TRACE_MANAGER_H
