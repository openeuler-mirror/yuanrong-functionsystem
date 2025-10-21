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

#ifndef COMMON_SCHEDULER_FRAMEWORK_PLUGINS_PLUGIN_REGISTER_H
#define COMMON_SCHEDULER_FRAMEWORK_PLUGINS_PLUGIN_REGISTER_H

#include "common/schedule_plugin/common/plugin_factory.h"

namespace functionsystem::schedule_framework {
class PluginRegister {
public:
    PluginRegister(const std::string &pluginName, const PluginCreator &gen) noexcept
    {
        PluginFactory::GetInstance().RegisterPluginCreator(pluginName, gen);
    }
    ~PluginRegister() = default;
};

#define REGISTER_SCHEDULER_PLUGIN(pluginName, gen) \
namespace { \
schedule_framework::PluginRegister regist##pluginName(pluginName, gen); \
}

}  // namespace functionsystem::schedule_framework

#endif  // COMMON_SCHEDULER_FRAMEWORK_PLUGINS_PLUGIN_REGISTER_H
