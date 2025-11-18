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

#ifndef COMMON_LEADER_BUSINESS_POLICY_H
#define COMMON_LEADER_BUSINESS_POLICY_H

#include <string>
#include "actor/aid.hpp"
#include "common/logs/logging.h"

namespace functionsystem::leader {

const std::string MASTER_STATUS = "master";
const std::string SLAVE_STATUS = "slave";

class BusinessPolicy {
public:
    BusinessPolicy() = default;
    virtual ~BusinessPolicy() = default;
    virtual void OnChange() = 0;
};

inline std::string GetStatus(const litebus::AID &curAID, const litebus::AID &masterAID, const std::string &curStatus)
{
    YRLOG_INFO("(transfer)you are {}, and master is {}", std::string(curAID), std::string(masterAID));
    std::string status = SLAVE_STATUS;
    if (curAID.Url() == masterAID.Url()) {
        status = MASTER_STATUS;
    }
    if (curStatus == status) {
        YRLOG_INFO("new status({}) is same with cur status", status);
        return "";
    }
    YRLOG_INFO("will change to new business({})", status);
    return status;
}

}  // namespace functionsystem:leader

#endif  // COMMON_LEADER_BUSINESS_POLICY_H
