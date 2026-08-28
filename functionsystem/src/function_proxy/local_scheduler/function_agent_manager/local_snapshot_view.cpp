/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 */

#include "function_proxy/local_scheduler/function_agent_manager/local_snapshot_view.h"

#include <iterator>

namespace functionsystem::local_scheduler {

Status LocalSnapshotView::Validate(const messages::LocalSnapshotMetadata &snapshot)
{
    if (snapshot.snapshotid().empty()
        || (snapshot.localrecoverycandidate() && snapshot.instanceid().empty())) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "local snapshot metadata is incomplete");
    }
    return Status::OK();
}

Status LocalSnapshotView::RebuildLatest()
{
    latestAnonymous_.clear();
    struct Candidate {
        const messages::LocalSnapshotMetadata *snapshot{nullptr};
    };
    std::unordered_map<std::string, Candidate> candidates;
    for (const auto &item : snapshots_) {
        const auto &snapshot = item.second.metadata;
        if (!snapshot.localrecoverycandidate()) {
            continue;
        }
        auto current = candidates.find(snapshot.instanceid());
        if (current == candidates.end()
            || snapshot.createdatunixseconds() > current->second.snapshot->createdatunixseconds()
            || (snapshot.createdatunixseconds() == current->second.snapshot->createdatunixseconds()
                && snapshot.snapshotid() > current->second.snapshot->snapshotid())) {
            candidates[snapshot.instanceid()] = {&snapshot};
            continue;
        }
    }
    for (const auto &[instanceID, candidate] : candidates) {
        latestAnonymous_[instanceID] = candidate.snapshot->snapshotid();
    }
    return Status::OK();
}

Status LocalSnapshotView::ReplaceAgentSnapshots(
    const std::string &functionAgentID,
    const std::vector<messages::LocalSnapshotMetadata> &snapshots)
{
    if (functionAgentID.empty()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "function agent ID is empty");
    }
    for (const auto &snapshot : snapshots) {
        const auto status = Validate(snapshot);
        if (status.IsError()) {
            return status;
        }
    }

    auto next = snapshots_;
    for (auto iter = next.begin(); iter != next.end();) {
        iter = iter->second.functionAgentID == functionAgentID ? next.erase(iter)
                                                               : std::next(iter);
    }
    for (const auto &snapshot : snapshots) {
        const auto existing = next.find(snapshot.snapshotid());
        if (existing != next.end() && existing->second.functionAgentID != functionAgentID) {
            return Status(StatusCode::SCHEDULE_CONFLICTED,
                          "local snapshot ID is reported by multiple agents");
        }
        next[snapshot.snapshotid()] = {functionAgentID, snapshot};
    }
    snapshots_.swap(next);
    return RebuildLatest();
}

LocalSnapshotRecordResult LocalSnapshotView::RecordCommitted(
    const std::string &functionAgentID,
    const messages::LocalSnapshotMetadata &snapshot)
{
    const auto validation = Validate(snapshot);
    if (functionAgentID.empty() || validation.IsError()) {
        return {functionAgentID.empty()
                    ? Status(StatusCode::ERR_PARAM_INVALID, "function agent ID is empty")
                    : validation,
                std::nullopt,
                {}};
    }
    const auto existing = snapshots_.find(snapshot.snapshotid());
    if (existing != snapshots_.end() && existing->second.functionAgentID != functionAgentID) {
        return {Status(StatusCode::SCHEDULE_CONFLICTED,
                       "local snapshot ID is already owned by another agent"),
                std::nullopt,
                {}};
    }
    const auto previous = snapshot.localrecoverycandidate()
        ? LatestAnonymous(snapshot.instanceid()) : std::nullopt;
    const auto previousAgent = previous.has_value()
        ? snapshots_.at(previous->snapshotid()).functionAgentID : std::string{};
    snapshots_[snapshot.snapshotid()] = {functionAgentID, snapshot};
    const auto rebuilt = RebuildLatest();
    if (rebuilt.IsError()) {
        return {rebuilt, std::nullopt, {}};
    }
    const auto latest = snapshot.localrecoverycandidate()
        ? LatestAnonymous(snapshot.instanceid()) : std::nullopt;
    return {Status::OK(), previous.has_value() && latest.has_value()
                                    && latest->snapshotid() == snapshot.snapshotid()
                                    && previous->snapshotid() != snapshot.snapshotid()
                                ? previous
                                : std::nullopt,
            previous.has_value() && latest.has_value()
                    && latest->snapshotid() == snapshot.snapshotid()
                    && previous->snapshotid() != snapshot.snapshotid()
                ? previousAgent
                : std::string{}};
}

std::optional<messages::LocalSnapshotMetadata> LocalSnapshotView::LatestAnonymous(
    const std::string &instanceID) const
{
    const auto latest = latestAnonymous_.find(instanceID);
    return latest == latestAnonymous_.end() ? std::nullopt : Find(latest->second);
}

std::optional<messages::LocalSnapshotMetadata> LocalSnapshotView::Find(
    const std::string &snapshotID) const
{
    const auto snapshot = snapshots_.find(snapshotID);
    return snapshot == snapshots_.end()
        ? std::nullopt
        : std::optional<messages::LocalSnapshotMetadata>(snapshot->second.metadata);
}

void LocalSnapshotView::Remove(const std::string &snapshotID)
{
    snapshots_.erase(snapshotID);
    (void)RebuildLatest();
}

}  // namespace functionsystem::local_scheduler
