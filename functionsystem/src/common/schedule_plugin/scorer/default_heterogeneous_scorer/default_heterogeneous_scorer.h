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

#ifndef FUNCTIONSYSTEM_DEFAULT_HETEROGENEOUS_SCORE_H
#define FUNCTIONSYSTEM_DEFAULT_HETEROGENEOUS_SCORE_H

#include <memory>
#include <string>

#include "common/proto/pb/posix/resource.pb.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/vectors_resource_tool.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/framework/policy.h"
#include "common/scheduler_framework/utils/score.h"
#include "common/status/status.h"

namespace functionsystem::schedule_plugin::score {
class DefaultHeterogeneousScorer : public schedule_framework::ScorePlugin {
public:
    DefaultHeterogeneousScorer() = default;
    ~DefaultHeterogeneousScorer() override = default;
    std::string GetPluginName() override;

    schedule_framework::NodeScore Score(const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
                                        const resource_view::InstanceInfo &instance,
                                        const resource_view::ResourceUnit &resourceUnit) override;
};

}

#endif // FUNCTIONSYSTEM_DEFAULT_HETEROGENEOUS_SCORE_H
