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

// ABI 兼容性说明：
// OpenTelemetry SDK 已配置 -DWITH_STL=ON，统一使用 std::variant 和 std::string_view
// CMakeLists.txt 中定义了 OPENTELEMETRY_STL_VERSION=2017 以匹配 SDK

#include <string>
#include <vector>
#include <variant>
#include <mutex>
#include <google/protobuf/map.h>
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
using AttributesVector = std::vector<std::pair<const std::string, const opentelemetry::common::AttributeValue>>;

namespace SpanName {
inline constexpr char kCreate[] = "yr.create";
inline constexpr char kDomainSchedule[] = "yr.schedule.domain";
inline constexpr char kLocalSchedule[] = "yr.schedule.local";
inline constexpr char kForwardSchedule[] = "yr.schedule.forward";
inline constexpr char kDeployInstance[] = "yr.instance.deploy";
inline constexpr char kWaitConnection[] = "yr.instance.wait_connection";
}  // namespace SpanName

class TraceManager : public Singleton<TraceManager> {
public:
    // Type aliases for cleaner interface
    using OtelSpan = opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>;

    // Span creation parameters
    struct SpanParam {
        std::string spanName;
        std::string spanKey;
        std::string traceID;
        std::string spanID;
        std::string traceParent;
        std::string function;
        std::string instanceID;
    };

    // ========================================================================
    // Initialization & Shutdown
    // ========================================================================
    void InitTrace(const std::string &serviceName, const std::string &hostID, const bool &enableTrace,
                   const std::string &traceConfig);
    void ShutDown();

    // Set global attribute that will be added to all spans
    void SetAttr(const std::string &key, const std::string &value);

    // ========================================================================
    // Span Creation APIs
    // ========================================================================

    // Core span creation with full OpenTelemetry parameters
    OtelSpan StartSpan(const std::string &name,
                      const opentelemetry::common::KeyValueIterable &attributes,
                      const opentelemetry::trace::SpanContextKeyValueIterable &links,
                      const opentelemetry::trace::StartSpanOptions &options);

    // Simple span creation with only options
    OtelSpan StartSpan(const std::string &name,
                      const opentelemetry::trace::StartSpanOptions &options);

    // Span creation with parent context and typed attributes
    OtelSpan StartSpan(const std::string &name,
                      const std::string &traceID,
                      const std::string &spanID,
                      const std::string &traceParent,
                      AttributesVector &attrs);

    // Start span and record for later management (e.g., StopSpan)
    OtelSpan StartSpanWithRecord(SpanParam &&spanParam);

    // ========================================================================
    // Span Lifecycle Management
    // ========================================================================
    void StopSpan(const std::string &spanName,
                  const std::string &spanKey,
                  const AttributesVector &attrs = {},
                  const std::vector<std::string> &events = {});

    std::string GetSpanIDFromStore(const std::string &spanKey, const std::string &spanName);
    void Clear();

    // ========================================================================
    // Utility Methods
    // ========================================================================
    static std::string SpanIDToStr(const opentelemetry::trace::SpanId &spanId);
    static std::string TraceIDToStr(const opentelemetry::trace::TraceId &traceID);
    static std::string SpanContextToTraceParent(const opentelemetry::trace::SpanContext &spanContext);
    static std::string GetTraceParentFromSpan(const OtelSpan &span);
    static std::string GetTraceParentFromOptions(
        const google::protobuf::Map<std::string, std::string> &options,
        const google::protobuf::Map<std::string, std::string> *fallback = nullptr);
    static void SetTraceParentToOptions(google::protobuf::Map<std::string, std::string> *options,
                                        const std::string &traceParent);
    static void PropagateSpanToOptions(const OtelSpan &span,
                                       google::protobuf::Map<std::string, std::string> *options,
                                       google::protobuf::Map<std::string, std::string> *fallback = nullptr);
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> GetTracer(
        const std::string &name = "yuanrong", const std::string &version = "");

private:
    OtelSpan CreateNoopSpan();
    std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> InitOtlpGrpcExporter(const OtelGrpcExporterConfig &conf);
    std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> InitLogFileExporter();
    opentelemetry::trace::StartSpanOptions BuildOptWithParent(const std::string &traceID,
                                                              const std::string &spanID,
                                                              const std::string &traceParent);

    bool enableTrace_{ false };
    std::map<std::string, std::string> attribute_;      // Global attributes
    std::map<std::string, OtelSpan> spanMap_;           // Active spans for lifecycle management
    std::mutex spanMapMutex_;
    std::string hostID_;
};

}  // namespace trace
}  // namespace functionsystem

#endif  // COMMON_TRACE_TRACE_MANAGER_H
