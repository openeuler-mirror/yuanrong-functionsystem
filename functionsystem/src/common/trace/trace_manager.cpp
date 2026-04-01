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

#include "trace_manager.h"
#include <nlohmann/json.hpp>
#include <opentelemetry/exporters/ostream/common_utils.h>
#include <opentelemetry/sdk/common/attribute_utils.h>
#include "common/proto/pb/posix_pb.h"
#include "exporter/log_file_exporter_factory.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/batch_span_processor.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"

namespace functionsystem {
namespace trace {

using std::string;

constexpr uint32_t TRACE_ID_LENGTH = 32;
constexpr uint32_t SPAN_ID_LENGTH = 16;
constexpr uint32_t TRACE_ID_BUF_LENGTH = 16;
constexpr uint32_t SPAN_ID_BUF_LENGTH = 8;

static inline bool IsHexDecimal(const std::string &str)
{
    return std::all_of(str.begin(), str.end(), ::isxdigit);
}

static void TraceIdStrToArr(std::string traceID, uint8_t (&arr)[TRACE_ID_BUF_LENGTH])
{
    // cut trace id prefix job-xxxxxxxx-trace-
    (void)traceID.erase(std::remove(traceID.begin(), traceID.end(), '-'), traceID.end());
    if (traceID.size() < TRACE_ID_LENGTH) {
        (void)traceID.append(TRACE_ID_LENGTH - traceID.size(), '0');
    }
    traceID = traceID.substr(traceID.size() - TRACE_ID_LENGTH, TRACE_ID_LENGTH);
    if (!IsHexDecimal(traceID)) {
        return;
    }
    if (traceID.length() != TRACE_ID_LENGTH && traceID.length() != (TRACE_ID_LENGTH - 1)) {
        YRLOG_WARN("invalid length: {}, traceID: {}", traceID.length(), traceID);
        return;
    }
    YRLOG_DEBUG("load trace id: {} string to buffer array", traceID);
    int pivot = 0;
    // convert each 2 digits to 1 trace id element
    for (size_t i = 0; i < traceID.length(); i += 2) {
        std::string sub = traceID.substr(i, 2);
        int value = std::stoi(sub, nullptr, 16);
        arr[pivot++] = uint8_t(value);
    }
}

static void SpanIdStrToArr(const std::string &spanID, uint8_t (&arr)[SPAN_ID_BUF_LENGTH])
{
    if (spanID.length() != SPAN_ID_LENGTH && spanID.length() != (SPAN_ID_LENGTH - 1)) {
        YRLOG_WARN("invalid length: {}, spanID: {}", spanID.length(), spanID);
        return;
    }
    int pivot = 0;
    // convert each 2 digits to 1 span id element
    for (size_t i = 0; i < spanID.length(); i += 2) {
        std::string sub = spanID.substr(i, 2);
        int value = std::stoi(sub, nullptr, 16);
        arr[pivot++] = uint8_t(value);
    }
}

// Initialize and shutdown
void TraceManager::InitTrace(const std::string &serviceName, const std::string &hostID, const bool &enableTrace,
                             const std::string &traceConfig)
{
    enableTrace_ = enableTrace;
    YRLOG_INFO("init trace, enableTrace is {}, traceConfig is {}", enableTrace, traceConfig);
    if (!enableTrace_) {
        return;
    }
    hostID_ = hostID;
    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> processors;
    try {
        auto confJson = nlohmann::json::parse(traceConfig);
        for (auto &element : confJson.items()) {
            if (element.key() == OTLP_GRPC_EXPORTER) {
                if (!element.value().contains("enable") || !element.value().at("enable").get<bool>()) {
                    YRLOG_INFO("Trace exporter {} is not enabled", OTLP_GRPC_EXPORTER);
                    continue;
                }
                if (!element.value().contains("endpoint")
                    || element.value().at("endpoint").get<std::string>().empty()) {
                    YRLOG_INFO("Trace exporter {} endpoint is not valid", OTLP_GRPC_EXPORTER);
                    continue;
                }
                OtelGrpcExporterConfig config;
                config.endpoint = element.value().at("endpoint").get<std::string>();
                opentelemetry::sdk::trace::BatchSpanProcessorOptions batchSpanProcessorOptions;
                YRLOG_INFO("OtelGrpcExporter is enable, endpoint is {}", config.endpoint);
                processors.push_back(
                    std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>(
                        opentelemetry::sdk::trace::BatchSpanProcessorFactory::Create(
                            std::move(InitOtlpGrpcExporter(config)), batchSpanProcessorOptions)));
            } else if (element.key() == LOG_FILE_EXPORTER) {
                if (!element.value().contains("enable")
                    || !element.value().at("enable").get<bool>()) {
                    YRLOG_INFO("Trace exporter {} is not enabled", LOG_FILE_EXPORTER);
                    continue;
                }
                opentelemetry::sdk::trace::BatchSpanProcessorOptions batchSpanProcessorOptions;
                YRLOG_INFO("logFileExporter is enable");
                processors.push_back(
                    std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>(
                        opentelemetry::sdk::trace::BatchSpanProcessorFactory::Create(
                            std::move(InitLogFileExporter()), batchSpanProcessorOptions)));
            }
        }
    } catch (nlohmann::detail::parse_error &e) {
        YRLOG_ERROR("Failed to parse trace config json, error: {}", e.what());
        enableTrace_ = false;
        return;
    } catch (std::exception &e) {
        YRLOG_ERROR("Failed to parse trace config json, error: {}", e.what());
        enableTrace_ = false;
        return;
    }
    if (processors.empty()) {
        YRLOG_WARN("There is no supported exporter in config");
        enableTrace_ = false;
        return;
    }
    opentelemetry::sdk::resource::ResourceAttributes attributes = {
        { opentelemetry::sdk::resource::SemanticConventions::kTelemetrySdkLanguage, "" },
        { opentelemetry::sdk::resource::SemanticConventions::kTelemetrySdkName, "" },
        { opentelemetry::sdk::resource::SemanticConventions::kTelemetrySdkVersion, "" },
        { opentelemetry::sdk::resource::SemanticConventions::kServiceName, serviceName },
    };
    auto provider = std::shared_ptr<opentelemetry::trace::TracerProvider>(
        std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
            std::move(processors), opentelemetry::sdk::resource::Resource::Create(attributes)));
    opentelemetry::trace::Provider::SetTracerProvider(provider);
}

void TraceManager::ShutDown()
{
    if (!enableTrace_) {
        return;
    }
    YRLOG_INFO("enter TraceManager shutDown");
    enableTrace_ = false;
    auto provider = opentelemetry::trace::Provider::GetTracerProvider();
    auto traceProvider = static_cast<opentelemetry::sdk::trace::TracerProvider*>(provider.get());
    if (traceProvider != nullptr && !traceProvider->ForceFlush()) {
        YRLOG_WARN("traceProvider shutDown failed");
    }
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> none;
    opentelemetry::trace::Provider::SetTracerProvider(none);
}

void TraceManager::SetAttr(const std::string &attr, const std::string &value)
{
    attribute_.insert_or_assign(attr, value);
}

// ============================================================================
// Private Helper Methods
// ============================================================================

TraceManager::OtelSpan TraceManager::CreateNoopSpan()
{
    static auto noopTracer = std::make_shared<opentelemetry::trace::NoopTracer>();
    return opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>(
        new opentelemetry::trace::NoopSpan(noopTracer));
}

// ============================================================================
// Span Creation APIs
// ============================================================================

// Core span creation with full OpenTelemetry parameters
TraceManager::OtelSpan TraceManager::StartSpan(const std::string &name,
                                               const opentelemetry::common::KeyValueIterable &attributes,
                                               const opentelemetry::trace::SpanContextKeyValueIterable &links,
                                               const opentelemetry::trace::StartSpanOptions &options)
{
    if (!enableTrace_) {
        return CreateNoopSpan();
    }

    try {
        auto tracer = GetTracer();
        if (tracer != nullptr) {
            return tracer->StartSpan(name, attributes, links, options);
        }
    } catch (const std::exception &e) {
        YRLOG_ERROR("StartSpan exception: {}", e.what());
    }

    return CreateNoopSpan();
}

// Simple span creation with only options
TraceManager::OtelSpan TraceManager::StartSpan(const std::string &name,
                                               const opentelemetry::trace::StartSpanOptions &options)
{
    return StartSpan(name, opentelemetry::common::NoopKeyValueIterable(),
                     opentelemetry::trace::NullSpanContext(), options);
}

// Span creation with parent context and typed attributes
TraceManager::OtelSpan TraceManager::StartSpan(
    const std::string &name,
    const std::string &traceID,
    const std::string &spanID,
    AttributesVector &attrs)
{
    YRLOG_DEBUG("start span with traceID and spanID, name: {}, traceID: {}, spanID: {}", name, traceID, spanID);

    auto options = BuildOptWithParent(traceID, spanID);
    for (auto it : attribute_) {
        attrs.emplace_back(it);
    }
    return StartSpan(name, opentelemetry::common::KeyValueIterableView(attrs),
                     opentelemetry::trace::NullSpanContext(), options);
}

// ============================================================================
// Span with Record Management (lifecycle tracking)
// ============================================================================

// Start span and record it for later retrieval (e.g., for StopSpan)
TraceManager::OtelSpan TraceManager::StartSpanWithRecord(TraceManager::SpanParam &&spanParam)
{
    std::string spanKey = spanParam.traceID + "_" + spanParam.spanName;
    YRLOG_DEBUG("(trace)start span, spanName: {}, traceID: {}, spanID: {}, function: {}, instanceID: {}",
                spanParam.spanName, spanParam.traceID, spanParam.spanID, spanParam.function, spanParam.instanceID);

    AttributesVector attrs;
    if (!spanParam.function.empty()) {
        attrs.emplace_back("yr.function", spanParam.function);
    }
    if (!spanParam.instanceID.empty()) {
        attrs.emplace_back("yr.instance_id", spanParam.instanceID);
    }
    if (!hostID_.empty()) {
        attrs.emplace_back("host.id", hostID_);
    }

    auto span = StartSpan(spanParam.spanName, spanParam.traceID, spanParam.spanID, attrs);
    if (span != nullptr) {
        std::lock_guard<std::mutex> lock(spanMapMutex_);
        spanMap_.emplace(spanKey, span);
    }

    return span;
}

// ============================================================================
// Span Lifecycle Management
// ============================================================================
void TraceManager::StopSpan(const std::string &spanName, const std::string &traceID,
                            const AttributesVector &attrs,
                            const std::vector<std::string> &events)
{
    std::string spanKey = traceID + "_" + spanName;
    YRLOG_DEBUG("stop span, key: {}", spanKey);

    OtelSpan span;
    {
        std::lock_guard<std::mutex> lock(spanMapMutex_);
        auto it = spanMap_.find(spanKey);
        if (it == spanMap_.end()) {
            YRLOG_WARN("no span: {} found with traceID: {}", spanName, traceID);
            return;
        }
        span = it->second;
    }

    for (const auto &event : events) {
        YRLOG_DEBUG("stopspan add event: {}", event);
        span->AddEvent(event);
    }
    for (const auto &[key, value] : attrs) {
        YRLOG_DEBUG("stopspan add attr: {}", key);
        span->SetAttribute(key, value);
    }
    opentelemetry::trace::EndSpanOptions options;
    options.end_steady_time = opentelemetry::common::SteadyTimestamp(std::chrono::steady_clock::now());
    span->End(options);

    {
        std::lock_guard<std::mutex> lock(spanMapMutex_);
        spanMap_.erase(spanKey);
        YRLOG_DEBUG("stop current span, traceID: {}, spanName: {}", traceID, spanName);
    }
}

std::string TraceManager::GetSpanIDFromStore(const std::string &traceID, const std::string &spanName)
{
    std::lock_guard<std::mutex> lock(spanMapMutex_);
    auto spanKey = traceID + "_" + spanName;
    auto it = spanMap_.find(spanKey);
    if (it == spanMap_.end()) {
        YRLOG_WARN("cannot find span in spanMap_. spanKey: {}", spanKey);
        return "";
    }
    auto spanID = it->second->GetContext().span_id();
    return SpanIDToStr(spanID);
}

void TraceManager::Clear()
{
    std::lock_guard<std::mutex> lock(spanMapMutex_);
    spanMap_.clear();
}

// methods
std::string TraceManager::SpanIDToStr(const opentelemetry::trace::SpanId &spanId)
{
    std::string spanIDStr;
    for (auto it = spanId.Id().begin(); it != spanId.Id().end(); ++it) {
        auto value = static_cast<int>(*it);
        std::ostringstream ss;
        // fill 0 in front if id element just has 1 digit
        ss << std::setfill('0') << std::setw(2) << std::hex << value;  // 2: output wide
        std::string element = ss.str();
        (void)spanIDStr.append(element);
    }
    return spanIDStr;
}

std::string TraceManager::TraceIDToStr(const opentelemetry::trace::TraceId &traceID)
{
    std::string traceIDStr;
    for (auto it = traceID.Id().begin(); it != traceID.Id().end(); ++it) {
        auto value = static_cast<int>(*it);
        std::ostringstream ss;
        // fill 0 in front if id element just has 1 digit
        ss << std::setfill('0') << std::setw(2) << std::hex << value;  // 2: output wide
        std::string element = ss.str();
        (void)traceIDStr.append(element);
    }
    return traceIDStr;
}

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> TraceManager::GetTracer(const std::string &name,
                                                                                       const std::string &version)
{
    auto provider = opentelemetry::trace::Provider::GetTracerProvider();
    return provider->GetTracer(name, version);
}

// Private methods
std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> TraceManager::InitLogFileExporter()
{
    return LogFileExporterFactory::Create();
}

std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> TraceManager::InitOtlpGrpcExporter(
    const OtelGrpcExporterConfig &conf)
{
    if (conf.endpoint.empty()) {
        return nullptr;
    }
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions options;
    options.endpoint = conf.endpoint;
    return opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(options);
}

opentelemetry::trace::StartSpanOptions TraceManager::BuildOptWithParent(const std::string &traceID,
                                                                        const std::string &spanID)
{
    YRLOG_DEBUG("build options with parent, traceID: {}, spanID: {}", traceID, spanID);

    opentelemetry::trace::StartSpanOptions options;
    if (!traceID.empty()) {
        uint8_t traceIdArr[TRACE_ID_BUF_LENGTH] = {};
        uint8_t spanIdArr[SPAN_ID_BUF_LENGTH] = {};

        TraceIdStrToArr(traceID, traceIdArr);
        opentelemetry::trace::TraceId optlTraceId(traceIdArr);
        if (spanID.empty()) {
            spanIdArr[SPAN_ID_BUF_LENGTH - 1] = 0x01;
            YRLOG_DEBUG("spanID is empty, set root span");
        } else {
            SpanIdStrToArr(spanID, spanIdArr);
        }
        opentelemetry::trace::SpanId optlSpanId(spanIdArr);
        opentelemetry::trace::SpanContext spanContext(optlTraceId, optlSpanId, {}, false);

        YRLOG_DEBUG("option is valid({})", spanContext.IsValid());

        options.parent = spanContext;
    } else {
        YRLOG_DEBUG("traceID is empty");
    }

    options.start_steady_time = opentelemetry::common::SteadyTimestamp(std::chrono::steady_clock::now());
    return options;
}

}  // namespace trace
}  // namespace functionsystem
