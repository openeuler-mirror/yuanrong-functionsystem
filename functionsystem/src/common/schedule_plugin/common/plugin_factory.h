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

#ifndef COMMON_SCHEDULER_FRAMEWORK_PLUGINS_PLUGIN_FACTROY_H
#define COMMON_SCHEDULER_FRAMEWORK_PLUGINS_PLUGIN_FACTROY_H

#include <unordered_map>

#include "common/scheduler_framework/framework/framework.h"
#include "common/scheduler_framework/framework/policy.h"
#include "common/utils/singleton.h"

namespace functionsystem::schedule_framework {
using PluginCreator = std::function<std::shared_ptr<SchedulePolicyPlugin>()>;
class PluginFactory : public Singleton<PluginFactory> {
public:
    PluginFactory() = default;
    ~PluginFactory() override = default;
    std::shared_ptr<SchedulePolicyPlugin> CreatePlugin(const std::string &pluginName);
    void RegisterPluginCreator(const std::string &pluginName, const PluginCreator &gen);

private:
    std::unordered_map<std::string, PluginCreator> plugins_;
};
}
#endif  // COMMON_SCHEDULER_FRAMEWORK_PLUGINS_PLUGIN_FACTROY_H
