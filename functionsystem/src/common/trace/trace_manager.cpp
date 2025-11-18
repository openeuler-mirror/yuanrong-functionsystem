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
#include "common/proto/pb/posix_pb.h"

namespace functionsystem {
namespace trace {

using std::string;

inline static bool IsHexDecimal(const string &str)
{
    return std::all_of(str.begin(), str.end(), ::isxdigit);
}

void TraceManager::BindTraceActor(const std::shared_ptr<TraceActor> &traceActor)
{
    if (traceActor == nullptr) {
        YRLOG_WARN("(trace)trace actor is nullptr");
    }
    traceActor_ = traceActor;
}

void TraceManager::BindModuleInfo(const std::string &hostID)
{
    hostID_ = hostID;
}

opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> TraceManager::GetTracer(const std::string &name,
                                                                                       const std::string &version)
{
    auto provider = opentelemetry::trace::Provider::GetTracerProvider();
    return provider->GetTracer(name, version);
}

opentelemetry::trace::SpanId TraceManager::StartInvokeSpan(const std::string &spanName, const InvokeRequest &request)
{
    return StartReqSpan(spanName, request.instanceid(), request);
}

opentelemetry::trace::SpanId TraceManager::StartCallSpan(const std::string &spanName, const std::string &instanceID,
                                                         const runtime::CallRequest &request)
{
    return StartReqSpan(spanName, instanceID, request);
}

template <typename T>
opentelemetry::trace::SpanId TraceManager::StartReqSpan(const std::string &spanName, const std::string &instanceID,
                                                        const T &request)
{
    if (traceActor_ == nullptr) {
        return opentelemetry::trace::SpanId();
    }
    SpanParam spanParam = {
        .spanName = spanName,
        .traceID = request.traceid(),
        .spanID = request.spanid(),
        .function = request.function(),
        .instanceID = instanceID,
    };
    auto spanSharedPtr = TraceManager::StartSpanWithRecord(std::move(spanParam));
    if (spanSharedPtr == nullptr) {
        return opentelemetry::trace::SpanId();
    }
    return spanSharedPtr->GetContext().span_id();
}

TraceManager::OtelSpanSharedPtr TraceManager::StartSpan(TraceManager::SpanParam &&spanParam)
{
    YRLOG_DEBUG("(trace)start span, spanName: {}, traceID: {}, spanID: {}, function: {}, instanceID: {}",
                spanParam.spanName, spanParam.traceID, spanParam.spanID, spanParam.function, spanParam.instanceID);
    auto options = BuildOptWithParent(std::move(spanParam.traceID), std::move(spanParam.spanID));
    auto tracer = GetTracer();
    if (tracer == nullptr) {
        YRLOG_WARN("(trace)tracer is nullptr");
        return TraceManager::OtelSpanSharedPtr(nullptr);
    }
    return tracer->StartSpan(std::move(spanParam.spanName),
        {{"function.name", std::move(spanParam.function)},
         {"host.id", hostID_},
         {"instance.id", std::move(spanParam.instanceID)}},
        options);
}

TraceManager::OtelSpanSharedPtr TraceManager::StartSpanWithRecord(TraceManager::SpanParam &&spanParam)
{
    if (traceActor_ == nullptr) {
        YRLOG_WARN("(trace)tracer is nullptr");
        return TraceManager::OtelSpanSharedPtr(nullptr);
    }
    std::string spanKey = spanParam.traceID + "_" + spanParam.spanName;
    YRLOG_DEBUG("start span: {}", spanKey);
    auto span = StartSpan(std::move(spanParam));
    if (span != nullptr) {
        spanMap_.insert({ spanKey, span });
    }
    return span;
}

TraceManager::OtelSpanSharedPtr TraceManager::StartInvokeLocalSpan(const std::string &spanName,
                                                                   const InvokeRequest &request)
{
    if (traceActor_ == nullptr) {
        return TraceManager::OtelSpanSharedPtr(nullptr);
    }
    SpanParam spanParam = {
        .spanName = spanName,
        .traceID = request.traceid(),
        .spanID = request.spanid(),
        .function = request.function(),
        .instanceID = request.instanceid()
    };
    return StartSpan(std::move(spanParam));
}


void TraceManager::StopSpan(const std::string &traceID, const std::string &spanName,
                            const std::vector<std::string> &events)
{
    if (traceActor_ == nullptr) {
        return;
    }
    std::string spanKey = traceID + "_" + spanName;
    YRLOG_DEBUG("stop span, key: {}", spanKey);
    if (spanMap_.find(spanKey) == spanMap_.end()) {
        YRLOG_WARN("no span: {} found with traceID: {}", spanName, traceID);
        return;
    }
    auto span = spanMap_.at(spanKey);
    for (const auto &event : events) {
        YRLOG_DEBUG("stopspan add event: {}", event);
        span->AddEvent(event);
    }
    opentelemetry::trace::EndSpanOptions options;
    options.end_steady_time = opentelemetry::common::SteadyTimestamp(std::chrono::steady_clock::now());
    span->End(options);
    for (auto iter = spanMap_.begin(); iter != spanMap_.end(); iter++) {
        if (iter->first == spanKey) {
            spanMap_.erase(iter);
            YRLOG_DEBUG("stop current span, traceID: {}, spanName: {}", traceID, spanName);
            return;
        }
    }
    YRLOG_WARN("cannot find span: {} with traceID: {}", spanName, traceID);
}

std::string TraceManager::GetSpanIDFromStore(const std::string &traceID, const std::string &spanName)
{
    auto spanKey = traceID + "_" + spanName;
    if (spanMap_.find(spanKey) == spanMap_.end()) {
        YRLOG_WARN("cannot find span in spanMap_. spanKey: {}", spanKey);
        return "";
    }
    auto spanID = spanMap_.at(spanKey)->GetContext().span_id();
    return SpanIDToStr(spanID);
}

void TraceManager::TraceIdStrToArr(std::string traceID, uint8_t (&arr)[TRACE_ID_BUF_LENGTH])
{
    // cut trace id prefix job-xxxxxxxx-trace-
    (void)traceID.erase(std::remove(traceID.begin(), traceID.end(), '-'), traceID.end());
    if (traceID.size() < TRACE_ID_LENGTH) {
        (void)traceID.insert(0, TRACE_ID_LENGTH - traceID.size(), '0');
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

void TraceManager::SpanIdStrToArr(const std::string &spanID, uint8_t (&arr)[SPAN_ID_BUF_LENGTH])
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

void TraceManager::StartLocalSpanAndSet(const std::string &spanName, InvokeRequest *request)
{
    if (traceActor_ == nullptr) {
        YRLOG_DEBUG("(trace)trace actor is nullptr");
        return;
    }
    YRLOG_DEBUG("(trace)root invoke, traceID: {}, spanID: {}", request->traceid(), request->spanid());
    auto spanSharePtr = TraceManager::StartInvokeLocalSpan(spanName, *request);
    request->set_traceid(TraceManager::TraceIDToStr(spanSharePtr->GetContext().trace_id()));
    YRLOG_DEBUG("(trace)new traceID: {}", TraceManager::TraceIDToStr(spanSharePtr->GetContext().trace_id()));
}

void TraceManager::Clear()
{
    spanMap_.clear();
}

opentelemetry::trace::StartSpanOptions TraceManager::BuildOptWithParent(const string &traceID, const string &spanID)
{
    YRLOG_DEBUG("(trace)build options with parent, traceID: {}, spanID: {}", traceID, spanID);

    opentelemetry::trace::StartSpanOptions options;
    if (!traceID.empty()) {
        uint8_t traceIdArr[TRACE_ID_BUF_LENGTH] = {};
        uint8_t spanIdArr[SPAN_ID_BUF_LENGTH] = {};

        TraceManager::TraceIdStrToArr(traceID, traceIdArr);
        opentelemetry::trace::TraceId optlTraceId(traceIdArr);
        if (spanID.empty()) {
            spanIdArr[SPAN_ID_BUF_LENGTH - 1] = 0x01;
            YRLOG_DEBUG("(trace)spanID is empty, set root span");
        } else {
            TraceManager::SpanIdStrToArr(spanID, spanIdArr);
        }
        opentelemetry::trace::SpanId optlSpanId(spanIdArr);
        opentelemetry::trace::SpanContext spanContext(optlTraceId, optlSpanId, {}, false);

        YRLOG_DEBUG("(xhua)option is valid({})", spanContext.IsValid());

        options.parent = spanContext;
    } else {
        YRLOG_DEBUG("(trace)traceID is empty");
    }

    options.start_steady_time = opentelemetry::common::SteadyTimestamp(std::chrono::steady_clock::now());
    return options;
}
}  // namespace trace
}  // namespace functionsystem
