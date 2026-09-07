/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef COMMON_SCHEDULER_FRAMEWORK_UNIT_EVALUATION_SCOPE_H
#define COMMON_SCHEDULER_FRAMEWORK_UNIT_EVALUATION_SCOPE_H

#include <memory>

#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::schedule_framework {

// Keeps one effective-allocatable calculation alive across every Filter and
// Score plugin evaluating the same Unit. RAII guarantees that early returns
// and rejected candidates end the evaluation scope before the next Unit.
class UnitEvaluationScope {
public:
    UnitEvaluationScope(const std::shared_ptr<ScheduleContext> &ctx,
                        const resource_view::ResourceUnit &unit)
        : context_(std::dynamic_pointer_cast<PreAllocatedContext>(ctx))
    {
        if (context_ != nullptr) {
            context_->BeginUnitEvaluation(unit);
        }
    }

    ~UnitEvaluationScope()
    {
        if (context_ != nullptr) {
            context_->EndUnitEvaluation();
        }
    }

private:
    std::shared_ptr<PreAllocatedContext> context_;
};

}  // namespace functionsystem::schedule_framework

#endif  // COMMON_SCHEDULER_FRAMEWORK_UNIT_EVALUATION_SCOPE_H
