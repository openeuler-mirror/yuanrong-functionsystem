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

#include "plugin_factory.h"

#include "common/logs/logging.h"
#include "common/status/status.h"

namespace functionsystem::schedule_framework {

std::shared_ptr<SchedulePolicyPlugin> PluginFactory::CreatePlugin(const std::string &pluginName)
{
    YRLOG_DEBUG("create scheduler plugin {}", pluginName);
    if (auto iter(plugins_.find(pluginName)); iter != plugins_.end()) {
        return iter->second();
    }
    return nullptr;
}

void PluginFactory::RegisterPluginCreator(const std::string &pluginName, const PluginCreator &gen)
{
    auto ret = plugins_.emplace(pluginName, gen);
    ASSERT_FS(ret.second);
    if (!ret.second) {
        YRLOG_ERROR("failed to register plugin creator {}", pluginName);
    }
}

}  // namespace functionsystem::schedule_framework
