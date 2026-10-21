/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_FRONTEND_CREATE_FAILURE_SNAPSHOT_H
#define FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_FRONTEND_CREATE_FAILURE_SNAPSHOT_H

#include <cstdint>
#include <string>

namespace functionsystem::local_scheduler {

// Last failure evidence + retry bookkeeping captured for a frontend create
// (POST /api/agent) while the frontend ready ticket is still pending. Only
// written when IsCreateByFrontend && create_error_policy==last_failure_on_timeout;
// it does NOT alter the reschedule/retry state machine. The ready-timeout closure
// reads this snapshot to surface the real supervisor error instead of a generic
// "ready call result timed out" string. Overwritten on every deploy failure so
// the last attempt's reason wins (retry fields are preserved across overwrites).
// The retriedTimes/firstFailure* fields are maintained by the readiness redeploy
// loop (HandleCheckReadinessFailure) so a success-after-retry create still shows
// its retry history: the consumer stops the yr.create span with these attributes
// when the ready ticket is fulfilled or times out.
struct FrontendCreateFailureSnapshot {
    bool present { false };
    int32_t code { 0 };
    std::string message;
    std::string instanceID;
    // Readiness redeploy count before the ready ticket was fulfilled/timed out;
    // 0 when the create succeeded on the first attempt.
    uint32_t retriedTimes { 0 };
    // First readiness failure (code may be 0 when only a message is available).
    int32_t firstFailureCode { 0 };
    std::string firstFailureMessage;
};

}  // namespace functionsystem::local_scheduler

#endif  // FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_FRONTEND_CREATE_FAILURE_SNAPSHOT_H
