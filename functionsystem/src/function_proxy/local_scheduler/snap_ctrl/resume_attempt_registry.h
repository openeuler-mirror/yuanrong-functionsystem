/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef LOCAL_SCHEDULER_RESUME_ATTEMPT_REGISTRY_H
#define LOCAL_SCHEDULER_RESUME_ATTEMPT_REGISTRY_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "async/future.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "common/utils/resume_identity.h"

namespace functionsystem::local_scheduler {

enum class ResumePhase {
    DEPLOYING,
    CREATING_CLIENT,
    STARTING,
    COMMITTING,
    RECONCILING,
    WINNER_CLEANUP,
    CLEARING_SNAPSHOT,
    LOSER_CLEANUP,
};

struct ResumeContext {
    resume_identity::TrustedResumeIdentity identity;
    resources::InstanceInfo candidate;
    std::shared_ptr<messages::ScheduleRequest> allocatedRequest;
    std::shared_ptr<messages::ScheduleRequest> runningRequest;
    std::vector<std::shared_ptr<litebus::Promise<messages::ScheduleResponse>>> completions;
    ResumePhase phase{ ResumePhase::DEPLOYING };
    Status failure{ StatusCode::FAILED, "resume failed" };
    bool heartbeatStarted{ false };
};

struct ResumeAttemptRegistration {
    enum class Result {
        INSERTED,
        COALESCED,
        CONFLICT,
    } result{ Result::CONFLICT };
    std::shared_ptr<ResumeContext> context;
};

/**
 * Process-local ownership of in-flight resume attempts.
 *
 * ETCD remains authoritative for logical state and CAS. This registry only
 * coalesces callbacks for one proxy process and validates that a RUNNING
 * record names the exact physical candidate before it is treated as winner.
 */
class ResumeAttemptRegistry {
public:
    ResumeAttemptRegistration Register(
        const resume_identity::TrustedResumeIdentity &identity,
        const resources::InstanceInfo &frozenPaused,
        const std::shared_ptr<messages::ScheduleRequest> &allocatedRequest,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &completion);

    ResumeAttemptRegistration RegisterCommittedWinner(
        const resume_identity::TrustedResumeIdentity &identity,
        const std::shared_ptr<messages::ScheduleRequest> &sourceRequest,
        const resources::InstanceInfo &authoritative);

    bool IsCurrent(const std::shared_ptr<ResumeContext> &context, ResumePhase phase) const;
    bool Contains(const std::shared_ptr<ResumeContext> &context) const;
    bool IsExactWinner(const std::shared_ptr<ResumeContext> &context,
                       const resources::InstanceInfo &authoritative) const;

    std::optional<std::vector<std::shared_ptr<litebus::Promise<messages::ScheduleResponse>>>> Remove(
        const std::shared_ptr<ResumeContext> &context);

private:
    using AttemptMap = std::unordered_map<std::string, std::shared_ptr<ResumeContext>>;
    std::unordered_map<std::string, AttemptMap> attempts_;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_RESUME_ATTEMPT_REGISTRY_H
