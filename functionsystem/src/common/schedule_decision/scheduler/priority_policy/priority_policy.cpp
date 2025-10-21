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

#include "priority_policy.h"

namespace functionsystem::schedule_decision {
bool PriorityPolicy::NeedSuspend(const int32_t resCode, const int64_t timeout)
{
    if (resCode != StatusCode::RESOURCE_NOT_ENOUGH && resCode != StatusCode::AFFINITY_SCHEDULE_FAILED) {
        return false;
    }

    if (timeout == 0) {
        return false;
    }

    return true;
}
}