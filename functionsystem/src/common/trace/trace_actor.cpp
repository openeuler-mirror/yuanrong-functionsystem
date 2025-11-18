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

#include "trace_actor.h"

#include <async/async.hpp>

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "trace_manager.h"

using namespace std;
using namespace opentelemetry::sdk::resource;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace otlp_exporter = opentelemetry::exporter::otlp;

namespace functionsystem {

TraceActor::TraceActor(const std::string &addr, const std::string &name) : ActorBase(name)
{
    YRLOG_INFO("create trace actor, name: {}, addr: {}", name, addr);
    grpcAddr_ = addr;
}

void TraceActor::Init()
{
    YRLOG_INFO("init tracer");
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions opts;
    opts.endpoint = grpcAddr_;
    auto exporter = opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(opts);
    auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
    ResourceAttributes attributes = {
        { SemanticConventions::kTelemetrySdkLanguage, "" },
        { SemanticConventions::kTelemetrySdkName, "" },
        { SemanticConventions::kTelemetrySdkVersion, "" },
        { SemanticConventions::kServiceName, "yuanrong-kernel" },
    };

    auto provider = std::shared_ptr<trace_api::TracerProvider>(std::make_shared<trace_sdk::TracerProvider>(
        std::move(processor), opentelemetry::sdk::resource::Resource::Create(attributes)));
    tracerProvider_ = std::static_pointer_cast<trace_sdk::TracerProvider>(provider);
    trace_api::Provider::SetTracerProvider(provider);
}

void TraceActor::Finalize()
{
    YRLOG_INFO("enter trace finalize");
    ASSERT_IF_NULL(tracerProvider_);
    if (!tracerProvider_->ForceFlush()) {
        YRLOG_WARN("tracerProvider flush failed");
    }
    trace::TraceManager::Clear();
    std::shared_ptr<trace_api::TracerProvider> none;
    trace_api::Provider::SetTracerProvider(none);
}
}  // namespace functionsystem