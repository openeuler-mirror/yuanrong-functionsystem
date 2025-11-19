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

#ifndef FUNCTIONSYSTEM_PRIORITY_POLICY_H
#define FUNCTIONSYSTEM_PRIORITY_POLICY_H

#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/schedule_decision/queue/schedule_queue.h"
#include "common/resource_view/resource_type.h"

namespace functionsystem::schedule_decision {

class PriorityPolicy {
public:
    PriorityPolicy() = default;
    virtual ~PriorityPolicy() = default;

    virtual PriorityPolicyType GetPriorityPolicyType() = 0;
    virtual bool CanSchedule(const std::shared_ptr<QueueItem> &item) = 0;
    virtual void PrepareForScheduling(const std::shared_ptr<QueueItem> &item) = 0;
    virtual void StorePendingInfo(const std::shared_ptr<QueueItem> &item) = 0;
    virtual void ClearPendingInfos() = 0;
    bool NeedSuspend(const int32_t resCode, const int64_t timeout);
};

}

#endif // FUNCTIONSYSTEM_PRIORITY_POLICY_H
