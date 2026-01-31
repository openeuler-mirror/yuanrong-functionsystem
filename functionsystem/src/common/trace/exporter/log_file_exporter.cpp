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

#include "log_file_exporter.h"
#include "common/logs/logging.h"
#include <opentelemetry/sdk/trace/recordable.h>
#include <opentelemetry/sdk/trace/span_data.h>

namespace functionsystem {
namespace trace {

std::unique_ptr<opentelemetry::sdk::trace::Recordable> LogFileExporter::MakeRecordable() noexcept
{
    return std::unique_ptr<opentelemetry::sdk::trace::Recordable>(
        new opentelemetry::sdk::trace::SpanData());
}

opentelemetry::sdk::common::ExportResult LogFileExporter::Export(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> &spans) noexcept
{
    for (auto &recordable : spans) {
        auto span = std::unique_ptr<opentelemetry::sdk::trace::SpanData>(
            static_cast<opentelemetry::sdk::trace::SpanData *>(recordable.release()));
        if (span != nullptr) {
            char trace_buf[32];
            char span_buf[16];
            span->GetTraceId().ToLowerBase16(trace_buf);
            span->GetSpanId().ToLowerBase16(span_buf);
            YRLOG_INFO("Trace: span_name={}, trace_id={}, span_id={}",
                      std::string(span->GetName()),
                      std::string(trace_buf, 32),
                      std::string(span_buf, 16));
        }
    }
    return opentelemetry::sdk::common::ExportResult::kSuccess;
}

bool LogFileExporter::Shutdown(std::chrono::microseconds) noexcept
{
    return true;
}

}  // namespace trace
}  // namespace functionsystem
