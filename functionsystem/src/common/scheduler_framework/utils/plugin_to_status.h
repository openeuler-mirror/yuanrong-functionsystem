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
#ifndef SCHEDULER_COMMON_PLUGIN_TO_STATUS_H
#define SCHEDULER_COMMON_PLUGIN_TO_STATUS_H

#include <string>
#include <unordered_map>
#include <vector>

#include "common/status/status.h"

namespace functionsystem::schedule_framework {

class PluginToStatus {
public:
    Status MergeStatus()
    {
        Status finalStatus(StatusCode::SUCCESS);

        if (pluginStatus.size() == 0) {
            return finalStatus;
        }

        for (auto it = pluginStatus.begin(); it != pluginStatus.end(); ++it) {
            if ((it->second).IsError()) {
                finalStatus = it->second;
            }

            finalStatus.AppendMessage((it->second).ToString());
        }

        return finalStatus;
    }

    void AddPluginStatus(const std::string &name, const Status &status)
    {
        pluginStatus[name] = status;
    }

private:
    std::unordered_map<std::string, Status> pluginStatus;
};
}
#endif // SCHEDULER_COMMON_PLUGIN_TO_STATUS_H