/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "resume_attempt_registry.h"

#include "common/types/instance_state.h"

namespace functionsystem::local_scheduler {
namespace {

bool SamePhysicalIdentity(const resources::InstanceInfo &left,
                          const resources::InstanceInfo &right)
{
    const auto sameExtension = [&left, &right](const std::string &key) {
        const auto leftIter = left.extensions().find(key);
        const auto rightIter = right.extensions().find(key);
        return leftIter == left.extensions().end()
            ? rightIter == right.extensions().end()
            : rightIter != right.extensions().end() && leftIter->second == rightIter->second;
    };
    return left.functionproxyid() == right.functionproxyid()
        && left.runtimeid() == right.runtimeid()
        && left.runtimeaddress() == right.runtimeaddress()
        && left.functionagentid() == right.functionagentid()
        && left.containerid() == right.containerid()
        && left.containerip() == right.containerip()
        && left.unitid() == right.unitid()
        && left.executortype() == right.executortype()
        && left.starttime() == right.starttime()
        && left.proxygrpcaddress() == right.proxygrpcaddress()
        && sameExtension("PID")
        && sameExtension("portForward");
}

}  // namespace

ResumeAttemptRegistration ResumeAttemptRegistry::Register(
    const resume_identity::TrustedResumeIdentity &identity,
    const resources::InstanceInfo &frozenPaused,
    const std::shared_ptr<messages::ScheduleRequest> &allocatedRequest,
    const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &completion)
{
    if (allocatedRequest == nullptr || completion == nullptr
        || !identity.MatchesSchedule(*allocatedRequest)) {
        return {};
    }
    auto &attempts = attempts_[identity.logicalInstanceID];
    if (auto existing = attempts.find(identity.targetAttemptID); existing != attempts.end()) {
        if (existing->second->identity.Digest() != identity.Digest()) {
            return {};
        }
        existing->second->completions.emplace_back(completion);
        return { ResumeAttemptRegistration::Result::COALESCED, existing->second };
    }
    if (!identity.MatchesAuthoritative(frozenPaused)) {
        if (attempts.empty()) {
            attempts_.erase(identity.logicalInstanceID);
        }
        return {};
    }
    auto context = std::make_shared<ResumeContext>();
    context->identity = identity;
    context->allocatedRequest = std::make_shared<messages::ScheduleRequest>(*allocatedRequest);
    context->completions.emplace_back(completion);
    attempts.emplace(identity.targetAttemptID, context);
    return { ResumeAttemptRegistration::Result::INSERTED, std::move(context) };
}

ResumeAttemptRegistration ResumeAttemptRegistry::RegisterCommittedWinner(
    const resume_identity::TrustedResumeIdentity &identity,
    const std::shared_ptr<messages::ScheduleRequest> &sourceRequest,
    const resources::InstanceInfo &authoritative)
{
    if (sourceRequest == nullptr || !authoritative.has_snapshotinfo()
        || !identity.MatchesCommittedWinner(authoritative)
        || !resume_identity::SnapshotIdentityMatches(authoritative.snapshotinfo(), identity.snapshot)) {
        return {};
    }
    auto &attempts = attempts_[identity.logicalInstanceID];
    if (auto existing = attempts.find(identity.targetAttemptID); existing != attempts.end()) {
        return existing->second->identity.Digest() == identity.Digest()
            ? ResumeAttemptRegistration{ ResumeAttemptRegistration::Result::COALESCED, existing->second }
            : ResumeAttemptRegistration{};
    }
    auto context = std::make_shared<ResumeContext>();
    context->identity = identity;
    context->candidate = authoritative;
    context->allocatedRequest = std::make_shared<messages::ScheduleRequest>(*sourceRequest);
    context->runningRequest = std::make_shared<messages::ScheduleRequest>();
    context->runningRequest->set_requestid(identity.targetAttemptID);
    context->runningRequest->mutable_instance()->CopyFrom(authoritative);
    context->phase = ResumePhase::WINNER_CLEANUP;
    context->heartbeatStarted = true;
    attempts.emplace(identity.targetAttemptID, context);
    return { ResumeAttemptRegistration::Result::INSERTED, std::move(context) };
}

bool ResumeAttemptRegistry::Contains(const std::shared_ptr<ResumeContext> &context) const
{
    if (context == nullptr) {
        return false;
    }
    const auto instance = attempts_.find(context->identity.logicalInstanceID);
    if (instance == attempts_.end()) {
        return false;
    }
    const auto attempt = instance->second.find(context->identity.targetAttemptID);
    return attempt != instance->second.end() && attempt->second == context;
}

bool ResumeAttemptRegistry::IsCurrent(const std::shared_ptr<ResumeContext> &context,
                                      ResumePhase phase) const
{
    return Contains(context) && context->phase == phase;
}

bool ResumeAttemptRegistry::IsExactWinner(
    const std::shared_ptr<ResumeContext> &context,
    const resources::InstanceInfo &authoritative) const
{
    return Contains(context)
        && authoritative.instanceid() == context->identity.logicalInstanceID
        && authoritative.requestid() == context->identity.logicalRequestID
        && authoritative.tenantid() == context->identity.tenantID
        && authoritative.version() == context->identity.expectedVersion + 1
        && authoritative.instancestatus().code() == static_cast<int32_t>(InstanceState::RUNNING)
        && (!authoritative.has_snapshotinfo()
            || resume_identity::SnapshotIdentityMatches(
                authoritative.snapshotinfo(), context->identity.snapshot))
        && SamePhysicalIdentity(authoritative, context->candidate);
}

std::optional<std::vector<std::shared_ptr<litebus::Promise<messages::ScheduleResponse>>>>
ResumeAttemptRegistry::Remove(const std::shared_ptr<ResumeContext> &context)
{
    if (!Contains(context)) {
        return std::nullopt;
    }
    auto instance = attempts_.find(context->identity.logicalInstanceID);
    auto attempt = instance->second.find(context->identity.targetAttemptID);
    auto completions = std::move(attempt->second->completions);
    instance->second.erase(attempt);
    if (instance->second.empty()) {
        attempts_.erase(instance);
    }
    return completions;
}

}  // namespace functionsystem::local_scheduler
