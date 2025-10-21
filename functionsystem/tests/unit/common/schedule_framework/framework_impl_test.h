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

#include <gmock/gmock.h>

#include <string>

#include "common/scheduler_framework/framework/policy.h"

namespace functionsystem::test {
using namespace functionsystem::schedule_framework;
class MockPreFilterPolicy : public PreFilterPlugin {
public:
    MOCK_METHOD(std::string, GetPluginName, (), (override));
    MOCK_METHOD(std::shared_ptr<PreFilterResult>, PreFilter,
                (const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                 const resource_view::ResourceUnit &resourceUnit),
                (override));
    MOCK_METHOD(bool, PrefilterMatched, (const resource_view::InstanceInfo &instance), (override));
};

class MockScorePlugin : public ScorePlugin {
public:
    MOCK_METHOD(std::string, GetPluginName, (), (override));
    MOCK_METHOD(NodeScore, Score,
                (const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                 const resource_view::ResourceUnit &resourceUnit),
                (override));
};

class MockFilterPlugin : public FilterPlugin {
public:
    MOCK_METHOD(std::string, GetPluginName, (), (override));
    MOCK_METHOD(Filtered, Filter,
                (const std::shared_ptr<ScheduleContext> &ctx, const resource_view::InstanceInfo &instance,
                 const resource_view::ResourceUnit &resourceUnit),
                (override));
};

}  // namespace functionsystem::test
