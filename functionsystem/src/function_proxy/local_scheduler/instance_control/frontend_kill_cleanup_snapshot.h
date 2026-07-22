/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_FRONTEND_KILL_CLEANUP_SNAPSHOT_H
#define FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_FRONTEND_KILL_CLEANUP_SNAPSHOT_H

#include <cstdint>
#include <string>

namespace functionsystem::local_scheduler {

// Payload-free, read-only evidence captured after a frontend kill dispatch.
// Unknown values are deliberately distinct from successful cleanup evidence.
struct FrontendKillCleanupSnapshot {
    bool requestTicketKnown { false };
    bool requestTicketCleared { false };
    bool instanceTicketKnown { false };
    bool instanceTicketCleared { false };
    std::string runtimeState { "unknown" };
    std::string instanceState { "unknown" };
    int64_t pendingInvokeCount { -1 };

    bool IsComplete() const
    {
        return requestTicketKnown && requestTicketCleared && instanceTicketKnown && instanceTicketCleared
               && runtimeState == "terminated" && (instanceState == "absent" || instanceState == "exited")
               && pendingInvokeCount == 0;
    }
};

}  // namespace functionsystem::local_scheduler

#endif  // FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_FRONTEND_KILL_CLEANUP_SNAPSHOT_H
