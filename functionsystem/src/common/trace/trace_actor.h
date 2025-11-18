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


#ifndef COMMON_TRACE_TRACE_ACTOR_H
#define COMMON_TRACE_TRACE_ACTOR_H
#define HAVE_ABSEIL
#include "actor/actor.hpp"
#include "async/future.hpp"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/tracer.h"

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;

namespace functionsystem {

class TraceActor : public litebus::ActorBase {
public:
    TraceActor(const std::string &addr, const std::string &name);

    ~TraceActor() = default;

protected:
    void Init() override;

    void Finalize() override;

private:
    std::string grpcAddr_;
    std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> tracerProvider_;
};

}  // namespace functionsystem

#endif  // COMMON_TRACE_TRACE_ACTOR_H
