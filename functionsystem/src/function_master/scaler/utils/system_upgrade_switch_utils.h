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

#ifndef FUNCTION_MASTER_SCALER_UTILS_SYSTEM_UPGRADE_SWITCH_UTILS_H
#define FUNCTION_MASTER_SCALER_UTILS_SYSTEM_UPGRADE_SWITCH_UTILS_H

#include <nlohmann/json.hpp>

#include "common/logs/logging.h"

namespace functionsystem {

const std::string AZ_ID_KEY = "azId";
const std::string STATUS_KEY = "status";
const std::string DEFAULT_SYSTEM_UPGRADE_KEY = "/hms-caas/edgems/upgrade-zones";
const uint32_t SYSTEM_STATUS_UPGRADING = 1;
const uint32_t SYSTEM_STATUS_UPGRADED = 2;
const uint32_t DEFAULT_AZ_ID = -1;

struct UpgradeInfo {
    uint32_t azID = 0 ;  // optional
    uint32_t status = 0;
};

using SystemUpgradeHandler = std::function<void(bool isUpgrading)>;
using LocalSchedFaultHandler = std::function<void(const std::string &nodeName)>;
using EvictAgentHandler = std::function<litebus::Future<Status>(
    const std::string &localID, const std::shared_ptr<messages::EvictAgentRequest> &req)>;
using ScaleUpSystemFuncHandler = std::function<void(const std::string &, uint32_t)>;

struct ScalerHandlers {
    SystemUpgradeHandler systemUpgradeHandler{ nullptr };
    LocalSchedFaultHandler localSchedFaultHandler{ nullptr };
    EvictAgentHandler evictAgentHandler{ nullptr };
    ScaleUpSystemFuncHandler scaleUpSystemFuncHandler{ nullptr };
};

struct SystemUpgradeParam {
    bool isEnabled{ false };
    std::string systemUpgradeKey{ DEFAULT_SYSTEM_UPGRADE_KEY };
    uint32_t azID{ 0 };
    std::shared_ptr<MetaStorageAccessor> systemUpgradeWatcher{ nullptr };
    ScalerHandlers handlers;
};

[[maybe_unused]] inline std::shared_ptr<UpgradeInfo> TransToUpgradeInfoFromJson(const std::string &jsonStr)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonStr);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse system upgrade switch info, error: {}", error.what());
        return nullptr;
    }

    auto upgradeInfo = std::make_shared<UpgradeInfo>();
    if (auto iter = j.find(AZ_ID_KEY); iter != j.end() && iter.value().is_number()) {
        upgradeInfo->azID = iter.value();
    } else {
        // if az id doesn't exist in json, set id to default(-1),
        // don't care about az, will handle all events
        upgradeInfo->azID = DEFAULT_AZ_ID;
    }

    if (auto iter = j.find(STATUS_KEY); iter != j.end() && iter.value().is_number()) {
        upgradeInfo->status = iter.value();
    }
    return upgradeInfo;
}
}
#endif  // FUNCTION_MASTER_SCALER_UTILS_SYSTEM_UPGRADE_SWITCH_UTILS_H
