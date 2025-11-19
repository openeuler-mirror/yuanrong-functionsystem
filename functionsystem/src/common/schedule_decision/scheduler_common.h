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

#ifndef DOMAIN_SCHEDULER_COMMON_H
#define DOMAIN_SCHEDULER_COMMON_H
#include <string>

#include "async/future.hpp"
#include "common/status/status.h"
#include "common/proto/pb/message_pb.h"
#include "common/scheduler_framework/utils/score.h"

namespace functionsystem::schedule_decision {
struct ScheduleResult {
    std::string id;
    int32_t code;
    std::string reason;
    // [Deprecated] Will be merged into vectorAllocations selectedIndices
    std::vector<int> realIDs;
    // [Deprecated] Will be merged into vectorAllocations extendedInfo
    std::string heteroProductName;

    // [Deprecated] Will be merged into vectorAllocations allocationValues -- Resource's name: Value.Vectors
    std::map<std::string, ::resources::Value_Vectors> allocatedVectors = {};
    // Vector-based resource allocation results
    std::vector<schedule_framework::VectorResourceAllocation> vectorAllocations{};
    // only valid while successful & not domain preAllocated
    std::shared_ptr<litebus::Promise<Status>> allocatedPromise = nullptr;
    // only valid while instance or nested bundle was scheduled to rg bundle, otherwise is equal to id.
    std::string unitID = "";
};

struct GroupScheduleResult {
    int32_t code;
    std::string reason;
    std::vector<ScheduleResult> results;
};

enum class ScheduleType { DEFAULT, PRIORITY };
enum class PriorityPolicyType { FIFO, FAIRNESS };
enum class GroupSchedulePolicy { NONE, SPREAD, STRICT_SPREAD, PACK, STRICT_PACK };

[[maybe_unused]] static bool operator==(const ScheduleResult &l, const ScheduleResult &r)
{
    return l.id == r.id && l.code == r.code && l.reason == r.reason;
}

struct GroupSpec {
    struct RangeOpt {
        bool isRange;
        int32_t min;
        int32_t max;
        int32_t step;
    };
    std::vector<std::shared_ptr<messages::ScheduleRequest>> requests;
    std::string groupReqId;
    litebus::Future<std::string> cancelTag;
    RangeOpt rangeOpt;
    bool priority;
    int64_t timeout;
    common::GroupPolicy groupSchedulePolicy;
};
}  // namespace functionsystem::schedule_decision

#endif  // DOMAIN_SCHEDULER_COMMON_H
