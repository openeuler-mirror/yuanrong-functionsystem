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

#ifndef COMMON_TRACE_LOG_FILE_EXPORTER_H
#define COMMON_TRACE_LOG_FILE_EXPORTER_H

#include <opentelemetry/sdk/trace/exporter.h>

namespace functionsystem {
namespace trace {

class LogFileExporter : public opentelemetry::sdk::trace::SpanExporter {
public:
    LogFileExporter() = default;
    ~LogFileExporter() override = default;

    std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override;

    opentelemetry::sdk::common::ExportResult Export(
        const opentelemetry::nostd::span<
            std::unique_ptr<opentelemetry::sdk::trace::Recordable>> &spans) noexcept override;

    bool Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds::max()) noexcept override;
};

}  // namespace trace
}  // namespace functionsystem

#endif  // COMMON_TRACE_LOG_FILE_EXPORTER_H
